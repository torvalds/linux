/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 *     MAC80211 support module
 */

#include <linux/etherdevice.h>
#include <linux/workqueue.h>
#include <linux/nl80211.h>
#include <linux/ieee80211.h>
#include <linux/slab.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
#include <net/regulatory.h>
#endif
/* for support scan in p2p concurrent */
#include <../net/mac80211/ieee80211_i.h>
#include "esp_pub.h"
#include "esp_sip.h"
#include "esp_ctrl.h"
#include "esp_sif.h"
#include "esp_debug.h"
#include "esp_wl.h"
#include "esp_utils.h"
#include <linux/rfkill-wlan.h>
#include "esp_mac80211.h"
#define ESP_IEEE80211_DBG esp_dbg

#define GET_NEXT_SEQ(seq) (((seq) +1) & 0x0fff)

extern void reset_signal_count(void);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
static void beacon_tim_init(void);
static u8 beacon_tim_save(u8 this_tim);
static bool beacon_tim_alter(struct sk_buff *beacon);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)) && defined(P2P_CONCURRENT)
static u8 esp_mac_addr[ETH_ALEN * 2];
#endif
static u8 getaddr_index(u8 * addr, struct esp_pub *epub);
static int dup_addr_by_index(u8 *addr, struct esp_pub *epub, u8 index);

static
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
void
esp_op_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control, struct sk_buff *skb)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
void
esp_op_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
#else
int
esp_op_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
#endif /* NEW_KERNEL */
{
	struct esp_pub *epub = (struct esp_pub *)hw->priv;

	ESP_IEEE80211_DBG(ESP_DBG_LOG, "%s enter\n", __func__);
	if (!mod_support_no_txampdu() &&
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
                	cfg80211_get_chandef_type(&epub->hw->conf.chandef) != NL80211_CHAN_NO_HT
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
			hw->conf.channel_type != NL80211_CHAN_NO_HT
#else
			!(hw->conf.flags&IEEE80211_CONF_SUPPORT_HT_MODE)
#endif                
	   ) {
		struct ieee80211_tx_info * tx_info = IEEE80211_SKB_CB(skb);
		struct ieee80211_hdr * wh = (struct ieee80211_hdr *)skb->data;
		if(ieee80211_is_data_qos(wh->frame_control)) {
			if(!(tx_info->flags & IEEE80211_TX_CTL_AMPDU)) {
				u8 tidno = ieee80211_get_qos_ctl(wh)[0] & IEEE80211_QOS_CTL_TID_MASK;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
				struct ieee80211_sta *sta = control->sta;
				struct esp_node * node = (struct esp_node *)sta->drv_priv;
				if(sta->ht_cap.ht_supported)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
				struct ieee80211_sta *sta = tx_info->control.sta;
				struct esp_node * node = (struct esp_node *)sta->drv_priv;
				if(sta->ht_cap.ht_supported)
#else
				struct esp_node * node = esp_get_node_by_addr(epub, wh->addr1);
				if(node->ht_info.ht_supported)
#endif
				{
					struct esp_tx_tid *tid = &node->tid[tidno];
					//record ssn
					spin_lock_bh(&epub->tx_ampdu_lock);
					tid->ssn = GET_NEXT_SEQ(le16_to_cpu(wh->seq_ctrl)>>4);
					ESP_IEEE80211_DBG(ESP_DBG_TRACE, "tidno:%u,ssn:%u\n", tidno, tid->ssn);
					spin_unlock_bh(&epub->tx_ampdu_lock);
				}
			} else {
				ESP_IEEE80211_DBG(ESP_DBG_TRACE, "tx ampdu pkt, sn:%u, %u\n", le16_to_cpu(wh->seq_ctrl)>>4, skb->len);
			}
		}
	}

#ifdef GEN_ERR_CHECKSUM
	esp_gen_err_checksum(skb);
#endif

	sip_tx_data_pkt_enqueue(epub, skb);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39))
	return NETDEV_TX_OK;
#endif /*2.6.39*/
}

static int esp_op_start(struct ieee80211_hw *hw)
{
	struct esp_pub *epub;

	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s\n", __func__);

	if (!hw) {
		ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s no hw!\n", __func__);
		return -EINVAL;
	}

	epub = (struct esp_pub *)hw->priv;

	if (!epub) {
		ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s no epub!\n", __func__);
		return EINVAL;
	}
	/*add rfkill poll function*/

	atomic_set(&epub->wl.off, 0);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
	wiphy_rfkill_start_polling(hw->wiphy);
#endif
	return 0;
}

static void esp_op_stop(struct ieee80211_hw *hw)
{
	struct esp_pub *epub;

	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s\n", __func__);

	if (!hw) {
		ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s no hw!\n", __func__);
		return;
	}

	epub = (struct esp_pub *)hw->priv;

	if (!epub) {
		ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s no epub!\n", __func__);
		return;
	}

	atomic_set(&epub->wl.off, 1);

#ifdef HOST_RESET_BUG
	mdelay(200);
#endif

	if (epub->wl.scan_req) {
		hw_scan_done(epub, true);
		epub->wl.scan_req=NULL;
		//msleep(2);
	}
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39))        
#ifdef CONFIG_PM
static int esp_op_suspend(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan)
{
	esp_dbg(ESP_DBG_OP, "%s\n", __func__);

	return 0;
}

static int esp_op_resume(struct ieee80211_hw *hw)
{
	esp_dbg(ESP_DBG_OP, "%s\n", __func__);

	return 0;
}
#endif //CONFIG_PM
#endif

void esp_sendup_deauth(struct esp_pub *epub, u8 *sta_addr)
{
 	struct sk_buff *skb;
	struct esp_80211_deauth *deauth;

	esp_dbg(ESP_DBG_TRACE, "%s", __func__);

       	skb = __dev_alloc_skb(sizeof(struct esp_80211_deauth), GFP_KERNEL);
	if (!skb)
		return;
	
	skb_put(skb, sizeof(struct esp_80211_deauth));

	deauth = (struct esp_80211_deauth *)skb->data;
	deauth->hdr.frame_control = 0x00c0; /* deauth  */
	deauth->hdr.duration_id = 0x003c; /* 60ms */
	memcpy(deauth->hdr.addr1, epub->master_addr, ETH_ALEN); /* dst */
	memcpy(deauth->hdr.addr2, sta_addr, ETH_ALEN); /* src */
	memcpy(deauth->hdr.addr3, epub->master_addr, ETH_ALEN); /* bssid */
	deauth->hdr.seq_ctrl = 0x0000;
	deauth->reason_code = 0x0003; /* sta leaving */

#ifndef RX_SENDUP_SYNC
	skb_queue_tail(&epub->rxq, skb);
	queue_work(epub->esp_wkq, &sip->epub->sendup_work);
#else
	local_bh_disable();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
	ieee80211_rx(epub->hw, skb);
#else
	ieee80211_rx(epub->hw, skb ,(struct ieee80211_rx_status *)skb->cb);
#endif
	local_bh_enable();
#endif /* RX_SENDUP_SYNC */
}

static void esp_send_nulldata(struct esp_pub *epub, struct esp_vif *evif, struct esp_node *enode)
{
 	struct sk_buff *skb;
	struct esp_80211_nulldata *nulldata;
	struct ieee80211_vif *vif = container_of((void *)evif, struct ieee80211_vif, drv_priv);

	esp_dbg(ESP_DBG_TRACE, "%s enter, enode %d", __func__, enode->index);

	skb = dev_alloc_skb(epub->hw->extra_tx_headroom+ sizeof(*nulldata));
	if (!skb)
		return;

	skb_reserve(skb, epub->hw->extra_tx_headroom);
	skb_put(skb, sizeof(*nulldata));
	nulldata = (struct esp_80211_nulldata *)skb->data;
	memset(nulldata, 0x00, sizeof(*nulldata));	

	nulldata->hdr.frame_control = 0x0248; /* null data , FROM DS 1 */
	nulldata->hdr.duration_id = 0x003c; /* 60ms */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
	memcpy(nulldata->hdr.addr1, enode->sta->addr, ETH_ALEN); /* dst */
#else
	memcpy(nulldata->hdr.addr1, enode->addr, ETH_ALEN); /* dst */
#endif
	memcpy(nulldata->hdr.addr2, epub->master_addr, ETH_ALEN); /* bssid */
	memcpy(nulldata->hdr.addr3, epub->master_addr, ETH_ALEN); /* src */
	nulldata->hdr.seq_ctrl = 0x0000;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
#endif
	IEEE80211_SKB_CB(skb)->control.vif = vif;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
	IEEE80211_SKB_CB(skb)->control.sta = enode->sta;
#endif
	IEEE80211_SKB_CB(skb)->control.hw_key = NULL;
	sip_tx_data_pkt_enqueue(epub, skb);
}

static void esp_send_nulldata_alarm(unsigned long data)
{
	u8 tim = 0;
	u8 index;
	u32 map;
	struct esp_node *enode;
	struct esp_vif *evif = (struct esp_vif *)data;
	struct esp_pub *epub = evif->epub;
	
	esp_dbg(ESP_DBG_TRACE, "%s enter", __func__);
	map = epub->enodes_map;
	while (map != 0) {
		index = ffs(map) - 1;
		if (index > ESP_PUB_MAX_STA)
			break;
		enode = esp_get_node_by_index(epub, index);
		if (enode && enode->ifidx == epub->master_ifidx) {
			if (atomic_read(&enode->sta_state) == ESP_STA_STATE_NORM) {
				if (atomic_dec_and_test(&enode->time_remain)) {
					atomic_set(&enode->sta_state, ESP_STA_STATE_WAIT);
					esp_send_nulldata(epub, evif, enode);
				} else if (atomic_read(&enode->time_remain) == 1) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
					tim |= (1<<enode->sta->aid);
#else
					tim |= (1<<enode->aid);
#endif
				}
			} else if (atomic_read(&enode->sta_state) == ESP_STA_STATE_WAIT) {
				atomic_inc(&enode->loss_count);
				if (atomic_read(&enode->loss_count) <= ESP_LOSS_COUNT_MAX) {
					atomic_set(&enode->sta_state, ESP_STA_STATE_WAIT);
					esp_send_nulldata(epub, evif, enode);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
					tim |= (1<<enode->sta->aid);
#else
					tim |= (1<<enode->aid);
#endif
				} else {
					atomic_set(&enode->sta_state, ESP_STA_STATE_LOST);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
					esp_sendup_deauth(epub, enode->sta->addr);
#else
					esp_sendup_deauth(epub, enode->addr);
#endif
				}
			}
		}
		map &= ~(1<<index);
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	if (tim) {
		beacon_tim_save(tim);
		esp_dbg(ESP_DBG_TRACE, "tim 0x%02x", tim);
	}
#endif

	mod_timer(&evif->nulldata_timer, jiffies + msecs_to_jiffies(ESP_ND_TIMER_INTERVAL));
	
}

void esp_sta_gc_conn_monitor_open(struct esp_pub *epub, struct esp_vif *evif)
{
	if (epub == NULL || evif == NULL)
		return;

	esp_dbg(ESP_DBG_TRACE, "master_ifidx %d\n", epub->master_ifidx);

	dup_addr_by_index(epub->master_addr, epub, evif->index);
	epub->master_ifidx =  evif->index;

	init_timer(&evif->nulldata_timer);
	evif->nulldata_timer.expires = jiffies + msecs_to_jiffies(1000);
        evif->nulldata_timer.data = (unsigned long) evif;
        evif->nulldata_timer.function = esp_send_nulldata_alarm;
        add_timer(&evif->nulldata_timer);
}

void esp_sta_gc_conn_monitor_close(struct esp_pub *epub, struct esp_vif *evif)
{
	if (epub == NULL || evif == NULL)
		return;

	esp_dbg(ESP_DBG_TRACE, "%s enter", __func__);

        del_timer_sync(&evif->nulldata_timer);
	memset(epub->master_addr, 0x00, ETH_ALEN);
	epub->master_ifidx = ESP_PUB_MAX_VIF;
}


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
static int esp_op_add_interface(struct ieee80211_hw *hw,
		struct ieee80211_if_init_conf *conf)
#else
static int esp_op_add_interface(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif)
#endif
{
	struct esp_pub *epub = (struct esp_pub *)hw->priv;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
	struct ieee80211_vif *vif = conf->vif;
#endif
	struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;
	struct sip_cmd_setvif svif;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter: type %d, addr %pM\n", __func__, vif->type, conf->mac_addr);
#else
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter: type %d, addr %pM\n", __func__, vif->type, vif->addr);
#endif

	memset(&svif, 0, sizeof(struct sip_cmd_setvif));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
	memcpy(svif.mac, conf->mac_addr, ETH_ALEN);
	evif->index = svif.index = getaddr_index(conf->mac_addr, epub);
#else
	memcpy(svif.mac, vif->addr, ETH_ALEN);
	evif->index = svif.index = getaddr_index(vif->addr, epub);
#endif
	evif->epub = epub;
	epub->vif = vif;
	svif.set = 1;
	if((1 << svif.index) & epub->vif_slot){
		ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s interface %d already used\n", __func__, svif.index);
		return -EOPNOTSUPP;
	}
	epub->vif_slot |= 1 << svif.index;

	if (svif.index == ESP_PUB_MAX_VIF) {
		ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s only support MAX %d interface\n", __func__, ESP_PUB_MAX_VIF);
		return -EOPNOTSUPP;
	}

	switch (vif->type) {
		case NL80211_IFTYPE_STATION:
			//if (svif.index == 1)
			//	vif->type = NL80211_IFTYPE_UNSPECIFIED;
			ESP_IEEE80211_DBG(ESP_SHOW, "%s STA \n", __func__);
			svif.op_mode = 0;
			svif.is_p2p = 0;
			break;
		case NL80211_IFTYPE_AP:
			ESP_IEEE80211_DBG(ESP_SHOW, "%s AP \n", __func__);
			svif.op_mode = 1;
			svif.is_p2p = 0;
			break;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		case NL80211_IFTYPE_P2P_CLIENT:
			ESP_IEEE80211_DBG(ESP_SHOW, "%s P2P_CLIENT \n", __func__);
			svif.op_mode = 0;
			svif.is_p2p = 1;
			break;
		case NL80211_IFTYPE_P2P_GO:
			ESP_IEEE80211_DBG(ESP_SHOW, "%s P2P_GO \n", __func__);
			svif.op_mode = 1;
			svif.is_p2p = 1;
			break;
#endif
		case NL80211_IFTYPE_UNSPECIFIED:
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_MONITOR:
		default:
			ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s does NOT support type %d\n", __func__, vif->type);
			return -EOPNOTSUPP;
	}

	sip_cmd(epub, SIP_CMD_SETVIF, (u8 *)&svif, sizeof(struct sip_cmd_setvif));
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static int esp_op_change_interface(struct ieee80211_hw *hw,
                                   struct ieee80211_vif *vif,
                                   enum nl80211_iftype new_type, bool p2p)
{
	struct esp_pub *epub = (struct esp_pub *)hw->priv;
	struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;
	struct sip_cmd_setvif svif;
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter,change to if:%d \n", __func__, new_type);
	
	if (new_type == NL80211_IFTYPE_AP) {
		ESP_IEEE80211_DBG(ESP_SHOW, "%s enter,change to AP \n", __func__);
	}

	if (vif->type != new_type) {
		ESP_IEEE80211_DBG(ESP_SHOW, "%s type from %d to %d\n", __func__, vif->type, new_type);
	}
	
	memset(&svif, 0, sizeof(struct sip_cmd_setvif));
	memcpy(svif.mac, vif->addr, ETH_ALEN);
	svif.index = evif->index;
	svif.set = 2;
	
	switch (new_type) {
        case NL80211_IFTYPE_STATION:
                svif.op_mode = 0;
                svif.is_p2p = p2p;
                break;
        case NL80211_IFTYPE_AP:
                svif.op_mode = 1;
                svif.is_p2p = p2p;
                break;
        case NL80211_IFTYPE_P2P_CLIENT:
                svif.op_mode = 0;
                svif.is_p2p = 1;
                break;
        case NL80211_IFTYPE_P2P_GO:
                svif.op_mode = 1;
                svif.is_p2p = 1;
                break;
        case NL80211_IFTYPE_UNSPECIFIED:
        case NL80211_IFTYPE_ADHOC:
        case NL80211_IFTYPE_AP_VLAN:
        case NL80211_IFTYPE_WDS:
        case NL80211_IFTYPE_MONITOR:
        default:
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s does NOT support type %d\n", __func__, vif->type);
                return -EOPNOTSUPP;
        }
	sip_cmd(epub, SIP_CMD_SETVIF, (u8 *)&svif, sizeof(struct sip_cmd_setvif));
        return 0;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
static void esp_op_remove_interface(struct ieee80211_hw *hw,
                                    struct ieee80211_if_init_conf *conf)
#else
static void esp_op_remove_interface(struct ieee80211_hw *hw,
                                    struct ieee80211_vif *vif)
#endif
{
	struct esp_pub *epub = (struct esp_pub *)hw->priv;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
	struct ieee80211_vif *vif = conf->vif;
#endif
	struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;
	struct sip_cmd_setvif svif;

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 30))
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, vif addr %pM\n", __func__, conf->mac_addr);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, vif addr %pM, beacon enable %x\n", __func__, conf->mac_addr, vif->bss_conf.enable_beacon);
#else
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, vif addr %pM, beacon enable %x\n", __func__, vif->addr, vif->bss_conf.enable_beacon);
#endif

	memset(&svif, 0, sizeof(struct sip_cmd_setvif));
	svif.index = evif->index;
	epub->vif_slot &= ~(1 << svif.index);

	if(evif->ap_up){
		evif->beacon_interval = 0;
		del_timer_sync(&evif->beacon_timer);
		evif->ap_up = false;
		esp_sta_gc_conn_monitor_close(epub, evif);
	}
	epub->vif = NULL;
	evif->epub = NULL;

	sip_cmd(epub, SIP_CMD_SETVIF, (u8 *)&svif, sizeof(struct sip_cmd_setvif));

	/* clean up tx/rx queue */

}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
#define BEACON_TIM_SAVE_MAX 12
u8 beacon_tim_saved[BEACON_TIM_SAVE_MAX];
int beacon_tim_count;
spinlock_t tim_lock;
static void beacon_tim_init(void)
{
	memset(beacon_tim_saved, BEACON_TIM_SAVE_MAX, 0);
	beacon_tim_count = 0;
	spin_lock_init(&tim_lock);
}

static u8 beacon_tim_save(u8 this_tim)
{
	u8 all_tim = 0;
	int i;

	spin_lock(&tim_lock);
	beacon_tim_saved[beacon_tim_count] = this_tim;
	if(++beacon_tim_count >= BEACON_TIM_SAVE_MAX)
		beacon_tim_count = 0;
	for(i = 0; i < BEACON_TIM_SAVE_MAX; i++)
		all_tim |= beacon_tim_saved[i];
	spin_unlock(&tim_lock);

	return all_tim;
}

static bool beacon_tim_alter(struct sk_buff *beacon)
{
        u8 *p, *tim_end;
	u8 tim_count;
        int len;
        int remain_len;
        struct ieee80211_mgmt * mgmt;

        if (beacon == NULL)
                return false;

        mgmt = (struct ieee80211_mgmt *)((u8 *)beacon->data);

        remain_len = beacon->len - ((u8 *)mgmt->u.beacon.variable - (u8 *)mgmt + 12);
        p = mgmt->u.beacon.variable;

        while (remain_len > 0) {
                if (*p == WLAN_EID_TIM) {       // tim field
                	len = *(++p);
                        tim_end = p + len;
			tim_count = *(++p);
			p += 2;
			//multicast
			if(tim_count == 0)
			    *p |= 0x1;
			if((*p & 0xfe) == 0 && tim_end >= p+1){// we only support 8 sta in this case
                        	p++;
				*p = beacon_tim_save(*p);
			}
                        return tim_count == 0;
                } else {
               		len = *(++p);
                	p += (len + 1);
		}
                remain_len -= (2 + len);
        }

        return false;
}

unsigned long init_jiffies;
unsigned long cycle_beacon_count;
static void drv_handle_beacon(unsigned long data)
{
	struct ieee80211_vif *vif = (struct ieee80211_vif *) data;
	struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;
	struct sk_buff *beacon;
	struct sk_buff *skb;
	static int dbgcnt = 0;
	bool tim_reach = false;

	if(evif->epub == NULL)
		return;

	mdelay(2400 * (cycle_beacon_count % 25) % 10000 /1000);
	
	beacon = ieee80211_beacon_get(evif->epub->hw, vif);

	tim_reach = beacon_tim_alter(beacon);

	if (beacon && !(dbgcnt++ % 600)) {
		ESP_IEEE80211_DBG(ESP_SHOW, " beacon length:%d,fc:0x%x\n", beacon->len,
			((struct ieee80211_mgmt *)(beacon->data))->frame_control);

	}

	if(beacon)
		sip_tx_data_pkt_enqueue(evif->epub, beacon);

	if(cycle_beacon_count++ == 100){
		init_jiffies = jiffies;
		cycle_beacon_count -= 100;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))    
	mod_timer(&evif->beacon_timer, init_jiffies + msecs_to_jiffies(cycle_beacon_count * vif->bss_conf.beacon_int*1024/1000));
#else
	mod_timer(&evif->beacon_timer, init_jiffies +msecs_to_jiffies(cycle_beacon_count * evif->beacon_interval*1024/1000));
#endif
	//FIXME:the packets must be sent at home channel
	//send buffer mcast frames
	if(tim_reach){
		skb = ieee80211_get_buffered_bc(evif->epub->hw, vif);
		while (skb) {
			sip_tx_data_pkt_enqueue(evif->epub, skb);
			skb = ieee80211_get_buffered_bc(evif->epub->hw, vif);
		}
	}
}

static void init_beacon_timer(struct ieee80211_vif *vif)
{
	struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;

	ESP_IEEE80211_DBG(ESP_DBG_OP, " %s enter: beacon interval %x\n", __func__, evif->beacon_interval);

	beacon_tim_init();
	init_timer(&evif->beacon_timer);  //TBD, not init here...
	cycle_beacon_count = 1;
	init_jiffies = jiffies;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
	evif->beacon_timer.expires = init_jiffies + msecs_to_jiffies(cycle_beacon_count * vif->bss_conf.beacon_int*1024/1000);
#else
	evif->beacon_timer.expires = init_jiffies + msecs_to_jiffies(cycle_beacon_count * evif->beacon_interval*1024/1000);
#endif
	evif->beacon_timer.data = (unsigned long) vif;
	evif->beacon_timer.function = drv_handle_beacon;
	add_timer(&evif->beacon_timer);
}
#endif

/*
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
    static void init_beacon_timer(struct ieee80211_vif *vif)
#else
    static void init_beacon_timer(struct ieee80211_conf *conf)
#endif
{
	struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
	ESP_IEEE80211_DBG(ESP_DBG_OP, " %s enter: beacon interval %x\n", __func__, vif->bss_conf.beacon_int);
#else
	ESP_IEEE80211_DBG(ESP_DBG_OP, " %s enter: beacon interval %x\n", __func__, conf->beacon_int);
#endif
	init_timer(&evif->beacon_timer);  //TBD, not init here...
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
	evif->beacon_timer.expires=jiffies+msecs_to_jiffies(vif->bss_conf.beacon_int*102/100);
	evif->beacon_timer.data = (unsigned long) vif;
#else
	evif->beacon_timer.expires=jiffies+msecs_to_jiffies(conf->beacon_int*102/100);
	evif->beacon_timer.data = (unsigned long) conf;
#endif
	//evif->beacon_timer.data = (unsigned long) vif;
	evif->beacon_timer.function = drv_handle_beacon;
	add_timer(&evif->beacon_timer);
}
*/

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
static int esp_op_config(struct ieee80211_hw *hw, u32 changed)
#else
static int esp_op_config(struct ieee80211_hw *hw, struct ieee80211_conf *conf)
#endif
{
	//struct ieee80211_conf *conf = &hw->conf;

	struct esp_pub *epub = (struct esp_pub *)hw->priv;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29))
	//struct esp_vif *evif = (struct esp_vif *)epub->vif->drv_priv;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter 0x%08x\n", __func__, changed);

        if (changed & (IEEE80211_CONF_CHANGE_CHANNEL | IEEE80211_CONF_CHANGE_IDLE)) {
                sip_send_config(epub, &hw->conf);
    	}
#else
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter 0x%08x\n", __func__, conf->flags);
        sip_send_config(epub, &hw->conf);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29))
	//evif->beacon_interval = conf->beacon_int;
	//init_beacon_timer(epub->vif);
#endif


#if 0
	if (changed & IEEE80211_CONF_CHANGE_PS) {
		struct esp_ps *ps = &epub->ps;

		ps->dtim_period = conf->ps_dtim_period;
		ps->max_sleep_period = conf->max_sleep_period;
		esp_ps_config(epub, ps, (conf->flags & IEEE80211_CONF_PS));
	}
#endif
    return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
static int esp_op_config_interface (struct ieee80211_hw *hw, 
                                    struct ieee80211_vif *vif,
                                    struct ieee80211_if_conf *conf)
{
	// assoc = 2 means AP
	struct esp_pub *epub = (struct esp_pub *)hw->priv;
	struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;
	//struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	ESP_IEEE80211_DBG(ESP_DBG_OP, " %s enter: changed %x, bssid %pM,vif->type = %d\n", __func__, conf->changed, conf->bssid,vif->type);

	if(conf->bssid)
		memcpy(epub->wl.bssid, conf->bssid, ETH_ALEN);
	else
		memset(epub->wl.bssid, 0, ETH_ALEN);

	if(vif->type == NL80211_IFTYPE_AP){
		if((conf->changed & IEEE80211_IFCC_BEACON)){
			sip_send_bss_info_update(epub, evif, (u8*)conf->bssid, 2);
			//evif->beacon_interval = conf->beacon_int;
		}
		else{
			ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s op----1-- mode unspecified\n", __func__);
		}
	}
	else{
		ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s op----2-- mode unspecified\n", __func__);
	}
	return 0;
}
#endif

static void esp_op_bss_info_changed(struct ieee80211_hw *hw,
                                    struct ieee80211_vif *vif,
                                    struct ieee80211_bss_conf *info,
                                    u32 changed)
{
        struct esp_pub *epub = (struct esp_pub *)hw->priv;
        struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))

    struct sta_info *sta;
    struct esp_node *node;
    struct ieee80211_ht_info *ht_info;   

    u8 addr_0[ETH_ALEN];
    memset(addr_0,0,ETH_ALEN);

    ESP_IEEE80211_DBG(ESP_DBG_OP,"%s enter, changed %x\n",__func__,changed);

    if((changed & BSS_CHANGED_ASSOC) && (memcmp(epub->wl.bssid,addr_0, ETH_ALEN)))
    {

        rcu_read_lock();
        node = esp_get_node_by_addr(epub, epub->wl.bssid );
        sta = sta_info_get(container_of(hw,struct ieee80211_local,hw), epub->wl.bssid);

        ht_info = &sta->ht_info;
        memcpy(node->supp_rates, sta->supp_rates, sizeof(node->supp_rates));
        memcpy(&node->ht_info.cap, &ht_info->cap, sizeof(node->ht_info.cap));
        memcpy(&node->ht_info.ht_supported, &ht_info->ht_supported, sizeof(node->ht_info.ht_supported));
        memcpy(&node->ht_info.ampdu_density, &ht_info->ampdu_density, sizeof(node->ht_info.ampdu_density));
        memcpy(&node->ht_info.ampdu_factor, &ht_info->ampdu_factor, sizeof(node->ht_info.ampdu_factor));
        if(sta->aid == 0)
            memcpy(&node->aid, &info->aid, sizeof(node->aid));
        else
            memcpy(&node->aid, &sta->aid, sizeof(node->aid));
        rcu_read_unlock();

        sip_send_set_sta(epub, evif->index, 1, node, vif, (u8)node->index);
    }
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
#endif

	// ieee80211_bss_conf(include/net/mac80211.h) is included in ieee80211_sub_if_data(net/mac80211/ieee80211_i.h) , does bssid=ieee80211_if_ap's ssid ?
	// in 2.6.27, ieee80211_sub_if_data has ieee80211_bss_conf while in 2.6.32 ieee80211_sub_if_data don't have ieee80211_bss_conf
	// in 2.6.27, ieee80211_bss_conf->enable_beacon don't exist, does it mean it support beacon always?
	// ESP_IEEE80211_DBG(ESP_DBG_OP, " %s enter: vif addr %pM, changed %x, assoc %x, bssid %pM\n", __func__, vif->addr, changed, info->assoc, info->bssid);
	// sdata->u.sta.bssid

        ESP_IEEE80211_DBG(ESP_DBG_OP, " %s enter: changed %x, assoc %x, bssid %pM\n", __func__, changed, info->assoc, info->bssid);

        if (vif->type == NL80211_IFTYPE_STATION) {
		if ((changed & BSS_CHANGED_BSSID) ||
				((changed & BSS_CHANGED_ASSOC) && (info->assoc)))
		{
			ESP_IEEE80211_DBG(ESP_DBG_TRACE, " %s STA change bssid or assoc\n", __func__);
			evif->beacon_interval = info->aid;
			memcpy(epub->wl.bssid, (u8*)info->bssid, ETH_ALEN);
			sip_send_bss_info_update(epub, evif, (u8*)info->bssid, info->assoc);
		} else if ((changed & BSS_CHANGED_ASSOC) && (!info->assoc)) {
			ESP_IEEE80211_DBG(ESP_DBG_TRACE, " %s STA change disassoc\n", __func__);
			evif->beacon_interval = 0;
			memset(epub->wl.bssid, 0, ETH_ALEN);
			sip_send_bss_info_update(epub, evif, (u8*)info->bssid, info->assoc);
		} else {
			ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s wrong mode of STA mode\n", __func__);
		}
	} else if (vif->type == NL80211_IFTYPE_AP) {
		if ((changed & BSS_CHANGED_BEACON_ENABLED) ||
				(changed & BSS_CHANGED_BEACON_INT)) {
			ESP_IEEE80211_DBG(ESP_DBG_TRACE, " %s AP change enable %d, interval is %d, bssid %pM\n", __func__, info->enable_beacon, info->beacon_int, info->bssid);
			if (info->enable_beacon && evif->ap_up != true) {
				evif->beacon_interval = info->beacon_int;
				init_beacon_timer(vif);
				esp_sta_gc_conn_monitor_open(epub, evif);
				sip_send_bss_info_update(epub, evif, (u8*)info->bssid, 2);
				evif->ap_up = true;
			} else if (!info->enable_beacon && evif->ap_up &&
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
                    !test_bit(SDATA_STATE_OFFCHANNEL, &sdata->state)
#else
                    true
#endif
                    ) {
				ESP_IEEE80211_DBG(ESP_DBG_TRACE, " %s AP disable beacon, interval is %d\n", __func__, info->beacon_int);
				evif->beacon_interval = 0;
				del_timer_sync(&evif->beacon_timer);
				esp_sta_gc_conn_monitor_close(epub, evif);
				sip_send_bss_info_update(epub, evif, (u8*)info->bssid, 2);
				evif->ap_up = false;
			}
		}
	} else {
		ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s op mode unspecified\n", __func__);
	}
#endif
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
static u64 esp_op_prepare_multicast(struct ieee80211_hw *hw,
                                    int mc_count, struct dev_addr_list *mc_list)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        return 0;
}
#else
static u64 esp_op_prepare_multicast(struct ieee80211_hw *hw,
                                    struct netdev_hw_addr_list *mc_list)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        return 0;
}

#endif /* NEW_KERNEL && KERNEL_35 */
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
static void esp_op_configure_filter(struct ieee80211_hw *hw,
                                    unsigned int changed_flags,
                                    unsigned int *total_flags,
                                    u64 multicast)
#else
static void esp_op_configure_filter(struct ieee80211_hw *hw,
                                    unsigned int changed_flags,
                                    unsigned int *total_flags,
                                    int mc_count,
                                    struct dev_addr_list *mc_list)
#endif
{
        struct esp_pub *epub = (struct esp_pub *)hw->priv;

        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        epub->rx_filter = 0;

        if (*total_flags & FIF_PROMISC_IN_BSS)
                epub->rx_filter |= FIF_PROMISC_IN_BSS;

        if (*total_flags & FIF_ALLMULTI)
                epub->rx_filter |= FIF_ALLMULTI;

        *total_flags = epub->rx_filter;
}

#if 0
static int esp_op_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
                          bool set)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        return 0;
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
static int esp_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
                          struct ieee80211_vif *vif, struct ieee80211_sta *sta,
                          struct ieee80211_key_conf *key)
#else
static int esp_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
                          const u8 *local_address,const u8 *address,
                          struct ieee80211_key_conf *key)
#endif
{
        u8 i;
        int  ret;
        struct esp_pub *epub = (struct esp_pub *)hw->priv;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
        struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;
        u8 ifidx = evif->index;
#else
        u8 ifidx = getaddr_index((u8 *)(local_address), epub); 
#endif
        u8 *peer_addr,isvalid;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
        ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, flags = %x keyindx = %x cmd = %x mac = %pM cipher = %x\n", __func__, key->flags, key->keyidx, cmd, vif->addr, key->cipher);
#else
        ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, flags = %x keyindx = %x cmd = %x cipher = %x\n", __func__, key->flags, key->keyidx, cmd, key->alg);
#endif

        key->flags= key->flags|IEEE80211_KEY_FLAG_GENERATE_IV;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
        if (sta) {
                if (memcmp(sta->addr, epub->wl.bssid, ETH_ALEN))
                        peer_addr = sta->addr;
                else
                        peer_addr = epub->wl.bssid;
        } else {
                peer_addr=epub->wl.bssid;
        }
#else
        peer_addr = (u8 *)address;
#endif
        isvalid = (cmd==SET_KEY) ? 1 : 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
        if ((key->flags&IEEE80211_KEY_FLAG_PAIRWISE) || (key->cipher == WLAN_CIPHER_SUITE_WEP40 || key->cipher == WLAN_CIPHER_SUITE_WEP104))
#else
        if ((key->flags&IEEE80211_KEY_FLAG_PAIRWISE) || (key->alg == ALG_WEP))
#endif
	    {
		if (isvalid) {
			for (i = 0; i < 19; i++) {
				if (epub->hi_map[i].flag == 0) {
					epub->hi_map[i].flag = 1;
					key->hw_key_idx = i + 6;
					memcpy(epub->hi_map[i].mac, peer_addr, ETH_ALEN);
					break;
				}
			}
		} else {
			u8 index = key->hw_key_idx - 6;
			epub->hi_map[index].flag = 0;
			memset(epub->hi_map[index].mac, 0, ETH_ALEN);
		}
        } else {
		if(isvalid){
			for(i = 0; i < 2; i++)
				if (epub->low_map[ifidx][i].flag == 0) {
					epub->low_map[ifidx][i].flag = 1;
                                        key->hw_key_idx = i + ifidx * 2 + 2;
                                        memcpy(epub->low_map[ifidx][i].mac, peer_addr, ETH_ALEN);
                                        break;
                                }
		} else {
			u8 index = key->hw_key_idx - 2 - ifidx * 2;
				epub->low_map[ifidx][index].flag = 0;
				memset(epub->low_map[ifidx][index].mac, 0, ETH_ALEN);
		}
        	//key->hw_key_idx = key->keyidx + ifidx * 2 + 1;
        }

        if (key->hw_key_idx >= 6) {
		/*send sub_scan task to target*/
		//epub->wl.ptk = (cmd==SET_KEY) ? key : NULL;
		if(isvalid)
			atomic_inc(&epub->wl.ptk_cnt);
		else
			atomic_dec(&epub->wl.ptk_cnt);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
        	if (key->cipher == WLAN_CIPHER_SUITE_WEP40 || key->cipher == WLAN_CIPHER_SUITE_WEP104)
#else
        	if (key->alg == ALG_WEP)
#endif
		{
			if(isvalid)
				atomic_inc(&epub->wl.gtk_cnt);
			else
				atomic_dec(&epub->wl.gtk_cnt);
		}
        } else {
        	/*send sub_scan task to target*/
		if(isvalid)
			atomic_inc(&epub->wl.gtk_cnt);
		else
			atomic_dec(&epub->wl.gtk_cnt);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
		if((key->cipher == WLAN_CIPHER_SUITE_WEP40 || key->cipher == WLAN_CIPHER_SUITE_WEP104))
#else
        if((key->alg == ALG_WEP))
#endif
		{
			if(isvalid)
				atomic_inc(&epub->wl.ptk_cnt);
			else
				atomic_dec(&epub->wl.ptk_cnt);
			//epub->wl.ptk = (cmd==SET_KEY) ? key : NULL;
		}
        }

        ret = sip_send_setkey(epub, ifidx, peer_addr, key, isvalid);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	if((key->cipher == WLAN_CIPHER_SUITE_TKIP || key->cipher == WLAN_CIPHER_SUITE_TKIP))
#else
	if((key->alg == ALG_TKIP))
#endif
	{
		if(ret == 0)
			atomic_set(&epub->wl.tkip_key_set, 1);
	}

	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s exit\n", __func__);
        return ret;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
static void esp_op_update_tkip_key(struct ieee80211_hw *hw,
                                   struct ieee80211_key_conf *conf, const u8 *address,
                                   u32 iv32, u16 *phase1key)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

}
#else
static void esp_op_update_tkip_key(struct ieee80211_hw *hw,
                                   struct ieee80211_vif *vif,
                                   struct ieee80211_key_conf *conf,
                                   struct ieee80211_sta *sta,
                                   u32 iv32, u16 *phase1key)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

}
#endif /* KERNEL_35 NEW_KERNEL*/


void hw_scan_done(struct esp_pub *epub, bool aborted)
{
        cancel_delayed_work_sync(&epub->scan_timeout_work);

        ESSERT(epub->wl.scan_req != NULL);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
        ieee80211_scan_completed(epub->hw, aborted);
#else
        ieee80211_scan_completed(epub->hw);
#endif
        if (test_and_clear_bit(ESP_WL_FLAG_STOP_TXQ, &epub->wl.flags)) {
                sip_trigger_txq_process(epub->sip);
        }
}

static void hw_scan_timeout_report(struct work_struct *work)
{
        struct esp_pub *epub =
                container_of(work, struct esp_pub, scan_timeout_work.work);
        bool aborted;

        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "eagle hw scan done\n");

        if (test_and_clear_bit(ESP_WL_FLAG_STOP_TXQ, &epub->wl.flags)) {
                sip_trigger_txq_process(epub->sip);
        }
        /*check if normally complete or aborted like timeout/hw error */
        aborted = (epub->wl.scan_req) ? true : false;

        if (aborted==true) {
                epub->wl.scan_req = NULL;
        }

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
        ieee80211_scan_completed(epub->hw, aborted);
#else
        ieee80211_scan_completed(epub->hw);
#endif  
}

#if 0
static void esp_op_sw_scan_start(struct ieee80211_hw *hw)
{}

static void esp_op_sw_scan_complete(struct ieee80211_hw *hw)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);
}
#endif

#if 0
static int esp_op_get_stats(struct ieee80211_hw *hw,
                            struct ieee80211_low_level_stats *stats)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        return 0;
}

static void esp_op_get_tkip_seq(struct ieee80211_hw *hw, u8 hw_key_idx,
                                u32 *iv32, u16 *iv16)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);
}
#endif

static int esp_op_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
static int esp_node_attach(struct ieee80211_hw *hw, u8 ifidx, struct ieee80211_sta *sta)
#else
static int esp_node_attach(struct ieee80211_hw *hw, u8 ifidx, const u8 *addr)
#endif
{
        struct esp_pub *epub = (struct esp_pub *)hw->priv;
        struct esp_node *node;
        u8 tidno;
        struct esp_tx_tid *tid;
	    int i;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
        struct sta_info *info = sta_info_get(container_of(hw,struct ieee80211_local,hw),(u8 *)addr);
        struct ieee80211_ht_info *ht_info = &info->ht_info;
#endif

	spin_lock_bh(&epub->tx_ampdu_lock);

	if(hweight32(epub->enodes_maps[ifidx]) < ESP_PUB_MAX_STA && (i = ffz(epub->enodes_map)) < ESP_PUB_MAX_STA + 1){
		epub->enodes_map |= (1 << i);
		epub->enodes_maps[ifidx] |= (1 << i);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
		node = (struct esp_node *)sta->drv_priv;
		epub->enodes[i] = node;
		node->sta = sta;
#else
		node = &epub->nodes[i];
		epub->enodes[i] = node;
		memcpy(node->addr, addr, ETH_ALEN);
		memcpy(&node->aid, &info->aid, sizeof(node->aid)); 
		memcpy(node->supp_rates, info->supp_rates, sizeof(node->supp_rates));
		memcpy(&node->ht_info.cap, &ht_info->cap, sizeof(node->ht_info.cap));
		memcpy(&node->ht_info.ht_supported, &ht_info->ht_supported, sizeof(node->ht_info.ht_supported));
		memcpy(&node->ht_info.ampdu_factor, &ht_info->ampdu_factor, sizeof(node->ht_info.ampdu_factor));
		memcpy(&node->ht_info.ampdu_density, &ht_info->ampdu_density, sizeof(node->ht_info.ampdu_density));
#endif
		node->ifidx = ifidx;
		node->index = i;
		atomic_set(&node->loss_count, 0);
		atomic_set(&node->time_remain, ESP_ND_TIME_REMAIN_MAX);
		atomic_set(&node->sta_state, ESP_STA_STATE_NORM);

		for(tidno = 0, tid = &node->tid[tidno]; tidno < WME_NUM_TID; tidno++) {
                tid->ssn = 0;
                tid->cnt = 0;
                tid->state = ESP_TID_STATE_INIT;
        }


	} else {
		i = -1;
	}

	spin_unlock_bh(&epub->tx_ampdu_lock);
	return i;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
static int esp_node_detach(struct ieee80211_hw *hw, u8 ifidx, struct ieee80211_sta *sta)
#else
static int esp_node_detach(struct ieee80211_hw *hw, u8 ifidx, const u8 *addr)
#endif
{
    struct esp_pub *epub = (struct esp_pub *)hw->priv;
	u32 map;
	int i;
    struct esp_node *node = NULL;

	spin_lock_bh(&epub->tx_ampdu_lock);
	map = epub->enodes_maps[ifidx];
	while(map != 0){
		i = ffs(map) - 1;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
		if(epub->enodes[i]->sta == sta){
			epub->enodes[i]->sta = NULL;
#else
		if(memcmp(epub->enodes[i]->addr, addr, ETH_ALEN) == 0){
#endif
			node = epub->enodes[i];
			epub->enodes[i] = NULL;
			epub->enodes_map &= ~(1 << i);
			epub->enodes_maps[ifidx] &= ~(1 << i);
			
			spin_unlock_bh(&epub->tx_ampdu_lock);
			return i;
		}
		map &= ~(1 << i);
	}

	spin_unlock_bh(&epub->tx_ampdu_lock);
	return -1;
}

struct esp_node * esp_get_node_by_addr(struct esp_pub * epub, const u8 *addr)
{
	int i;
	u32 map;
	struct esp_node *node = NULL;
	if(addr == NULL)
		return NULL;
	spin_lock_bh(&epub->tx_ampdu_lock);
	map = epub->enodes_map;
	while(map != 0){
		i = ffs(map) - 1;
		if(i < 0){
			spin_unlock_bh(&epub->tx_ampdu_lock);
			return NULL;
		}
		map &= ~(1 << i);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
		if(memcmp(epub->enodes[i]->sta->addr, addr, ETH_ALEN) == 0)
#else
		if(memcmp(epub->enodes[i]->addr, addr, ETH_ALEN) == 0)
#endif
		{
			node = epub->enodes[i];
			break;
		}
	}

	spin_unlock_bh(&epub->tx_ampdu_lock);
	return node;
}

struct esp_node * esp_get_node_by_index(struct esp_pub * epub, u8 index)
{
	u32 map;
	struct esp_node *node = NULL;

	if (epub == NULL)
		return NULL;

	spin_lock_bh(&epub->tx_ampdu_lock);
	map = epub->enodes_map;
	if (map & BIT(index)) {
		node = epub->enodes[index];
	} else {
		spin_unlock_bh(&epub->tx_ampdu_lock);
		return NULL;
	}

	spin_unlock_bh(&epub->tx_ampdu_lock);
	return node;
}

int esp_get_empty_rxampdu(struct esp_pub * epub, const u8 *addr, u8 tid)
{
	int index = -1;
	if(addr == NULL)
		return index;
	spin_lock_bh(&epub->rx_ampdu_lock);
	if((index = ffz(epub->rxampdu_map)) < ESP_PUB_MAX_RXAMPDU){
		epub->rxampdu_map |= BIT(index);
		epub->rxampdu_node[index] = esp_get_node_by_addr(epub, addr);
		epub->rxampdu_tid[index] = tid;
	} else {
		index = -1;
	}
	spin_unlock_bh(&epub->rx_ampdu_lock);
	return index;
}

int esp_get_exist_rxampdu(struct esp_pub * epub, const u8 *addr, u8 tid)
{	
	u8 map;
	int index = -1;
	int i;
	if(addr == NULL)
		return index;
	spin_lock_bh(&epub->rx_ampdu_lock);
	map = epub->rxampdu_map;
	while(map != 0){
		i = ffs(map) - 1;
		if(i < 0){
			spin_unlock_bh(&epub->rx_ampdu_lock);
			return index;
		}
		map &= ~ BIT(i);
		if(epub->rxampdu_tid[i] == tid && 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
			memcmp(epub->rxampdu_node[i]->sta->addr, addr, ETH_ALEN) == 0
#else
			memcmp(epub->rxampdu_node[i]->addr, addr, ETH_ALEN) == 0
#endif
		){
			index = i;
			break;
		}
	}

	epub->rxampdu_map &= ~ BIT(index);
	spin_unlock_bh(&epub->rx_ampdu_lock);
	return index;

}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
static int esp_op_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta)
#else
static int esp_op_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif, const u8 *addr)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
	struct esp_pub *epub = (struct esp_pub *)hw->priv;
#endif
	struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;
	int index;

	if (vif->type == NL80211_IFTYPE_STATION)
		reset_signal_count();

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, addr %pM\n", __func__, addr);
   	index = esp_node_attach(hw, evif->index, addr);

#else
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, sta addr %pM\n", __func__, sta->addr);
#else 
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, vif addr %pM, sta addr %pM\n", __func__, vif->addr, sta->addr);
#endif
	index = esp_node_attach(hw, evif->index, sta);
#endif

	if(index < 0)
		return -1;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
	sip_send_set_sta(epub, evif->index, 1, sta, vif, (u8)index);
#else
	//node = esp_get_node_by_addr(epub, addr);
	//sip_send_set_sta(epub, evif->index, 1, node, vif, (u8)index);
#endif
    return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
static int esp_op_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta)
#else
static int esp_op_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif, const u8 *addr)
#endif
{	
	struct esp_pub *epub = (struct esp_pub *)hw->priv;
	struct esp_vif *evif = (struct esp_vif *)vif->drv_priv;
	int index;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
	struct esp_node *node;
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, addr %pM\n", __func__, addr);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, sta addr %pM\n", __func__, sta->addr);
#else 
	ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, vif addr %pM, sta addr %pM\n", __func__, vif->addr, sta->addr);
#endif
	
    	//remove a connect in target
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
	index = esp_node_detach(hw, evif->index, sta);
	sip_send_set_sta(epub, evif->index, 0, sta, vif, (u8)index);
#else
	node = esp_get_node_by_addr(epub, addr);
	index = esp_node_detach(hw, evif->index, addr);
	sip_send_set_sta(epub, evif->index, 0, node, vif, node->index);
#endif

	return 0;
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
static void esp_op_sta_notify(struct ieee80211_hw *hw, struct ieee80211_vif *vif, enum sta_notify_cmd cmd, struct ieee80211_sta *sta)
#else
static void esp_op_sta_notify(struct ieee80211_hw *hw, struct ieee80211_vif *vif, enum sta_notify_cmd cmd, const u8 *addr)
#endif
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
        struct esp_pub *epub = (struct esp_pub *)hw->priv;
#endif

        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        switch (cmd) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
        case STA_NOTIFY_ADD:
            ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s cmd add\n", __func__);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
            esp_op_sta_add(hw, vif, sta);
#else
            memcpy(epub->wl.bssid, addr, ETH_ALEN);
            esp_op_sta_add(hw, vif, addr);
#endif
            break;

        case STA_NOTIFY_REMOVE:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
            esp_op_sta_remove(hw, vif, sta);
#else
            esp_op_sta_remove(hw, vif, addr);
            memset(epub->wl.bssid, 0, ETH_ALEN);
#endif
            break;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
        case STA_NOTIFY_SLEEP:
                break;

        case STA_NOTIFY_AWAKE:
                break;
#endif /* NEW_KERNEL */

        default:
                break;
        }
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
static int esp_op_conf_tx(struct ieee80211_hw *hw, 
			  struct ieee80211_vif *vif,
			  u16 queue,
                          const struct ieee80211_tx_queue_params *params)

#else
static int esp_op_conf_tx(struct ieee80211_hw *hw, u16 queue,
                          const struct ieee80211_tx_queue_params *params)
#endif
{
        struct esp_pub *epub = (struct esp_pub *)hw->priv;
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);
        return sip_send_wmm_params(epub, queue, params);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
static int esp_op_get_tx_stats(struct ieee80211_hw *hw,
                               struct ieee80211_tx_queue_stats *stats)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        return 0;
}
#endif /* !NEW_KERNEL && !KERNEL_35*/

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
static u64 esp_op_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
#else
static u64 esp_op_get_tsf(struct ieee80211_hw *hw)
#endif
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
static void esp_op_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u64 tsf)
#else
static void esp_op_set_tsf(struct ieee80211_hw *hw, u64 tsf)
#endif
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
static void esp_op_reset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
#else
static void esp_op_reset_tsf(struct ieee80211_hw *hw)
#endif
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
static void esp_op_rfkill_poll(struct ieee80211_hw *hw)
{
        struct esp_pub *epub = (struct esp_pub *)hw->priv;

        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        wiphy_rfkill_set_hw_state(hw->wiphy,
                                  test_bit(ESP_WL_FLAG_RFKILL, &epub->wl.flags) ? true : false);
}
#endif

#ifdef HW_SCAN
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
static int esp_op_hw_scan(struct ieee80211_hw *hw,
                          struct cfg80211_scan_request *req)
#else
static int esp_op_hw_scan(struct ieee80211_hw *hw,
                          struct ieee80211_vif *vif,
                          struct cfg80211_scan_request *req)
#endif /* NEW_KERNEL && KERNEL_35 */
{
        struct esp_pub *epub = (struct esp_pub *)hw->priv;
        int i, ret;
        bool scan_often = true;

        ESP_IEEE80211_DBG(ESP_DBG_OP, "%s\n", __func__);

        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "scan, %d\n", req->n_ssids);
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "scan, len 1:%d,ssid 1:%s\n", req->ssids->ssid_len, req->ssids->ssid_len == 0? "":(char *)req->ssids->ssid);
        if(req->n_ssids > 1)
                ESP_IEEE80211_DBG(ESP_DBG_TRACE, "scan, len 2:%d,ssid 2:%s\n", (req->ssids+1)->ssid_len, (req->ssids+1)->ssid_len == 0? "":(char *)(req->ssids + 1)->ssid);

        /*scan_request is keep allocate untill scan_done,record it
          to split request into multi sdio_cmd*/
	if (atomic_read(&epub->wl.off)) {
		esp_dbg(ESP_DBG_ERROR, "%s scan but wl off \n", __func__);
		return -EPERM;
	}

        if(req->n_ssids > 1){
                struct cfg80211_ssid *ssid2 = req->ssids + 1;
                if((req->ssids->ssid_len > 0 && ssid2->ssid_len > 0) || req->n_ssids > 2){
                        ESP_IEEE80211_DBG(ESP_DBG_ERROR, "scan ssid num: %d, ssid1:%s, ssid2:%s,not support\n", req->n_ssids, 
				        req->ssids->ssid_len == 0 ? "":(char *)req->ssids->ssid, ssid2->ssid_len == 0? "":(char *)ssid2->ssid);
		                return -EINVAL;
		        }
        }

        epub->wl.scan_req = req;

        for (i = 0; i < req->n_channels; i++)
                ESP_IEEE80211_DBG(ESP_DBG_TRACE, "eagle hw_scan freq %d\n",
                                  req->channels[i]->center_freq);
#if 0
        for (i = 0; i < req->n_ssids; i++) {
                if (req->ssids->ssid_len> 0) {
                        req->ssids->ssid[req->ssids->ssid_len]='\0';
                        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "scan_ssid %d:%s\n",
                                          i, req->ssids->ssid);
                }
        }
#endif

        /*in connect state, suspend tx data*/
        if(epub->sip->support_bgscan &&
		test_bit(ESP_WL_FLAG_CONNECT, &epub->wl.flags) &&
		req->n_channels > 0)
	{

                scan_often = epub->scan_permit_valid && time_before(jiffies, epub->scan_permit);
                epub->scan_permit_valid = true;

                if (!scan_often) {
/*                        epub->scan_permit = jiffies + msecs_to_jiffies(900);
                        set_bit(ESP_WL_FLAG_STOP_TXQ, &epub->wl.flags);
                        if (atomic_read(&epub->txq_stopped) == false) {
                                atomic_set(&epub->txq_stopped, true);
                                ieee80211_stop_queues(hw);
                        }
*/
                } else {
                        ESP_IEEE80211_DBG(ESP_DBG_LOG, "scan too often\n");
			return -EACCES;
                }
        } else {
		scan_often = false;
	}

        /*send sub_scan task to target*/
        ret = sip_send_scan(epub);

        if (ret) {
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "fail to send scan_cmd\n");
		return ret;
        } else {
		if(!scan_often) {
			epub->scan_permit = jiffies + msecs_to_jiffies(900);
                        set_bit(ESP_WL_FLAG_STOP_TXQ, &epub->wl.flags);
                        if (atomic_read(&epub->txq_stopped) == false) {
                                atomic_set(&epub->txq_stopped, true);
                                ieee80211_stop_queues(hw);
                        }
			/*force scan complete in case target fail to report in time*/
                	ieee80211_queue_delayed_work(hw, &epub->scan_timeout_work, req->n_channels * HZ / 4);
		}
        }

        return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
static int esp_op_remain_on_channel(struct ieee80211_hw *hw,
                                    struct ieee80211_channel *chan,
                                    enum nl80211_channel_type channel_type,
                                    int duration)
{
      struct esp_pub *epub = (struct esp_pub *)hw->priv;

      ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, center_freq = %d duration = %d\n", __func__, chan->center_freq, duration);
      sip_send_roc(epub, chan->center_freq, duration);
      return 0;
}

static int esp_op_cancel_remain_on_channel(struct ieee80211_hw *hw)
{
      struct esp_pub *epub = (struct esp_pub *)hw->priv;

      ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter \n", __func__);
      epub->roc_flags= 0;  // to disable roc state
      sip_send_roc(epub, 0, 0);
     return 0;
}
#endif /* > 2.6.38 */
#endif

void esp_rocdone_process(struct ieee80211_hw *hw, struct sip_evt_roc *report)
{    
      struct esp_pub *epub = (struct esp_pub *)hw->priv;

      ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter, state = %d is_ok = %d\n", __func__, report->state, report->is_ok);

      //roc process begin 
      if((report->state==1)&&(report->is_ok==1)) 
      {
           epub->roc_flags=1;  //flags in roc state, to fix channel, not change
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
           ieee80211_ready_on_channel(hw);
#endif
      }
      else if ((report->state==0)&&(report->is_ok==1))    //roc process timeout
      {
           epub->roc_flags= 0;  // to disable roc state
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
           ieee80211_remain_on_channel_expired(hw);     
#endif
       }
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39))        
static int esp_op_set_bitrate_mask(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				const struct cfg80211_bitrate_mask *mask)
{
        ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter \n", __func__);
        ESP_IEEE80211_DBG(ESP_DBG_OP, "%s vif->macaddr[%pM], mask[%d]\n", __func__, vif->addr, mask->control[0].legacy);

	return 0;
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))        
void esp_op_flush(struct ieee80211_hw *hw, u32 queues, bool drop)
#else
void esp_op_flush(struct ieee80211_hw *hw, bool drop)
#endif
{
	
        ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter \n", __func__);
	do{
		
		struct esp_pub *epub = (struct esp_pub *)hw->priv;
		unsigned long time = jiffies + msecs_to_jiffies(15);
		while(atomic_read(&epub->sip->tx_data_pkt_queued)){
			if(!time_before(jiffies, time)){
				break;
			}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
            if(sif_get_ate_config() == 0){
                ieee80211_queue_work(epub->hw, &epub->tx_work);
            } else {
                queue_work(epub->esp_wkq, &epub->tx_work);
            }
#else
            queue_work(epub->esp_wkq, &epub->tx_work);
#endif
			//sip_txq_process(epub);
		}
		mdelay(10);
		
	}while(0);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
static int esp_op_ampdu_action(struct ieee80211_hw *hw,
                enum ieee80211_ampdu_mlme_action action,
			    struct ieee80211_sta *sta, u16 tid, u16 *ssn)
#else
static int esp_op_ampdu_action(struct ieee80211_hw *hw,
                enum ieee80211_ampdu_mlme_action action,
			    const u8 *addr, u16 tid, u16 *ssn)
#endif
#else
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39))
static int esp_op_ampdu_action(struct ieee80211_hw *hw,
                struct ieee80211_vif *vif,
                               enum ieee80211_ampdu_mlme_action action,
                               struct ieee80211_sta *sta, u16 tid, u16 *ssn)
#else
static int esp_op_ampdu_action(struct ieee80211_hw *hw,
                               struct ieee80211_vif *vif,
                               enum ieee80211_ampdu_mlme_action action,
                               struct ieee80211_sta *sta, u16 tid, u16 *ssn,
                               u8 buf_size)
#endif
#endif /* NEW_KERNEL && KERNEL_35 */
{
        int ret = -EOPNOTSUPP;
        struct esp_pub *epub = (struct esp_pub *)hw->priv;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
        struct esp_node * node = (struct esp_node *)sta->drv_priv;
#else
        struct esp_node * node = esp_get_node_by_addr(epub, addr);
#endif
        struct esp_tx_tid * tid_info = &node->tid[tid];
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39))
	u8 buf_size = 64;
#endif

        ESP_IEEE80211_DBG(ESP_DBG_OP, "%s enter \n", __func__);
        switch(action) {
        case IEEE80211_AMPDU_TX_START:
                if (mod_support_no_txampdu() ||
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
                	cfg80211_get_chandef_type(&epub->hw->conf.chandef) == NL80211_CHAN_NO_HT
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
                        hw->conf.channel_type == NL80211_CHAN_NO_HT
#else
                        !(hw->conf.flags&IEEE80211_CONF_SUPPORT_HT_MODE)
#endif
			||
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
                        !sta->ht_cap.ht_supported
#else
                        !node->ht_info.ht_supported
#endif
                            )
                        return ret;

		//if (vif->p2p || vif->type != NL80211_IFTYPE_STATION)
		//	return ret;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s TX START, addr:%pM,tid:%u\n", __func__, addr, tid);
#else
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s TX START, addr:%pM,tid:%u,state:%d\n", __func__, sta->addr, tid, tid_info->state);
#endif
                spin_lock_bh(&epub->tx_ampdu_lock);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))                
                ESSERT(tid_info->state == ESP_TID_STATE_TRIGGER);
                *ssn = tid_info->ssn;
                tid_info->state = ESP_TID_STATE_PROGRESS;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
                ieee80211_start_tx_ba_cb_irqsafe(hw, addr, tid);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33))
                ieee80211_start_tx_ba_cb_irqsafe(hw, sta->addr, tid);
#else
                ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
#endif
                spin_unlock_bh(&epub->tx_ampdu_lock);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
                ret = 0;
#else
                spin_lock_bh(&epub->tx_ampdu_lock);
		
                if (tid_info->state != ESP_TID_STATE_PROGRESS) {
                        if (tid_info->state == ESP_TID_STATE_INIT) {
				                printk(KERN_ERR "%s WIFI RESET, IGNORE\n", __func__);
                                spin_unlock_bh(&epub->tx_ampdu_lock);
				                return -ENETRESET;
                        } else {
				                ESSERT(0);
                        }
                }
			
                tid_info->state = ESP_TID_STATE_OPERATIONAL;
                spin_unlock_bh(&epub->tx_ampdu_lock);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
                ret = sip_send_ampdu_action(epub, SIP_AMPDU_TX_OPERATIONAL, sta->addr, tid, node->ifidx, buf_size);
#else
                ret = sip_send_ampdu_action(epub, SIP_AMPDU_TX_OPERATIONAL, addr, tid, node->ifidx, buf_size);
#endif
#endif
                break;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
	case IEEE80211_AMPDU_TX_STOP_CONT:
#else
        case IEEE80211_AMPDU_TX_STOP:
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s TX STOP, addr:%pM,tid:%u\n", __func__, addr, tid);
#else
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s TX STOP, addr:%pM,tid:%u,state:%d\n", __func__, sta->addr, tid, tid_info->state);
#endif
                spin_lock_bh(&epub->tx_ampdu_lock);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
                if(tid_info->state == ESP_TID_STATE_WAIT_STOP)
                        tid_info->state = ESP_TID_STATE_STOP;
                else
                        tid_info->state = ESP_TID_STATE_INIT;
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
                ieee80211_stop_tx_ba_cb_irqsafe(hw, addr, tid);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33))
                ieee80211_stop_tx_ba_cb_irqsafe(hw, sta->addr, tid);
#else
                ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
#endif
                spin_unlock_bh(&epub->tx_ampdu_lock);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
                ret = sip_send_ampdu_action(epub, SIP_AMPDU_TX_STOP, addr, tid, node->ifidx, 0);
#else
                ret = sip_send_ampdu_action(epub, SIP_AMPDU_TX_STOP, sta->addr, tid, node->ifidx, 0);
#endif
                break;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
                if(tid_info->state == ESP_TID_STATE_WAIT_STOP)
                        tid_info->state = ESP_TID_STATE_STOP;
                else
                        tid_info->state = ESP_TID_STATE_INIT;
#endif
                ret = sip_send_ampdu_action(epub, SIP_AMPDU_TX_STOP, sta->addr, tid, node->ifidx, 0);
		        break;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
        case IEEE80211_AMPDU_TX_OPERATIONAL:
#else
        case IEEE80211_AMPDU_TX_RESUME:
#endif
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s TX OPERATION, addr:%pM,tid:%u,state:%d\n", __func__, sta->addr, tid, tid_info->state);
                spin_lock_bh(&epub->tx_ampdu_lock);
		
                if (tid_info->state != ESP_TID_STATE_PROGRESS) {
                        if (tid_info->state == ESP_TID_STATE_INIT) {
				                printk(KERN_ERR "%s WIFI RESET, IGNORE\n", __func__);
                                spin_unlock_bh(&epub->tx_ampdu_lock);
				                return -ENETRESET;
                        } else {
				                ESSERT(0);
                        }
                }
			
                tid_info->state = ESP_TID_STATE_OPERATIONAL;
                spin_unlock_bh(&epub->tx_ampdu_lock);
                ret = sip_send_ampdu_action(epub, SIP_AMPDU_TX_OPERATIONAL, sta->addr, tid, node->ifidx, buf_size);
                break;
#endif
        case IEEE80211_AMPDU_RX_START:
                if(mod_support_no_rxampdu() ||
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
                	cfg80211_get_chandef_type(&epub->hw->conf.chandef) == NL80211_CHAN_NO_HT
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
                        hw->conf.channel_type == NL80211_CHAN_NO_HT
#else
                        !(hw->conf.flags&IEEE80211_CONF_SUPPORT_HT_MODE)
#endif
			||
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
                        !sta->ht_cap.ht_supported
#else
                        !node->ht_info.ht_supported
#endif
                        )
                        return ret;

		if (
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
                (vif->p2p && false)
#else
                false
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33))
                || false
#else
                || (vif->type != NL80211_IFTYPE_STATION && false)
#endif
           )
			return ret;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s RX START %pM tid %u %u\n", __func__, addr, tid, *ssn);
                ret = sip_send_ampdu_action(epub, SIP_AMPDU_RX_START, addr, tid, *ssn, 64);
#else
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s RX START %pM tid %u %u\n", __func__, sta->addr, tid, *ssn);
                ret = sip_send_ampdu_action(epub, SIP_AMPDU_RX_START, sta->addr, tid, *ssn, 64);
#endif
                break;
        case IEEE80211_AMPDU_RX_STOP:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s RX STOP %pM tid %u\n", __func__, addr, tid);
                ret = sip_send_ampdu_action(epub, SIP_AMPDU_RX_STOP, addr, tid, 0, 0);
#else
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "%s RX STOP %pM tid %u\n", __func__, sta->addr, tid);
                ret = sip_send_ampdu_action(epub, SIP_AMPDU_RX_STOP, sta->addr, tid, 0, 0);
#endif
                break;
        default:
                break;
        }
        return ret;
}

#if 0
static int esp_op_tx_last_beacon(struct ieee80211_hw *hw)
{

        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        return 0;
}

#ifdef CONFIG_NL80211_TESTMODE
static int esp_op_testmode_cmd(struct ieee80211_hw *hw, void *data, int len)
{
        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter \n", __func__);

        return 0;
}
#endif /* CONFIG_NL80211_TESTMODE */
#endif

static void
esp_tx_work(struct work_struct *work)
{
        struct esp_pub *epub = container_of(work, struct esp_pub, tx_work);

        mutex_lock(&epub->tx_mtx);
        sip_txq_process(epub);
        mutex_unlock(&epub->tx_mtx);
}

#ifndef RX_SENDUP_SYNC
//for debug
static int data_pkt_dequeue_cnt = 0;
static void _esp_flush_rxq(struct esp_pub *epub)
{
        struct sk_buff *skb = NULL;

        while ((skb = skb_dequeue(&epub->rxq))) {
		//do not log when in spin_lock
                //esp_dbg(ESP_DBG_TRACE, "%s call ieee80211_rx \n", __func__);
                ieee80211_rx(epub->hw, skb);
        }
}

static void
esp_sendup_work(struct work_struct *work)
{
        struct esp_pub *epub = container_of(work, struct esp_pub, sendup_work);
        spin_lock_bh(&epub->rx_lock);
        _esp_flush_rxq(epub);
        spin_unlock_bh(&epub->rx_lock);
}
#endif /* !RX_SENDUP_SYNC */

static const struct ieee80211_ops esp_mac80211_ops = {
        .tx = esp_op_tx,
        .start = esp_op_start,
        .stop = esp_op_stop,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39))        
#ifdef CONFIG_PM
        .suspend = esp_op_suspend,
        .resume = esp_op_resume,
#endif
#endif
        .add_interface = esp_op_add_interface,
        .remove_interface = esp_op_remove_interface,
        .config = esp_op_config,

        .bss_info_changed = esp_op_bss_info_changed,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
        .config_interface = esp_op_config_interface,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
        .prepare_multicast = esp_op_prepare_multicast,
#endif
        .configure_filter = esp_op_configure_filter,
        .set_key = esp_op_set_key,
        .update_tkip_key = esp_op_update_tkip_key,
        //.sched_scan_start = esp_op_sched_scan_start,
        //.sched_scan_stop = esp_op_sched_scan_stop,
        .set_rts_threshold = esp_op_set_rts_threshold,
        .sta_notify = esp_op_sta_notify,
        .conf_tx = esp_op_conf_tx,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
        .get_tx_stats = esp_op_get_tx_stats,
#endif /* KERNEL_VERSION < 2.6.35*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	.change_interface = esp_op_change_interface,
#endif
        .get_tsf = esp_op_get_tsf,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
        .set_tsf = esp_op_set_tsf,
#endif
        .reset_tsf = esp_op_reset_tsf,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
        .rfkill_poll= esp_op_rfkill_poll,
#endif
#ifdef HW_SCAN
        .hw_scan = esp_op_hw_scan,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
        .remain_on_channel= esp_op_remain_on_channel,
        .cancel_remain_on_channel=esp_op_cancel_remain_on_channel,
#endif /* >=2.6.38 */
#endif
        .ampdu_action = esp_op_ampdu_action,
        //.get_survey = esp_op_get_survey,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34))
        .sta_add = esp_op_sta_add,
        .sta_remove = esp_op_sta_remove,
#endif /* >= 2.6.34 */
#ifdef CONFIG_NL80211_TESTMODE
        //CFG80211_TESTMODE_CMD(esp_op_tm_cmd)
#endif
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39))
	.set_bitrate_mask = esp_op_set_bitrate_mask,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34))
	.flush = esp_op_flush,
#endif
};

struct esp_pub * esp_pub_alloc_mac80211(struct device *dev)
{
        struct ieee80211_hw *hw;
        struct esp_pub *epub;
        int ret = 0;

        hw = ieee80211_alloc_hw(sizeof(struct esp_pub), &esp_mac80211_ops);

        if (hw == NULL) {
                esp_dbg(ESP_DBG_ERROR, "ieee80211 can't alloc hw!\n");
                ret = -ENOMEM;
                return ERR_PTR(ret);
        }
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
        hw->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
#endif

        epub = hw->priv;
        memset(epub, 0, sizeof(*epub));
        epub->hw = hw;
        SET_IEEE80211_DEV(hw, dev);
        epub->dev = dev;

        skb_queue_head_init(&epub->txq);
        skb_queue_head_init(&epub->txdoneq);
        skb_queue_head_init(&epub->rxq);

	spin_lock_init(&epub->tx_ampdu_lock);
	spin_lock_init(&epub->rx_ampdu_lock);
        spin_lock_init(&epub->tx_lock);
        mutex_init(&epub->tx_mtx);
        spin_lock_init(&epub->rx_lock);

        INIT_WORK(&epub->tx_work, esp_tx_work);
#ifndef RX_SENDUP_SYNC
        INIT_WORK(&epub->sendup_work, esp_sendup_work);
#endif //!RX_SENDUP_SYNC

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39))
        //epub->esp_wkq = create_freezeable_workqueue("esp_wkq"); 
        epub->esp_wkq = create_singlethread_workqueue("esp_wkq");
#else
        //epub->esp_wkq = create_freezable_workqueue("esp_wkq"); 
        epub->esp_wkq = create_singlethread_workqueue("esp_wkq");
#endif /* NEW_KERNEL */

        if (epub->esp_wkq == NULL) {
                ret = -ENOMEM;
                return ERR_PTR(ret);
        }

	epub->master_ifidx = ESP_PUB_MAX_VIF;

        epub->scan_permit_valid = false;
        INIT_DELAYED_WORK(&epub->scan_timeout_work, hw_scan_timeout_report);

        return epub;
}


int esp_pub_dealloc_mac80211(struct esp_pub *epub)
{
        set_bit(ESP_WL_FLAG_RFKILL, &epub->wl.flags);

        destroy_workqueue(epub->esp_wkq);
        mutex_destroy(&epub->tx_mtx);

#ifdef ESP_NO_MAC80211
        free_netdev(epub->net_dev);
        wiphy_free(epub->wdev->wiphy);
        kfree(epub->wdev);
#else
        if (epub->hw) {
                ieee80211_free_hw(epub->hw);
        }
#endif

        return 0;
}

#if 0
static int esp_reg_notifier(struct wiphy *wiphy,
                            struct regulatory_request *request)
{
        struct ieee80211_supported_band *sband;
        struct ieee80211_channel *ch;
        int i;

        ESP_IEEE80211_DBG(ESP_DBG_TRACE, "%s enter %d\n", __func__, request->initiator
                         );

        //TBD
}
#endif

/* 2G band channels */
static struct ieee80211_channel esp_channels_2ghz[] = {
        { .hw_value = 1, .center_freq = 2412, .max_power = 25 },
        { .hw_value = 2, .center_freq = 2417, .max_power = 25 },
        { .hw_value = 3, .center_freq = 2422, .max_power = 25 },
        { .hw_value = 4, .center_freq = 2427, .max_power = 25 },
        { .hw_value = 5, .center_freq = 2432, .max_power = 25 },
        { .hw_value = 6, .center_freq = 2437, .max_power = 25 },
        { .hw_value = 7, .center_freq = 2442, .max_power = 25 },
        { .hw_value = 8, .center_freq = 2447, .max_power = 25 },
        { .hw_value = 9, .center_freq = 2452, .max_power = 25 },
        { .hw_value = 10, .center_freq = 2457, .max_power = 25 },
        { .hw_value = 11, .center_freq = 2462, .max_power = 25 },
        { .hw_value = 12, .center_freq = 2467, .max_power = 25 },
        { .hw_value = 13, .center_freq = 2472, .max_power = 25 },
        //{ .hw_value = 14, .center_freq = 2484, .max_power = 25 },
};

/* 11G rate */
static struct ieee80211_rate esp_rates_2ghz[] = {
        {
                .bitrate = 10,
                .hw_value = CONF_HW_BIT_RATE_1MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_1MBPS,
        },
        {
                .bitrate = 20,
                .hw_value = CONF_HW_BIT_RATE_2MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_2MBPS,
                .flags = IEEE80211_RATE_SHORT_PREAMBLE
        },
        {
                .bitrate = 55,
                .hw_value = CONF_HW_BIT_RATE_5_5MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_5_5MBPS,
                .flags = IEEE80211_RATE_SHORT_PREAMBLE
        },
        {
                .bitrate = 110,
                .hw_value = CONF_HW_BIT_RATE_11MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_11MBPS,
                .flags = IEEE80211_RATE_SHORT_PREAMBLE
        },
        {
                .bitrate = 60,
                .hw_value = CONF_HW_BIT_RATE_6MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_6MBPS,
        },
        {
                .bitrate = 90,
                .hw_value = CONF_HW_BIT_RATE_9MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_9MBPS,
        },
        {
                .bitrate = 120,
                .hw_value = CONF_HW_BIT_RATE_12MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_12MBPS,
        },
        {
                .bitrate = 180,
                .hw_value = CONF_HW_BIT_RATE_18MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_18MBPS,
        },
        {
                .bitrate = 240,
                .hw_value = CONF_HW_BIT_RATE_24MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_24MBPS,
        },
        {
                .bitrate = 360,
                .hw_value = CONF_HW_BIT_RATE_36MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_36MBPS,
        },
        {
                .bitrate = 480,
                .hw_value = CONF_HW_BIT_RATE_48MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_48MBPS,
        },
        {
                .bitrate = 540,
                .hw_value = CONF_HW_BIT_RATE_54MBPS,
                .hw_value_short = CONF_HW_BIT_RATE_54MBPS,
        },
};

static void
esp_pub_init_mac80211(struct esp_pub *epub)
{
        struct ieee80211_hw *hw = epub->hw;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
        static const u32 cipher_suites[] = {
                WLAN_CIPHER_SUITE_WEP40,
                WLAN_CIPHER_SUITE_WEP104,
                WLAN_CIPHER_SUITE_TKIP,
                WLAN_CIPHER_SUITE_CCMP,
        };
#endif

        hw->channel_change_time = 420000; /* in us */
        hw->max_listen_interval = 10;

        hw->flags = IEEE80211_HW_SIGNAL_DBM |
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33))
                    IEEE80211_HW_HAS_RATE_CONTROL |
#endif /* >= 2.6.33 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
                    IEEE80211_HW_MFP_CAPABLE |
                    IEEE80211_HW_SUPPORTS_PS |
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
                IEEE80211_HW_AMPDU_AGGREGATION |
#endif
				IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING;
   //IEEE80211_HW_PS_NULLFUNC_STACK |	
        //IEEE80211_HW_CONNECTION_MONITOR |
        //IEEE80211_HW_BEACON_FILTER |
        //IEEE80211_HW_AMPDU_AGGREGATION |
        //IEEE80211_HW_REPORTS_TX_ACK_STATUS;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
        hw->max_rx_aggregation_subframes = 0x40;
        hw->max_tx_aggregation_subframes = 0x40;
#endif /* >= 2.6.39 */
        
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
        hw->wiphy->cipher_suites = cipher_suites;
        hw->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);
        hw->wiphy->max_scan_ie_len = epub->sip->tx_blksz - sizeof(struct sip_hdr) - sizeof(struct sip_cmd_scan);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
        /* ONLY station for now, support P2P soon... */
        hw->wiphy->interface_modes = 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
            BIT(NL80211_IFTYPE_P2P_GO) |
		    BIT(NL80211_IFTYPE_P2P_CLIENT) |
#endif
            BIT(NL80211_IFTYPE_STATION) |
		    BIT(NL80211_IFTYPE_AP);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
        hw->wiphy->max_scan_ssids = 2;
        //hw->wiphy->max_sched_scan_ssids = 16;
        //hw->wiphy->max_match_sets = 16;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
		hw->wiphy->max_remain_on_channel_duration = 5000;
#endif

	atomic_set(&epub->wl.off, 1);

        epub->wl.sbands[IEEE80211_BAND_2GHZ].band = IEEE80211_BAND_2GHZ;
        epub->wl.sbands[IEEE80211_BAND_2GHZ].channels = esp_channels_2ghz;
        epub->wl.sbands[IEEE80211_BAND_2GHZ].bitrates = esp_rates_2ghz;
        epub->wl.sbands[IEEE80211_BAND_2GHZ].n_channels = ARRAY_SIZE(esp_channels_2ghz);
        epub->wl.sbands[IEEE80211_BAND_2GHZ].n_bitrates = ARRAY_SIZE(esp_rates_2ghz);
        /*add to support 11n*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.ht_supported = true;
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.cap = 0x112C;//IEEE80211_HT_CAP_RX_STBC; //IEEE80211_HT_CAP_SGI_20;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_16K;
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE;
#else
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.ampdu_factor = 1;//IEEE80211_HT_MAX_AMPDU_16K;
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.ampdu_density = 0;//IEEE80211_HT_MPDU_DENSITY_NONE;
#endif
        memset(&epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.mcs, 0,
               sizeof(epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.mcs));
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.mcs.rx_mask[0] = 0xff;
        //epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.mcs.rx_highest = 7;
        //epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
#else
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_info.ht_supported = true;
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_info.cap = 0x112C;//IEEE80211_HT_CAP_RX_STBC; //IEEE80211_HT_CAP_SGI_20;
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_info.ampdu_factor = 1;//IEEE80211_HT_MAX_AMPDU_16K;
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_info.ampdu_density = 0;//IEEE80211_HT_MPDU_DENSITY_NONE;
        memset(&epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_info.supp_mcs_set, 0,
               sizeof(epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_info.supp_mcs_set));
        epub->wl.sbands[IEEE80211_BAND_2GHZ].ht_info.supp_mcs_set[0] = 0xff;
#endif


        /* BAND_5GHZ TBD */

        hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
                &epub->wl.sbands[IEEE80211_BAND_2GHZ];
        /* BAND_5GHZ TBD */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
        /*no fragment*/
        hw->wiphy->frag_threshold = IEEE80211_MAX_FRAG_THRESHOLD;
#endif

        /* handle AC queue in f/w */
        hw->queues = 4;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30))
        hw->max_rates = 4;
#else
        hw->max_altrates = 4;
#endif
#endif
        //hw->wiphy->reg_notifier = esp_reg_notify;

        hw->vif_data_size = sizeof(struct esp_vif);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
        hw->sta_data_size = sizeof(struct esp_node);
#endif

        //hw->max_rx_aggregation_subframes = 8;
}

int
esp_register_mac80211(struct esp_pub *epub)
{
        int ret = 0;
	u8 mac[ETH_ALEN];
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)) && defined(P2P_CONCURRENT)
	u8 *wlan_addr;
	u8 *p2p_addr;
	int idx;
#endif

        esp_pub_init_mac80211(epub);

	printk("Wifi Efuse Mac => %02x:%02x:%02x:%02x:%02x:%02x\n", epub->mac_addr[0], epub->mac_addr[1],
          epub->mac_addr[2], epub->mac_addr[3], epub->mac_addr[4], epub->mac_addr[5]);
	
	if (!rockchip_wifi_mac_addr(mac)) 
	{
		printk("=========> get mac address from flash=[%02x:%02x:%02x:%02x:%02x:%02x]\n", mac[0], mac[1],
                mac[2], mac[3], mac[4], mac[5]);
		memcpy(epub->mac_addr, mac, ETH_ALEN);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)) && defined(P2P_CONCURRENT)
	epub->hw->wiphy->addresses = (struct mac_address *)esp_mac_addr;
	memcpy(&epub->hw->wiphy->addresses[0], epub->mac_addr, ETH_ALEN);
	memcpy(&epub->hw->wiphy->addresses[1], epub->mac_addr, ETH_ALEN);
	wlan_addr = (u8 *)&epub->hw->wiphy->addresses[0];
	p2p_addr  = (u8 *)&epub->hw->wiphy->addresses[1];

	for (idx = 0; idx < 64; idx++) {
                p2p_addr[0] = wlan_addr[0] | 0x02;
                p2p_addr[0] ^= idx << 2;
                if (memcmp(p2p_addr, wlan_addr, ETH_ALEN) != 0)
                        break;
        }

	epub->hw->wiphy->n_addresses = 2;
#else

        SET_IEEE80211_PERM_ADDR(epub->hw, epub->mac_addr);
#endif

        ret = ieee80211_register_hw(epub->hw);

        if (ret < 0) {
                ESP_IEEE80211_DBG(ESP_DBG_ERROR, "unable to register mac80211 hw: %d\n", ret);
                return ret;
        } else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
#ifdef MAC80211_NO_CHANGE
        	rtnl_lock();
		if (epub->hw->wiphy->interface_modes &
                (BIT(NL80211_IFTYPE_P2P_GO) | BIT(NL80211_IFTYPE_P2P_CLIENT))) {
                ret = ieee80211_if_add(hw_to_local(epub->hw), "p2p%d", NULL,
                                          NL80211_IFTYPE_STATION, NULL);
                if (ret)
                        wiphy_warn(epub->hw->wiphy,
                                   "Failed to add default virtual iface\n");
        	}

        	rtnl_unlock();
#endif
#endif
	}

        set_bit(ESP_WL_FLAG_HW_REGISTERED, &epub->wl.flags);

        return ret;
}

static u8 getaddr_index(u8 * addr, struct esp_pub *epub)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)) && defined(P2P_CONCURRENT)
	int i;
	for(i = 0; i < ESP_PUB_MAX_VIF; i++)
		if(memcmp(addr, (u8 *)&epub->hw->wiphy->addresses[i], ETH_ALEN) == 0)
                	return i;
	return ESP_PUB_MAX_VIF;
#else
	return 0;
#endif
}

static int dup_addr_by_index(u8 *addr, struct esp_pub *epub, u8 index)
{
	if (addr == NULL)
		return -EINVAL;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)) && defined(P2P_CONCURRENT)
	if (index >= ESP_PUB_MAX_VIF || index < 0) {
		memcpy(addr, epub->mac_addr, ETH_ALEN);
		return -ERANGE;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	memcpy(addr, &epub->hw->wiphy->addresses[index], ETH_ALEN);
#else
	memcpy(addr, epub->mac_addr, ETH_ALEN);
#endif /* kernel version */
#else
	memcpy(addr, epub->mac_addr, ETH_ALEN);
#endif
	return 0;
}

