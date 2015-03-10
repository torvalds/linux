/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 
 * SIP ctrl packet parse and pack
 */

#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/firmware.h>

#include "esp_pub.h"
#include "esp_sip.h"
#include "esp_ctrl.h"
#include "esp_sif.h"
#include "esp_debug.h"
#include "esp_wmac.h"
#include "esp_utils.h"
#include "esp_wl.h"
#include "esp_file.h"
#include "esp_path.h"
#ifdef TEST_MODE
#include "testmode.h"
#endif /* TEST_MODE */
#include "esp_version.h"

extern struct completion *gl_bootup_cplx; 

static void esp_tx_ba_session_op(struct esp_sip *sip, struct esp_node *node, trc_ampdu_state_t state, u8 tid )
{
        struct esp_tx_tid *txtid;

        txtid = &node->tid[tid];
        if (state == TRC_TX_AMPDU_STOPPED) {
                if (txtid->state == ESP_TID_STATE_OPERATIONAL) {
                        esp_dbg(ESP_DBG_TXAMPDU, "%s tid %d TXAMPDU GOT STOP EVT\n", __func__, tid);

                        spin_lock_bh(&sip->epub->tx_ampdu_lock);
                        txtid->state = ESP_TID_STATE_WAIT_STOP;
                        spin_unlock_bh(&sip->epub->tx_ampdu_lock);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
                        ieee80211_stop_tx_ba_session(sip->epub->hw, node->addr, (u16)tid, WLAN_BACK_INITIATOR);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32))
                        ieee80211_stop_tx_ba_session(sip->epub->hw, node->sta->addr, (u16)tid, WLAN_BACK_INITIATOR);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
                        ieee80211_stop_tx_ba_session(node->sta, (u16)tid, WLAN_BACK_INITIATOR);
#else
                        ieee80211_stop_tx_ba_session(node->sta, (u16)tid);
#endif /* KERNEL_VERSION 2.6.39 */
                } else {
                        esp_dbg(ESP_DBG_TXAMPDU, "%s tid %d TXAMPDU GOT STOP EVT IN WRONG STATE %d\n", __func__, tid, txtid->state);
                }
        } else if (state == TRC_TX_AMPDU_OPERATIONAL) {
                if (txtid->state == ESP_TID_STATE_STOP) {
                        esp_dbg(ESP_DBG_TXAMPDU, "%s tid %d TXAMPDU GOT OPERATIONAL\n", __func__, tid);

                        spin_lock_bh(&sip->epub->tx_ampdu_lock);
                        txtid->state = ESP_TID_STATE_TRIGGER;
                        spin_unlock_bh(&sip->epub->tx_ampdu_lock);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
                        ieee80211_start_tx_ba_session(sip->epub->hw, node->addr, tid);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32))
                        ieee80211_start_tx_ba_session(sip->epub->hw, node->sta->addr, tid);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 37))
                        ieee80211_start_tx_ba_session(node->sta, (u16)tid);
#else
                        ieee80211_start_tx_ba_session(node->sta, (u16)tid, 0);
#endif /* KERNEL_VERSION 2.6.39 */

                } else if(txtid->state == ESP_TID_STATE_OPERATIONAL) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
			sip_send_ampdu_action(sip->epub, SIP_AMPDU_TX_OPERATIONAL, node->addr, tid, node->ifidx, 0);
#else
			sip_send_ampdu_action(sip->epub, SIP_AMPDU_TX_OPERATIONAL, node->sta->addr, tid, node->ifidx, 0);
#endif
		} else {
                        esp_dbg(ESP_DBG_TXAMPDU, "%s tid %d TXAMPDU GOT OPERATIONAL EVT IN WRONG STATE %d\n", __func__, tid, txtid->state);
                }
        }
}

int sip_parse_event_debug(struct esp_pub *epub, const u8 *src, u8 *dst)
{
	struct sip_evt_debug* debug_evt =  (struct sip_evt_debug *)(src + SIP_CTRL_HDR_LEN);

	switch (debug_evt->results[0]) {
		case RDRSSI: {
			u32 mask = debug_evt->results[1];
			u8 *p = (u8 *)&debug_evt->results[2];
			u8 index;
			struct esp_node *enode;

			while (mask != 0) {
				index = ffs(mask) - 1;
				if (index > ESP_PUB_MAX_STA)
					break;
				enode = esp_get_node_by_index(epub, index);
				if (enode == NULL) {
					esp_dbg(ESP_DBG_ERROR, "trc mask dismatch");
				} else {
                 			dst += sprintf(dst, "%02x:%02x:%02x:%02x:%02x:%02x 0x%x 0x%x\n", 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
						enode->sta->addr[0], enode->sta->addr[1], enode->sta->addr[2],
						enode->sta->addr[3], enode->sta->addr[4], enode->sta->addr[5],
#else
						enode->addr[0], enode->addr[1], enode->addr[2],
						enode->addr[3], enode->addr[4], enode->addr[5],
#endif
						*p, *(p+1));
						p += 2;
				}
				mask &= ~(1<<index);
			};
                 	dst += sprintf(dst, "%c", '\0');
					break;
		}
		default: {
			int i;
        		for(i = 1; i < debug_evt->len; i++)
                 		dst += sprintf(dst, "0x%x%s", debug_evt->results[i], i == debug_evt->len -1 ? "":" " );
			break;
		}
	}

	return 0;
}

int sip_parse_events(struct esp_sip *sip, u8 *buf)
{
        struct sip_hdr *hdr = (struct sip_hdr *)buf;

        switch (hdr->c_evtid) {
	case SIP_EVT_TARGET_ON: {
		/* use rx work queue to send... */
		if (atomic_read(&sip->state) == SIP_PREPARE_BOOT || atomic_read(&sip->state) == SIP_BOOT) {
			atomic_set(&sip->state, SIP_SEND_INIT);
			queue_work(sip->epub->esp_wkq, &sip->rx_process_work);
		} else {
			esp_dbg(ESP_DBG_ERROR, "%s boot during wrong state %d\n", __func__, atomic_read(&sip->state));
		}
                break;
	}

        case SIP_EVT_BOOTUP: {
           	struct sip_evt_bootup2 *bootup_evt = (struct sip_evt_bootup2 *)(buf + SIP_CTRL_HDR_LEN);
		if (sip->rawbuf)
                	kfree(sip->rawbuf);
		
		sip_post_init(sip, bootup_evt);
		
		if (gl_bootup_cplx)	
			complete(gl_bootup_cplx);
                
		break;
        }
	case SIP_EVT_RESETTING:{
        	sip->epub->wait_reset = 1;                       
        	if (gl_bootup_cplx)	
			complete(gl_bootup_cplx);
		break;
	}
	case SIP_EVT_SLEEP:{
		//atomic_set(&sip->epub->ps.state, ESP_PM_ON);
		break;
	}
	case SIP_EVT_TXIDLE:{
		//struct sip_evt_txidle *txidle = (struct sip_evt_txidle *)(buf + SIP_CTRL_HDR_LEN);
		//sip_txdone_clear(sip, txidle->last_seq);
		break;
	}
#ifndef FAST_TX_STATUS
        case SIP_EVT_TX_STATUS: {
                struct sip_evt_tx_report *report = (struct sip_evt_tx_report *)(buf + SIP_CTRL_HDR_LEN);
                sip_txdoneq_process(sip, report);

                break;
        }
#endif /* FAST_TX_STATUS */

        case SIP_EVT_SCAN_RESULT: {
                struct sip_evt_scan_report *report = (struct sip_evt_scan_report *)(buf + SIP_CTRL_HDR_LEN);
                if (atomic_read(&sip->epub->wl.off)) {
                        esp_dbg(ESP_DBG_ERROR, "%s scan result while wlan off\n", __func__);
                        return 0;
                }
                sip_scandone_process(sip, report);

                break;
        }

	case SIP_EVT_ROC: {
                struct sip_evt_roc* report = (struct sip_evt_roc *)(buf + SIP_CTRL_HDR_LEN);
                esp_rocdone_process(sip->epub->hw, report);
                break;
        }


#ifdef ESP_RX_COPYBACK_TEST

        case SIP_EVT_COPYBACK: {
                u32 len = hdr->len - SIP_CTRL_HDR_LEN;

                esp_dbg(ESP_DBG_TRACE, "%s copyback len %d   seq %u\n", __func__, len, hdr->seq);

                memcpy(copyback_buf + copyback_offset, pkt->buf + SIP_CTRL_HDR_LEN, len);
                copyback_offset += len;

                //show_buf(pkt->buf, 256);

                //how about totlen % 256 == 0??
                if (hdr->hdr.len < 256) {
                        kfree(copyback_buf);
                }
        }
        break;
#endif /* ESP_RX_COPYBACK_TEST */
        case SIP_EVT_CREDIT_RPT:
                break;

#ifdef TEST_MODE
        case SIP_EVT_WAKEUP: {
                u8 check_str[12];
                struct sip_evt_wakeup* wakeup_evt=  (struct sip_evt_wakeup *)(buf + SIP_CTRL_HDR_LEN);
                sprintf((char *)&check_str, "%d", wakeup_evt->check_data);
                esp_test_cmd_event(TEST_CMD_WAKEUP, (char *)&check_str);
                break;
        }

        case SIP_EVT_DEBUG: {
                u8 check_str[640];
		sip_parse_event_debug(sip->epub, buf, check_str);
		esp_dbg(ESP_DBG_TRACE, "%s", check_str);
                esp_test_cmd_event(TEST_CMD_DEBUG, (char *)&check_str);
                break;
        }

        case SIP_EVT_LOOPBACK: {
                u8 check_str[12];
                struct sip_evt_loopback *loopback_evt = (struct sip_evt_loopback *)(buf + SIP_CTRL_HDR_LEN);
                esp_dbg(ESP_DBG_LOG, "%s loopback len %d seq %u\n", __func__,hdr->len, hdr->seq);

                if(loopback_evt->pack_id!=get_loopback_id()) {
                        sprintf((char *)&check_str, "seq id error %d, expect %d", loopback_evt->pack_id, get_loopback_id());
                        esp_test_cmd_event(TEST_CMD_LOOPBACK, (char *)&check_str);
                }

                if((loopback_evt->pack_id+1) <get_loopback_num()) {
                        inc_loopback_id();
                        sip_send_loopback_mblk(sip, loopback_evt->txlen, loopback_evt->rxlen, get_loopback_id());
                } else {
                        sprintf((char *)&check_str, "test over!");
                        esp_test_cmd_event(TEST_CMD_LOOPBACK, (char *)&check_str);
                }
                break;
        }
#endif  /*TEST_MODE*/

        case SIP_EVT_SNPRINTF_TO_HOST: {
                u8 *p = (buf + sizeof(struct sip_hdr) + sizeof(u16));
                u16 *len = (u16 *)(buf + sizeof(struct sip_hdr));
		char test_res_str[560];
		sprintf(test_res_str, "esp_host:%llx\nesp_target: %.*s", DRIVER_VER, *len, p);
		
                esp_dbg(ESP_SHOW, "%s\n", test_res_str);
		if(*len && sip->epub->sdio_state == ESP_SDIO_STATE_FIRST_INIT){
        		char filename[256];
			if (mod_eagle_path_get() == NULL)
        			sprintf(filename, "%s/%s", FWPATH, "test_results");
			else
        			sprintf(filename, "%s/%s", mod_eagle_path_get(), "test_results");
			esp_readwrite_file(filename, NULL, test_res_str, strlen(test_res_str));
		}
                break;
        }
        case SIP_EVT_TRC_AMPDU: {
                struct sip_evt_trc_ampdu *ep = (struct sip_evt_trc_ampdu*)(buf + SIP_CTRL_HDR_LEN);
                struct esp_node *node = NULL;
                int i = 0;

                if (atomic_read(&sip->epub->wl.off)) {
                        esp_dbg(ESP_DBG_ERROR, "%s scan result while wlan off\n", __func__);
                        return 0;
                }
		
		node = esp_get_node_by_addr(sip->epub, ep->addr);
		if(node == NULL)
			break;
                for (i = 0; i < 8; i++) {
                        if (ep->tid & (1<<i)) {
                                esp_tx_ba_session_op(sip, node, ep->state, i);
                        }
                }
                break;
        }

	case SIP_EVT_EP: {
		char *ep = (char *)(buf + SIP_CTRL_HDR_LEN);
		static int counter = 0;

		esp_dbg(ESP_ATE, "%s EVT_EP \n\n", __func__);
		if (counter++ < 2) {
			esp_dbg(ESP_ATE, "ATE: %s \n", ep);
		}

		esp_test_ate_done_cb(ep);

		break;
	}
	case SIP_EVT_INIT_EP: {
		char *ep = (char *)(buf + SIP_CTRL_HDR_LEN);
		esp_dbg(ESP_ATE, "Phy Init: %s \n", ep);
		break;
	}

	case SIP_EVT_NOISEFLOOR:{
		struct sip_evt_noisefloor *ep = (struct sip_evt_noisefloor *)(buf + SIP_CTRL_HDR_LEN);	                                      
		atomic_set(&sip->noise_floor, ep->noise_floor);
		break;
	}
	
	case SIP_EVT_NULLFUNC_REPORT:{
		struct sip_evt_nullfunc_report *nr = (struct sip_evt_nullfunc_report *)(buf + SIP_CTRL_HDR_LEN);
		struct esp_node *enode = NULL;
		u8 ifidx = nr->ifidx;
		u8 index = nr->index;
		u8 status = nr->status;

		if (index < 0 && index > ESP_PUB_MAX_STA)
			break;

		if (sip->epub->master_ifidx != ifidx)
			break;

		enode = esp_get_node_by_index(sip->epub, index);
		if (!enode)
			break;

		if (atomic_read(&enode->sta_state) == ESP_STA_STATE_LOST)
			break;

		/* assert status equals 0 , else ignore */
		if (status != 0)
			esp_dbg(ESP_DBG_ERROR, "nulldata status strange");

		atomic_set(&enode->loss_count, 0);
		atomic_set(&enode->time_remain, ESP_ND_TIME_REMAIN_MAX);
		atomic_set(&enode->sta_state, ESP_STA_STATE_NORM);

		esp_dbg(ESP_DBG_TRACE, "index %d, status %d, loss %d, tremain %d", index, status, atomic_read(&enode->loss_count), atomic_read(&enode->time_remain));
	}
        default:
                break;
        }

        return 0;
}

#ifdef HAS_INIT_DATA
#include "esp_init_data.h"
#else
#define ESP_INIT_NAME "esp_init_data.bin"
#endif /* HAS_INIT_DATA */

void sip_send_chip_init(struct esp_sip *sip)
{
	size_t size = 0;
#if (defined(CONFIG_DEBUG_FS) && defined(DEBUGFS_BOOTMODE)) || defined(ESP_CLASS)
	u8 *tmp_init_data = NULL;
#endif
#ifndef HAS_INIT_DATA
        const struct firmware *fw_entry;
        u8 * esp_init_data = NULL;
        int ret = 0;

        ret = esp_request_firmware(&fw_entry, ESP_INIT_NAME, sip->epub->dev);
        
        if (ret) {
                esp_dbg(ESP_DBG_ERROR, "%s =============ERROR! NO INIT DATA!!=================\n", __func__);
		return;
        }
        esp_init_data = kmemdup(fw_entry->data, fw_entry->size, GFP_KERNEL);

	size = fw_entry->size;

        esp_release_firmware(fw_entry);

        if (esp_init_data == NULL) {
                esp_dbg(ESP_DBG_ERROR, "%s =============ERROR! NO MEMORY!!=================\n", __func__);
		return;
        }
#else
	size = sizeof(esp_init_data);
#endif /* !HAS_INIT_DATA */

#if (defined(CONFIG_DEBUG_FS) && defined(DEBUGFS_BOOTMODE)) || defined(ESP_CLASS)
	tmp_init_data = kmemdup(esp_init_data, size, GFP_KERNEL);
	if (tmp_init_data == NULL) {
		esp_dbg(ESP_DBG_ERROR, "tmp_init_data alloc failed");
#ifndef HAS_INIT_DATA
        	kfree(esp_init_data);
#endif /* !HAS_INIT_DATA */
		return;
	}
	fix_init_data(tmp_init_data, size);
#else
	fix_init_data(esp_init_data, size);
#endif
	atomic_sub(1, &sip->tx_credits);
	
#if (defined(CONFIG_DEBUG_FS) && defined(DEBUGFS_BOOTMODE)) || defined(ESP_CLASS)
	sip_send_cmd(sip, SIP_CMD_INIT, size, (void *)tmp_init_data);

	kfree(tmp_init_data);
#else
	sip_send_cmd(sip, SIP_CMD_INIT, size, (void *)esp_init_data);
#endif
#ifndef HAS_INIT_DATA
        kfree(esp_init_data);
#endif /* !HAS_INIT_DATA */
}

int sip_send_config(struct esp_pub *epub, struct ieee80211_conf * conf)
{
        struct sk_buff *skb = NULL;
        struct sip_cmd_config *configcmd;

        skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_config) + sizeof(struct sip_hdr), SIP_CMD_CONFIG);
        if (!skb)
                return -EINVAL;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
        esp_dbg(ESP_DBG_TRACE, "%s config center freq %d\n", __func__, conf->chandef.chan->center_freq);
#else
        esp_dbg(ESP_DBG_TRACE, "%s config center freq %d\n", __func__, conf->channel->center_freq);
#endif
        configcmd = (struct sip_cmd_config *)(skb->data + sizeof(struct sip_hdr));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
        configcmd->center_freq= conf->chandef.chan->center_freq;
#else
        configcmd->center_freq= conf->channel->center_freq;
#endif
		configcmd->duration= 0;
        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}

int  sip_send_bss_info_update(struct esp_pub *epub, struct esp_vif *evif, u8 *bssid, int assoc)
{
        struct sk_buff *skb = NULL;
        struct sip_cmd_bss_info_update*bsscmd;

        skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_bss_info_update) + sizeof(struct sip_hdr), SIP_CMD_BSS_INFO_UPDATE);
        if (!skb)
                return -EINVAL;

        bsscmd = (struct sip_cmd_bss_info_update *)(skb->data + sizeof(struct sip_hdr));
        if (assoc == 2) { //hack for softAP mode
			bsscmd->beacon_int = evif->beacon_interval;
		} else if (assoc == 1) {
			set_bit(ESP_WL_FLAG_CONNECT, &epub->wl.flags);
        } else {
			clear_bit(ESP_WL_FLAG_CONNECT, &epub->wl.flags);
        }
		bsscmd->bssid_no = evif->index;
		bsscmd->isassoc= assoc;
		bsscmd->beacon_int = evif->beacon_interval;
        memcpy(bsscmd->bssid, bssid, ETH_ALEN);
        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}

int  sip_send_wmm_params(struct esp_pub *epub, u8 aci, const struct ieee80211_tx_queue_params *params)
{
        struct sk_buff *skb = NULL;
        struct sip_cmd_set_wmm_params* bsscmd;
        skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_set_wmm_params) + sizeof(struct sip_hdr), SIP_CMD_SET_WMM_PARAM);
        if (!skb)
                return -EINVAL;

        bsscmd = (struct sip_cmd_set_wmm_params *)(skb->data + sizeof(struct sip_hdr));
        bsscmd->aci= aci;
        bsscmd->aifs=params->aifs;
        bsscmd->txop_us=params->txop*32;

        bsscmd->ecw_min = 32 - __builtin_clz(params->cw_min);
        bsscmd->ecw_max= 32 -__builtin_clz(params->cw_max);

        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}

int sip_send_ampdu_action(struct esp_pub *epub, u8 action_num, const u8 * addr, u16 tid, u16 ssn, u8 buf_size)
{
        int index = 0;
        struct sk_buff *skb = NULL;
        struct sip_cmd_ampdu_action * action;
	if(action_num == SIP_AMPDU_RX_START){
		index = esp_get_empty_rxampdu(epub, addr, tid);
	} else if(action_num == SIP_AMPDU_RX_STOP){
		index = esp_get_exist_rxampdu(epub, addr, tid);
	}
        if(index < 0)
                return -EACCES;
        skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_ampdu_action) + sizeof(struct sip_hdr), SIP_CMD_AMPDU_ACTION);
        if(!skb)
                return -EINVAL;

        action = (struct sip_cmd_ampdu_action *)(skb->data + sizeof(struct sip_hdr));
        action->action = action_num;
	//for TX, it means interface index
	action->index = ssn;

        switch(action_num) {
        case SIP_AMPDU_RX_START:
                action->ssn = ssn;
        case SIP_AMPDU_RX_STOP:
                action->index = index;
        case SIP_AMPDU_TX_OPERATIONAL:
        case SIP_AMPDU_TX_STOP:
                action->win_size = buf_size;
                action->tid = tid;
                memcpy(action->addr, addr, ETH_ALEN);
                break;
        }

        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}

#ifdef HW_SCAN
/*send cmd to target, if aborted is true, inform target stop scan, report scan complete imediately
  return 1: complete over, 0: success, still have next scan, -1: hardware failure
  */
int sip_send_scan(struct esp_pub *epub)
{
        struct cfg80211_scan_request *scan_req = epub->wl.scan_req;
        struct sk_buff *skb = NULL;
        struct sip_cmd_scan *scancmd;
        u8 *ptr = NULL;
        int i;
	u8 append_len, ssid_len;

        ESSERT(scan_req != NULL);
        ssid_len = scan_req->n_ssids == 0 ? 0:
                (scan_req->n_ssids == 1 ? scan_req->ssids->ssid_len: scan_req->ssids->ssid_len + (scan_req->ssids + 1)->ssid_len);
        append_len = ssid_len + scan_req->n_channels + scan_req->ie_len;

        skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_scan) + sizeof(struct sip_hdr) + append_len, SIP_CMD_SCAN);

        if (!skb)
                return -EINVAL;

        ptr = skb->data;
        scancmd = (struct sip_cmd_scan *)(ptr + sizeof(struct sip_hdr));
        ptr += sizeof(struct sip_hdr);

        scancmd->aborted= false;

        if (scancmd->aborted==false) {
		ptr += sizeof(struct sip_cmd_scan);
                if (scan_req->n_ssids <=0 || (scan_req->n_ssids == 1&& ssid_len == 0)) {
                        scancmd->ssid_len = 0;
                } else { 
                        scancmd->ssid_len = ssid_len;
			if(scan_req->ssids->ssid_len == ssid_len)
                        	memcpy(ptr, scan_req->ssids->ssid, scancmd->ssid_len);
			else
				memcpy(ptr, (scan_req->ssids + 1)->ssid, scancmd->ssid_len);
                }

		ptr += scancmd->ssid_len;
                scancmd->n_channels=scan_req->n_channels;
                for (i=0; i<scan_req->n_channels; i++)
                        ptr[i] = scan_req->channels[i]->hw_value;
		
		ptr += scancmd->n_channels;
		if (scan_req->ie_len && scan_req->ie != NULL) {
                        scancmd->ie_len=scan_req->ie_len;
                        memcpy(ptr, scan_req->ie, scan_req->ie_len);
                } else {
			scancmd->ie_len = 0;
		}
		//add a flag that support two ssids,
		if(scan_req->n_ssids > 1)
			scancmd->ssid_len |= 0x80;

        }
        
        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}
#endif

int sip_send_suspend_config(struct esp_pub *epub, u8 suspend)
{
        struct sip_cmd_suspend *cmd = NULL;
	struct sk_buff *skb = NULL;

        skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_suspend) + sizeof(struct sip_hdr), SIP_CMD_SUSPEND);

        if (!skb)
                return -EINVAL;

        cmd = (struct sip_cmd_suspend *)(skb->data + sizeof(struct sip_hdr));
	cmd->suspend = suspend;
        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}

int sip_send_ps_config(struct esp_pub *epub, struct esp_ps *ps)
{
        struct sip_cmd_ps *pscmd = NULL;
        struct sk_buff *skb = NULL;
        struct sip_hdr *shdr = NULL;

        skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_ps) + sizeof(struct sip_hdr), SIP_CMD_PS);

        if (!skb)
		return -EINVAL;


        shdr = (struct sip_hdr *)skb->data;
        pscmd = (struct sip_cmd_ps *)(skb->data + sizeof(struct sip_hdr));

        pscmd->dtim_period = ps->dtim_period;
        pscmd->max_sleep_period = ps->max_sleep_period;

        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}

void sip_scandone_process(struct esp_sip *sip, struct sip_evt_scan_report *scan_report)
{
        struct esp_pub *epub = sip->epub;

        esp_dbg(ESP_DBG_TRACE, "eagle hw scan report\n");

        if (epub->wl.scan_req) {
                hw_scan_done(epub, scan_report->aborted);
                epub->wl.scan_req = NULL;
        }
}

int sip_send_setkey(struct esp_pub *epub, u8 bssid_no, u8 *peer_addr, struct ieee80211_key_conf *key, u8 isvalid)
{
        struct sip_cmd_setkey *setkeycmd;
        struct sk_buff *skb = NULL;

        skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_setkey) + sizeof(struct sip_hdr), SIP_CMD_SETKEY);

        if (!skb)
                return -EINVAL;

        setkeycmd = (struct sip_cmd_setkey *)(skb->data + sizeof(struct sip_hdr));

        if (peer_addr) {
                memcpy(setkeycmd->addr, peer_addr, ETH_ALEN);
        } else {
                memset(setkeycmd->addr, 0, ETH_ALEN);
        }

		setkeycmd->bssid_no = bssid_no;
        setkeycmd->hw_key_idx= key->hw_key_idx;

        if (isvalid) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
                setkeycmd->alg= key->alg;
#else
                setkeycmd->alg= esp_cipher2alg(key->cipher);
#endif /* NEW_KERNEL */
                setkeycmd->keyidx = key->keyidx;
                setkeycmd->keylen = key->keylen;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
                if (key->alg == ALG_TKIP) {
#else
                if (key->cipher == WLAN_CIPHER_SUITE_TKIP) {
#endif /* NEW_KERNEL */
                        memcpy(setkeycmd->key, key->key, 16);
                        memcpy(setkeycmd->key+16,key->key+24,8);
                        memcpy(setkeycmd->key+24,key->key+16,8);
                } else {
                        memcpy(setkeycmd->key, key->key, key->keylen);
                }

                setkeycmd->flags=1;
        } else {
                setkeycmd->flags=0;
        }
        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}

#ifdef FPGA_LOOPBACK
#define LOOPBACK_PKT_LEN 200
int sip_send_loopback_cmd_mblk(struct esp_sip *sip)
{
        int cnt, ret;

        for (cnt = 0; cnt < 4; cnt++) {
                if (0!=(ret=sip_send_loopback_mblk(sip, LOOPBACK_PKT_LEN, LOOPBACK_PKT_LEN, 0)))
                        return ret;
        }
        return 0;
}
#endif /* FPGA_LOOPBACK */

int sip_send_loopback_mblk(struct esp_sip *sip, int txpacket_len, int rxpacket_len, int packet_id)
{
        struct sk_buff *skb = NULL;
        struct sip_cmd_loopback *cmd;
        u8 *ptr = NULL;
        int i, ret;

        //send 100 loopback pkt
        if(txpacket_len)
                skb = sip_alloc_ctrl_skbuf(sip, sizeof(struct sip_cmd_loopback) + sizeof(struct sip_hdr) +  txpacket_len, SIP_CMD_LOOPBACK);
        else
                skb = sip_alloc_ctrl_skbuf(sip, sizeof(struct sip_cmd_loopback) + sizeof(struct sip_hdr), SIP_CMD_LOOPBACK);

        if (!skb)
                return -ENOMEM;

        ptr = skb->data;
        cmd = (struct sip_cmd_loopback *)(ptr + sizeof(struct sip_hdr));
        ptr += sizeof(struct sip_hdr);
        cmd->txlen = txpacket_len;
        cmd->rxlen = rxpacket_len;
        cmd->pack_id = packet_id;

        if (txpacket_len) {
                ptr += sizeof(struct sip_cmd_loopback);
                /* fill up pkt payload */
                for (i = 0; i < txpacket_len; i++) {
                        ptr[i] = i;
                }
        }

        ret = sip_cmd_enqueue(sip, skb, ENQUEUE_PRIOR_TAIL);
        if (ret <0)
                return ret;

        return 0;
}

//remain_on_channel 
int sip_send_roc(struct esp_pub *epub, u16 center_freq, u16 duration)
{
        struct sk_buff *skb = NULL;
        struct sip_cmd_config *configcmd;

        skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_config) + sizeof(struct sip_hdr), SIP_CMD_CONFIG);
        if (!skb)
                return -EINVAL;

        configcmd = (struct sip_cmd_config *)(skb->data + sizeof(struct sip_hdr));
        configcmd->center_freq= center_freq;
        configcmd->duration= duration;
        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
int sip_send_set_sta(struct esp_pub *epub, u8 ifidx, u8 set, struct ieee80211_sta *sta, struct ieee80211_vif *vif, u8 index)
#else
int sip_send_set_sta(struct esp_pub *epub, u8 ifidx, u8 set, struct esp_node *node, struct ieee80211_vif *vif, u8 index)
#endif
{
	struct sk_buff *skb = NULL;
	struct sip_cmd_setsta *setstacmd;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28))
	struct ieee80211_ht_info ht_info = node->ht_info;
#endif
	skb = sip_alloc_ctrl_skbuf(epub->sip, sizeof(struct sip_cmd_setsta) + sizeof(struct sip_hdr), SIP_CMD_SETSTA);
	if (!skb)
	return -EINVAL;

	setstacmd = (struct sip_cmd_setsta *)(skb->data + sizeof(struct sip_hdr));
	setstacmd->ifidx = ifidx;
	setstacmd->index = index;
	setstacmd->set = set;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
	if(sta->aid == 0)
		setstacmd->aid = vif->bss_conf.aid;
	else
		setstacmd->aid = sta->aid;
	memcpy(setstacmd->mac, sta->addr, ETH_ALEN);
	if(set){
		if(sta->ht_cap.ht_supported){
			if(sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
				setstacmd->phymode = ESP_IEEE80211_T_HT20_S;
			else
				setstacmd->phymode = ESP_IEEE80211_T_HT20_L;
			setstacmd->ampdu_factor = sta->ht_cap.ampdu_factor;
			setstacmd->ampdu_density = sta->ht_cap.ampdu_density;
		} else {
			if(sta->supp_rates[IEEE80211_BAND_2GHZ] & (~(u32)CONF_HW_BIT_RATE_11B_MASK)){
				setstacmd->phymode = ESP_IEEE80211_T_OFDM;
			} else {
				setstacmd->phymode = ESP_IEEE80211_T_CCK;
			}
		}
	}
#else
    setstacmd->aid = node->aid;
    memcpy(setstacmd->mac, node->addr, ETH_ALEN);
    if(set){
        if(ht_info.ht_supported){
            if(ht_info.cap & IEEE80211_HT_CAP_SGI_20)
                setstacmd->phymode = ESP_IEEE80211_T_HT20_S;
            else
                setstacmd->phymode = ESP_IEEE80211_T_HT20_L;
            setstacmd->ampdu_factor = ht_info.ampdu_factor;
            setstacmd->ampdu_density = ht_info.ampdu_density;
        } else {
            //note supp_rates is u64[] in 2.6.27
            if(node->supp_rates[IEEE80211_BAND_2GHZ] & (~(u64)CONF_HW_BIT_RATE_11B_MASK)){
                setstacmd->phymode = ESP_IEEE80211_T_OFDM;
            } else {
                setstacmd->phymode = ESP_IEEE80211_T_CCK;
            }   
        }   
    }   
#endif
        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}

int sip_send_recalc_credit(struct esp_pub *epub)
{
	struct sk_buff *skb = NULL;

	skb = sip_alloc_ctrl_skbuf(epub->sip, 0 + sizeof(struct sip_hdr), SIP_CMD_RECALC_CREDIT);
	if (!skb)
		return -ENOMEM;
	
        return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_HEAD);
}

int sip_cmd(struct esp_pub *epub, enum sip_cmd_id cmd_id, u8 *cmd_buf, u8 cmd_len)
{
	struct sk_buff *skb = NULL;

	skb = sip_alloc_ctrl_skbuf(epub->sip, cmd_len + sizeof(struct sip_hdr), cmd_id);
	if (!skb)
		return -ENOMEM;

	memcpy(skb->data + sizeof(struct sip_hdr), cmd_buf, cmd_len);

	return sip_cmd_enqueue(epub->sip, skb, ENQUEUE_PRIOR_TAIL);
}
