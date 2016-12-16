/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#define _RTW_RECV_C_
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <mlme_osdep.h>
#include <linux/ip.h>
#include <linux/if_ether.h>
#include <usb_ops.h>
#include <linux/ieee80211.h>
#include <wifi.h>
#include <rtl8723a_recv.h>
#include <rtl8723a_xmit.h>

void rtw_signal_stat_timer_hdl23a(unsigned long data);

void _rtw_init_sta_recv_priv23a(struct sta_recv_priv *psta_recvpriv)
{



	spin_lock_init(&psta_recvpriv->lock);

	/* for (i = 0; i<MAX_RX_NUMBLKS; i++) */
	/*	_rtw_init_queue23a(&psta_recvpriv->blk_strms[i]); */

	_rtw_init_queue23a(&psta_recvpriv->defrag_q);


}

int _rtw_init_recv_priv23a(struct recv_priv *precvpriv,
			struct rtw_adapter *padapter)
{
	struct recv_frame *precvframe;
	int i;
	int res = _SUCCESS;

	spin_lock_init(&precvpriv->lock);

	_rtw_init_queue23a(&precvpriv->free_recv_queue);
	_rtw_init_queue23a(&precvpriv->recv_pending_queue);
	_rtw_init_queue23a(&precvpriv->uc_swdec_pending_queue);

	precvpriv->adapter = padapter;

	for (i = 0; i < NR_RECVFRAME ; i++) {
		precvframe = kzalloc(sizeof(struct recv_frame), GFP_KERNEL);
		if (!precvframe)
			break;
		INIT_LIST_HEAD(&precvframe->list);

		list_add_tail(&precvframe->list,
			      &precvpriv->free_recv_queue.queue);

		precvframe->adapter = padapter;
		precvframe++;
	}

	precvpriv->free_recvframe_cnt = i;
	precvpriv->rx_pending_cnt = 1;

	res = rtl8723au_init_recv_priv(padapter);

	setup_timer(&precvpriv->signal_stat_timer, rtw_signal_stat_timer_hdl23a,
		    (unsigned long)padapter);

	precvpriv->signal_stat_sampling_interval = 1000; /* ms */

	rtw_set_signal_stat_timer(precvpriv);

	return res;
}

void _rtw_free_recv_priv23a(struct recv_priv *precvpriv)
{
	struct rtw_adapter *padapter = precvpriv->adapter;
	struct recv_frame *precvframe, *ptmp;

	rtw_free_uc_swdec_pending_queue23a(padapter);

	list_for_each_entry_safe(precvframe, ptmp,
				 &precvpriv->free_recv_queue.queue, list) {
		list_del_init(&precvframe->list);
		kfree(precvframe);
	}

	rtl8723au_free_recv_priv(padapter);
}

struct recv_frame *rtw_alloc_recvframe23a(struct rtw_queue *pfree_recv_queue)
{
	struct recv_frame *pframe;
	struct rtw_adapter *padapter;
	struct recv_priv *precvpriv;

	spin_lock_bh(&pfree_recv_queue->lock);

	pframe = list_first_entry_or_null(&pfree_recv_queue->queue,
					  struct recv_frame, list);
	if (pframe) {
		list_del_init(&pframe->list);
		padapter = pframe->adapter;
		if (padapter) {
			precvpriv = &padapter->recvpriv;
			if (pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt--;
		}
	}

	spin_unlock_bh(&pfree_recv_queue->lock);

	return pframe;
}

int rtw_free_recvframe23a(struct recv_frame *precvframe)
{
	struct rtw_adapter *padapter = precvframe->adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct rtw_queue *pfree_recv_queue;

	if (precvframe->pkt) {
		dev_kfree_skb_any(precvframe->pkt);/* free skb by driver */
		precvframe->pkt = NULL;
	}

	pfree_recv_queue = &precvpriv->free_recv_queue;
	spin_lock_bh(&pfree_recv_queue->lock);

	list_del_init(&precvframe->list);

	list_add_tail(&precvframe->list, get_list_head(pfree_recv_queue));

	if (padapter) {
		if (pfree_recv_queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;
	}

	spin_unlock_bh(&pfree_recv_queue->lock);



	return _SUCCESS;
}

int rtw_enqueue_recvframe23a(struct recv_frame *precvframe, struct rtw_queue *queue)
{
	struct rtw_adapter *padapter = precvframe->adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	spin_lock_bh(&queue->lock);

	list_del_init(&precvframe->list);

	list_add_tail(&precvframe->list, get_list_head(queue));

	if (padapter) {
		if (queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;
	}

	spin_unlock_bh(&queue->lock);

	return _SUCCESS;
}

/*
caller : defrag ; recvframe_chk_defrag23a in recv_thread  (passive)
pframequeue: defrag_queue : will be accessed in recv_thread  (passive)

using spinlock to protect

*/

static void rtw_free_recvframe23a_queue(struct rtw_queue *pframequeue)
{
	struct recv_frame *hdr, *ptmp;
	struct list_head *phead;

	spin_lock(&pframequeue->lock);
	phead = get_list_head(pframequeue);
	list_for_each_entry_safe(hdr, ptmp, phead, list)
		rtw_free_recvframe23a(hdr);
	spin_unlock(&pframequeue->lock);
}

u32 rtw_free_uc_swdec_pending_queue23a(struct rtw_adapter *adapter)
{
	u32 cnt = 0;
	struct recv_frame *pending_frame;

	while ((pending_frame = rtw_alloc_recvframe23a(&adapter->recvpriv.uc_swdec_pending_queue))) {
		rtw_free_recvframe23a(pending_frame);
		DBG_8723A("%s: dequeue uc_swdec_pending_queue\n", __func__);
		cnt++;
	}

	return cnt;
}

struct recv_buf *rtw_dequeue_recvbuf23a (struct rtw_queue *queue)
{
	unsigned long irqL;
	struct recv_buf *precvbuf;

	spin_lock_irqsave(&queue->lock, irqL);

	precvbuf = list_first_entry_or_null(&queue->queue,
					    struct recv_buf, list);
	if (precvbuf)
		list_del_init(&precvbuf->list);

	spin_unlock_irqrestore(&queue->lock, irqL);

	return precvbuf;
}

int recvframe_chkmic(struct rtw_adapter *adapter,
		     struct recv_frame *precvframe);
int recvframe_chkmic(struct rtw_adapter *adapter,
		     struct recv_frame *precvframe) {

	int	i, res = _SUCCESS;
	u32	datalen;
	u8	miccode[8];
	u8	bmic_err = false, brpt_micerror = true;
	u8	*pframe, *payload, *pframemic;
	u8	*mickey;
	struct	sta_info *stainfo;
	struct	rx_pkt_attrib *prxattrib = &precvframe->attrib;
	struct	security_priv *psecuritypriv = &adapter->securitypriv;

	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;


	stainfo = rtw_get_stainfo23a(&adapter->stapriv, &prxattrib->ta[0]);

	if (prxattrib->encrypt == WLAN_CIPHER_SUITE_TKIP) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 "recvframe_chkmic:prxattrib->encrypt == WLAN_CIPHER_SUITE_TKIP\n");
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 "recvframe_chkmic:da = %pM\n", prxattrib->ra);

		/* calculate mic code */
		if (stainfo != NULL) {
			if (is_multicast_ether_addr(prxattrib->ra)) {
				mickey = &psecuritypriv->dot118021XGrprxmickey[prxattrib->key_index].skey[0];

				RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
					 "recvframe_chkmic: bcmc key\n");

				if (!psecuritypriv->binstallGrpkey) {
					res = _FAIL;
					RT_TRACE(_module_rtl871x_recv_c_,
						 _drv_err_,
						 "recvframe_chkmic:didn't install group key!\n");
					DBG_8723A("\n recvframe_chkmic:didn't "
						  "install group key!!!!!!\n");
					goto exit;
				}
			} else {
				mickey = &stainfo->dot11tkiprxmickey.skey[0];
				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
					 "recvframe_chkmic: unicast key\n");
			}

			/* icv_len included the mic code */
			datalen = precvframe->pkt->len-prxattrib->
				hdrlen-prxattrib->iv_len-prxattrib->icv_len - 8;
			pframe = precvframe->pkt->data;
			payload = pframe + prxattrib->hdrlen +
				prxattrib->iv_len;

			RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
				 "prxattrib->iv_len =%d prxattrib->icv_len =%d\n",
				 prxattrib->iv_len, prxattrib->icv_len);

			/* care the length of the data */
			rtw_seccalctkipmic23a(mickey, pframe, payload,
					   datalen, &miccode[0],
					   (unsigned char)prxattrib->priority);

			pframemic = payload + datalen;

			bmic_err = false;

			for (i = 0; i < 8; i++) {
				if (miccode[i] != *(pframemic + i)) {
					RT_TRACE(_module_rtl871x_recv_c_,
						 _drv_err_,
						 "recvframe_chkmic:miccode[%d](%02x) != *(pframemic+%d)(%02x)\n",
						 i, miccode[i],
						 i, *(pframemic + i));
					bmic_err = true;
				}
			}

			if (bmic_err == true) {
				int i;

				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
					 "*(pframemic-8)-*(pframemic-1) =%*phC\n",
					 8, pframemic - 8);
				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
					 "*(pframemic-16)-*(pframemic-9) =%*phC\n",
					 8, pframemic - 16);

				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
					 "====== demp packet (len =%d) ======\n",
					 precvframe->pkt->len);
				for (i = 0; i < precvframe->pkt->len; i = i + 8) {
					RT_TRACE(_module_rtl871x_recv_c_,
						 _drv_err_, "%*phC\n",
						 8, precvframe->pkt->data + i);
				}
				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
					 "====== demp packet end [len =%d]======\n",
					 precvframe->pkt->len);
				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
					 "hrdlen =%d\n", prxattrib->hdrlen);

				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
					 "ra = %pM psecuritypriv->binstallGrpkey =%d\n",
					 prxattrib->ra,
					 psecuritypriv->binstallGrpkey);

				/*  double check key_index for some timing
				    issue, cannot compare with
				    psecuritypriv->dot118021XGrpKeyid also
				    cause timing issue */
				if ((is_multicast_ether_addr(prxattrib->ra)) &&
				    (prxattrib->key_index !=
				     pmlmeinfo->key_index))
					brpt_micerror = false;

				if ((prxattrib->bdecrypted == true) &&
				    (brpt_micerror == true)) {
					rtw_handle_tkip_mic_err23a(adapter, (u8)is_multicast_ether_addr(prxattrib->ra));
					RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
						 "mic error :prxattrib->bdecrypted =%d\n",
						 prxattrib->bdecrypted);
					DBG_8723A(" mic error :prxattrib->"
						  "bdecrypted =%d\n",
						  prxattrib->bdecrypted);
				} else {
					RT_TRACE(_module_rtl871x_recv_c_,
						 _drv_err_,
						 "mic error :prxattrib->bdecrypted =%d\n",
						 prxattrib->bdecrypted);
					DBG_8723A(" mic error :prxattrib->"
						  "bdecrypted =%d\n",
						  prxattrib->bdecrypted);
				}

				res = _FAIL;
			} else {
				/* mic checked ok */
				if (!psecuritypriv->bcheck_grpkey &&
				    is_multicast_ether_addr(prxattrib->ra)) {
					psecuritypriv->bcheck_grpkey = 1;
					RT_TRACE(_module_rtl871x_recv_c_,
						 _drv_err_,
						 "psecuritypriv->bcheck_grpkey = true\n");
				}
			}
		} else {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "recvframe_chkmic: rtw_get_stainfo23a ==NULL!!!\n");
		}

		skb_trim(precvframe->pkt, precvframe->pkt->len - 8);
	}

exit:



	return res;
}

/* decrypt and set the ivlen, icvlen of the recv_frame */
struct recv_frame *decryptor(struct rtw_adapter *padapter,
			     struct recv_frame *precv_frame);
struct recv_frame *decryptor(struct rtw_adapter *padapter,
			     struct recv_frame *precv_frame)
{
	struct rx_pkt_attrib *prxattrib = &precv_frame->attrib;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct recv_frame *return_packet = precv_frame;
	int res = _SUCCESS;

	RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
		 "prxstat->decrypted =%x prxattrib->encrypt = 0x%03x\n",
		 prxattrib->bdecrypted, prxattrib->encrypt);

	if (prxattrib->encrypt > 0) {
		u8 *iv = precv_frame->pkt->data + prxattrib->hdrlen;

		prxattrib->key_index = (((iv[3]) >> 6) & 0x3);

		if (prxattrib->key_index > WEP_KEYS) {
			DBG_8723A("prxattrib->key_index(%d) > WEP_KEYS\n",
				  prxattrib->key_index);

			switch (prxattrib->encrypt) {
			case WLAN_CIPHER_SUITE_WEP40:
			case WLAN_CIPHER_SUITE_WEP104:
				prxattrib->key_index =
					psecuritypriv->dot11PrivacyKeyIndex;
				break;
			case WLAN_CIPHER_SUITE_TKIP:
			case WLAN_CIPHER_SUITE_CCMP:
			default:
				prxattrib->key_index =
					psecuritypriv->dot118021XGrpKeyid;
				break;
			}
		}
	}

	if ((prxattrib->encrypt > 0) && ((prxattrib->bdecrypted == 0))) {
		psecuritypriv->hw_decrypted = 0;
		switch (prxattrib->encrypt) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			rtw_wep_decrypt23a(padapter, precv_frame);
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			res = rtw_tkip_decrypt23a(padapter, precv_frame);
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			res = rtw_aes_decrypt23a(padapter, precv_frame);
			break;
		default:
			break;
		}
	} else if (prxattrib->bdecrypted == 1 && prxattrib->encrypt > 0 &&
		   (psecuritypriv->busetkipkey == 1 ||
		    prxattrib->encrypt != WLAN_CIPHER_SUITE_TKIP)) {
			psecuritypriv->hw_decrypted = 1;
	}

	if (res == _FAIL) {
		rtw_free_recvframe23a(return_packet);
		return_packet = NULL;
	}



	return return_packet;
}

/* set the security information in the recv_frame */
static struct recv_frame *portctrl(struct rtw_adapter *adapter,
				   struct recv_frame *precv_frame)
{
	u8 *psta_addr, *ptr;
	uint auth_alg;
	struct recv_frame *pfhdr;
	struct sta_info *psta;
	struct sta_priv *pstapriv ;
	struct recv_frame *prtnframe;
	u16 ether_type;
	u16 eapol_type = ETH_P_PAE;/* for Funia BD's WPA issue */
	struct rx_pkt_attrib *pattrib;

	pstapriv = &adapter->stapriv;

	auth_alg = adapter->securitypriv.dot11AuthAlgrthm;

	pfhdr = precv_frame;
	pattrib = &pfhdr->attrib;
	psta_addr = pattrib->ta;
	psta = rtw_get_stainfo23a(pstapriv, psta_addr);

	RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
		 "########portctrl:adapter->securitypriv.dot11AuthAlgrthm =%d\n",
		 adapter->securitypriv.dot11AuthAlgrthm);

	prtnframe = precv_frame;

	if (auth_alg == dot11AuthAlgrthm_8021X) {
		/* get ether_type */
		ptr = pfhdr->pkt->data + pfhdr->attrib.hdrlen;

		ether_type = (ptr[6] << 8) | ptr[7];

		if (psta && psta->ieee8021x_blocked) {
			/* blocked */
			/* only accept EAPOL frame */
			RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
				 "########portctrl:psta->ieee8021x_blocked ==1\n");

			if (ether_type != eapol_type) {
				/* free this frame */
				rtw_free_recvframe23a(precv_frame);
				prtnframe = NULL;
			}
		}
	}

	return prtnframe;
}

int recv_decache(struct recv_frame *precv_frame, u8 bretry,
		 struct stainfo_rxcache *prxcache);
int recv_decache(struct recv_frame *precv_frame, u8 bretry,
		 struct stainfo_rxcache *prxcache)
{
	int tid = precv_frame->attrib.priority;

	u16 seq_ctrl = ((precv_frame->attrib.seq_num & 0xffff) << 4) |
		(precv_frame->attrib.frag_num & 0xf);



	if (tid > 15) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
			 "recv_decache, (tid>15)! seq_ctrl = 0x%x, tid = 0x%x\n",
			 seq_ctrl, tid);

		return _FAIL;
	}

	if (1) { /* if (bretry) */
		if (seq_ctrl == prxcache->tid_rxseq[tid]) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
				 "recv_decache, seq_ctrl = 0x%x, tid = 0x%x, tid_rxseq = 0x%x\n",
				 seq_ctrl, tid, prxcache->tid_rxseq[tid]);

			return _FAIL;
		}
	}

	prxcache->tid_rxseq[tid] = seq_ctrl;



	return _SUCCESS;
}

void process23a_pwrbit_data(struct rtw_adapter *padapter,
			 struct recv_frame *precv_frame);
void process23a_pwrbit_data(struct rtw_adapter *padapter,
			 struct recv_frame *precv_frame)
{
#ifdef CONFIG_8723AU_AP_MODE
	unsigned char pwrbit;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct rx_pkt_attrib *pattrib = &precv_frame->attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;

	psta = rtw_get_stainfo23a(pstapriv, pattrib->src);

	if (psta) {
		pwrbit = ieee80211_has_pm(hdr->frame_control);

		if (pwrbit) {
			if (!(psta->state & WIFI_SLEEP_STATE))
				stop_sta_xmit23a(padapter, psta);
		} else {
			if (psta->state & WIFI_SLEEP_STATE)
				wakeup_sta_to_xmit23a(padapter, psta);
		}
	}

#endif
}

void process_wmmps_data(struct rtw_adapter *padapter,
			struct recv_frame *precv_frame);
void process_wmmps_data(struct rtw_adapter *padapter,
			struct recv_frame *precv_frame)
{
#ifdef CONFIG_8723AU_AP_MODE
	struct rx_pkt_attrib *pattrib = &precv_frame->attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;

	psta = rtw_get_stainfo23a(pstapriv, pattrib->src);

	if (!psta)
		return;


	if (!psta->qos_option)
		return;

	if (!(psta->qos_info & 0xf))
		return;

	if (psta->state & WIFI_SLEEP_STATE) {
		u8 wmmps_ac = 0;

		switch (pattrib->priority) {
		case 1:
		case 2:
			wmmps_ac = psta->uapsd_bk & BIT(1);
			break;
		case 4:
		case 5:
			wmmps_ac = psta->uapsd_vi & BIT(1);
			break;
		case 6:
		case 7:
			wmmps_ac = psta->uapsd_vo & BIT(1);
			break;
		case 0:
		case 3:
		default:
			wmmps_ac = psta->uapsd_be & BIT(1);
			break;
		}

		if (wmmps_ac) {
			if (psta->sleepq_ac_len > 0) {
				/* process received triggered frame */
				xmit_delivery_enabled_frames23a(padapter, psta);
			} else {
				/* issue one qos null frame with More data bit = 0 and the EOSP bit set (= 1) */
				issue_qos_nulldata23a(padapter, psta->hwaddr,
						   (u16)pattrib->priority,
						   0, 0);
			}
		}
	}

#endif
}

static void count_rx_stats(struct rtw_adapter *padapter,
			   struct recv_frame *prframe, struct sta_info *sta)
{
	int sz;
	struct sta_info *psta = NULL;
	struct stainfo_stats *pstats = NULL;
	struct rx_pkt_attrib *pattrib = & prframe->attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	sz = prframe->pkt->len;
	precvpriv->rx_bytes += sz;

	padapter->mlmepriv.LinkDetectInfo.NumRxOkInPeriod++;

	if ((!is_broadcast_ether_addr(pattrib->dst)) &&
	    (!is_multicast_ether_addr(pattrib->dst)))
		padapter->mlmepriv.LinkDetectInfo.NumRxUnicastOkInPeriod++;

	if (sta)
		psta = sta;
	else
		psta = prframe->psta;

	if (psta) {
		pstats = &psta->sta_stats;

		pstats->rx_data_pkts++;
		pstats->rx_bytes += sz;
	}
}

static int sta2sta_data_frame(struct rtw_adapter *adapter,
			      struct recv_frame *precv_frame,
			      struct sta_info**psta)
{
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = & precv_frame->attrib;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = myid(&adapter->eeprompriv);
	u8 *sta_addr = NULL;
	int bmcast = is_multicast_ether_addr(pattrib->dst);



	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {

		/*  filter packets that SA is myself or multicast or broadcast */
		if (ether_addr_equal(myhwaddr, pattrib->src)) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "SA == myself\n");
			ret = _FAIL;
			goto exit;
		}

		if (!ether_addr_equal(myhwaddr, pattrib->dst) && !bmcast) {
			ret = _FAIL;
			goto exit;
		}

		if (ether_addr_equal(pattrib->bssid, "\x0\x0\x0\x0\x0\x0") ||
		    ether_addr_equal(mybssid, "\x0\x0\x0\x0\x0\x0") ||
		    !ether_addr_equal(pattrib->bssid, mybssid)) {
			ret = _FAIL;
			goto exit;
		}

		sta_addr = pattrib->src;
	} else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		/*  For Station mode, sa and bssid should always be BSSID,
		    and DA is my mac-address */
		if (!ether_addr_equal(pattrib->bssid, pattrib->src)) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "bssid != TA under STATION_MODE; drop pkt\n");
			ret = _FAIL;
			goto exit;
		}

		sta_addr = pattrib->bssid;

	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		if (bmcast) {
			/*  For AP mode, if DA == MCAST, then BSSID should be also MCAST */
			if (!is_multicast_ether_addr(pattrib->bssid)) {
				ret = _FAIL;
				goto exit;
			}
		} else { /*  not mc-frame */
			/*  For AP mode, if DA is non-MCAST, then it must
			    be BSSID, and bssid == BSSID */
			if (!ether_addr_equal(pattrib->bssid, pattrib->dst)) {
				ret = _FAIL;
				goto exit;
			}

			sta_addr = pattrib->src;
		}
	} else if (check_fwstate(pmlmepriv, WIFI_MP_STATE)) {
		ether_addr_copy(pattrib->dst, hdr->addr1);
		ether_addr_copy(pattrib->src, hdr->addr2);
		ether_addr_copy(pattrib->bssid, hdr->addr3);
		ether_addr_copy(pattrib->ra, pattrib->dst);
		ether_addr_copy(pattrib->ta, pattrib->src);

		sta_addr = mybssid;
	} else {
		ret  = _FAIL;
	}

	if (bmcast)
		*psta = rtw_get_bcmc_stainfo23a(adapter);
	else
		*psta = rtw_get_stainfo23a(pstapriv, sta_addr); /*  get ap_info */

	if (*psta == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
			 "can't get psta under sta2sta_data_frame ; drop pkt\n");
		ret = _FAIL;
		goto exit;
	}

exit:

	return ret;
}

int ap2sta_data_frame(struct rtw_adapter *adapter,
		      struct recv_frame *precv_frame,
		      struct sta_info **psta);
int ap2sta_data_frame(struct rtw_adapter *adapter,
		      struct recv_frame *precv_frame,
		      struct sta_info **psta)
{
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct rx_pkt_attrib *pattrib = & precv_frame->attrib;
	int ret = _SUCCESS;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = myid(&adapter->eeprompriv);
	int bmcast = is_multicast_ether_addr(pattrib->dst);



	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) &&
	    (check_fwstate(pmlmepriv, _FW_LINKED) ||
	     check_fwstate(pmlmepriv, _FW_UNDER_LINKING))) {

		/* filter packets that SA is myself or multicast or broadcast */
		if (ether_addr_equal(myhwaddr, pattrib->src)) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "SA == myself\n");
			ret = _FAIL;
			goto exit;
		}

		/*  da should be for me */
		if (!ether_addr_equal(myhwaddr, pattrib->dst) && !bmcast) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
				 "ap2sta_data_frame:  compare DA failed; DA=%pM\n",
				 pattrib->dst);
			ret = _FAIL;
			goto exit;
		}

		/*  check BSSID */
		if (ether_addr_equal(pattrib->bssid, "\x0\x0\x0\x0\x0\x0") ||
		    ether_addr_equal(mybssid, "\x0\x0\x0\x0\x0\x0") ||
		    !ether_addr_equal(pattrib->bssid, mybssid)) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
				 "ap2sta_data_frame:  compare BSSID failed; BSSID=%pM\n",
				 pattrib->bssid);
			RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
				 "mybssid=%pM\n", mybssid);

			if (!bmcast) {
				DBG_8723A("issue_deauth23a to the nonassociated ap=%pM for the reason(7)\n",
					  pattrib->bssid);
				issue_deauth23a(adapter, pattrib->bssid,
					     WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
			}

			ret = _FAIL;
			goto exit;
		}

		if (bmcast)
			*psta = rtw_get_bcmc_stainfo23a(adapter);
		else
			/*  get ap_info */
			*psta = rtw_get_stainfo23a(pstapriv, pattrib->bssid);

		if (*psta == NULL) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "ap2sta: can't get psta under STATION_MODE; drop pkt\n");
			ret = _FAIL;
			goto exit;
		}

		if (ieee80211_is_nullfunc(hdr->frame_control)) {
			/* No data, will not indicate to upper layer,
			   temporily count it here */
			count_rx_stats(adapter, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}

	} else if (check_fwstate(pmlmepriv, WIFI_MP_STATE) &&
		   check_fwstate(pmlmepriv, _FW_LINKED)) {
		ether_addr_copy(pattrib->dst, hdr->addr1);
		ether_addr_copy(pattrib->src, hdr->addr2);
		ether_addr_copy(pattrib->bssid, hdr->addr3);
		ether_addr_copy(pattrib->ra, pattrib->dst);
		ether_addr_copy(pattrib->ta, pattrib->src);

		/*  */
		ether_addr_copy(pattrib->bssid,  mybssid);

		/*  get sta_info */
		*psta = rtw_get_stainfo23a(pstapriv, pattrib->bssid);
		if (*psta == NULL) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "can't get psta under MP_MODE ; drop pkt\n");
			ret = _FAIL;
			goto exit;
		}
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		/* Special case */
		ret = RTW_RX_HANDLED;
		goto exit;
	} else {
		if (ether_addr_equal(myhwaddr, pattrib->dst) && !bmcast) {
			*psta = rtw_get_stainfo23a(pstapriv, pattrib->bssid);
			if (*psta == NULL) {
				DBG_8723A("issue_deauth23a to the ap=%pM for the reason(7)\n",
					  pattrib->bssid);

				issue_deauth23a(adapter, pattrib->bssid,
					     WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
			}
		}

		ret = _FAIL;
	}

exit:



	return ret;
}

int sta2ap_data_frame(struct rtw_adapter *adapter,
		      struct recv_frame *precv_frame,
		      struct sta_info **psta);
int sta2ap_data_frame(struct rtw_adapter *adapter,
		      struct recv_frame *precv_frame,
		      struct sta_info **psta)
{
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct rx_pkt_attrib *pattrib = & precv_frame->attrib;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	unsigned char *mybssid = get_bssid(pmlmepriv);
	int ret = _SUCCESS;



	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		/* For AP mode, RA = BSSID, TX = STA(SRC_ADDR), A3 = DST_ADDR */
		if (!ether_addr_equal(pattrib->bssid, mybssid)) {
			ret = _FAIL;
			goto exit;
		}

		*psta = rtw_get_stainfo23a(pstapriv, pattrib->src);
		if (*psta == NULL) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "can't get psta under AP_MODE; drop pkt\n");
			DBG_8723A("issue_deauth23a to sta=%pM for the reason(7)\n",
				  pattrib->src);

			issue_deauth23a(adapter, pattrib->src,
				     WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);

			ret = RTW_RX_HANDLED;
			goto exit;
		}

		process23a_pwrbit_data(adapter, precv_frame);

		/* We only get here if it's a data frame, so no need to
		 * confirm data frame type first */
		if (ieee80211_is_data_qos(hdr->frame_control))
			process_wmmps_data(adapter, precv_frame);

		if (ieee80211_is_nullfunc(hdr->frame_control)) {
			/* No data, will not indicate to upper layer,
			   temporily count it here */
			count_rx_stats(adapter, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}
	} else {
		u8 *myhwaddr = myid(&adapter->eeprompriv);

		if (!ether_addr_equal(pattrib->ra, myhwaddr)) {
			ret = RTW_RX_HANDLED;
			goto exit;
		}
		DBG_8723A("issue_deauth23a to sta=%pM for the reason(7)\n",
			  pattrib->src);
		issue_deauth23a(adapter, pattrib->src,
			     WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
		ret = RTW_RX_HANDLED;
		goto exit;
	}

exit:



	return ret;
}

static int validate_recv_ctrl_frame(struct rtw_adapter *padapter,
				    struct recv_frame *precv_frame)
{
#ifdef CONFIG_8723AU_AP_MODE
	struct rx_pkt_attrib *pattrib = &precv_frame->attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (!ieee80211_is_ctl(hdr->frame_control))
		return _FAIL;

	/* receive the frames that ra(a1) is my address */
	if (!ether_addr_equal(hdr->addr1, myid(&padapter->eeprompriv)))
		return _FAIL;

	/* only handle ps-poll */
	if (ieee80211_is_pspoll(hdr->frame_control)) {
		struct ieee80211_pspoll *psp = (struct ieee80211_pspoll *)hdr;
		u16 aid;
		u8 wmmps_ac = 0;
		struct sta_info *psta = NULL;

		aid = le16_to_cpu(psp->aid) & 0x3fff;
		psta = rtw_get_stainfo23a(pstapriv, hdr->addr2);

		if (!psta || psta->aid != aid)
			return _FAIL;

		/* for rx pkt statistics */
		psta->sta_stats.rx_ctrl_pkts++;

		switch (pattrib->priority) {
		case 1:
		case 2:
			wmmps_ac = psta->uapsd_bk & BIT(0);
			break;
		case 4:
		case 5:
			wmmps_ac = psta->uapsd_vi & BIT(0);
			break;
		case 6:
		case 7:
			wmmps_ac = psta->uapsd_vo & BIT(0);
			break;
		case 0:
		case 3:
		default:
			wmmps_ac = psta->uapsd_be & BIT(0);
			break;
		}

		if (wmmps_ac)
			return _FAIL;

		if (psta->state & WIFI_STA_ALIVE_CHK_STATE) {
			DBG_8723A("%s alive check-rx ps-poll\n", __func__);
			psta->expire_to = pstapriv->expire_to;
			psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
		}

		if ((psta->state & WIFI_SLEEP_STATE) &&
		    (pstapriv->sta_dz_bitmap & CHKBIT(psta->aid))) {
			struct list_head *xmitframe_phead;
			struct xmit_frame *pxmitframe;
			struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

			spin_lock_bh(&pxmitpriv->lock);

			xmitframe_phead = get_list_head(&psta->sleep_q);
			pxmitframe = list_first_entry_or_null(xmitframe_phead,
							      struct xmit_frame,
							      list);
			if (pxmitframe) {
				list_del_init(&pxmitframe->list);

				psta->sleepq_len--;

				if (psta->sleepq_len>0)
					pxmitframe->attrib.mdata = 1;
				else
					pxmitframe->attrib.mdata = 0;

				pxmitframe->attrib.triggered = 1;

				rtl8723au_hal_xmitframe_enqueue(padapter,
								pxmitframe);

				if (psta->sleepq_len == 0) {
					pstapriv->tim_bitmap &= ~CHKBIT(psta->aid);
					update_beacon23a(padapter, WLAN_EID_TIM,
							 NULL, false);
				}

				spin_unlock_bh(&pxmitpriv->lock);

			} else {
				spin_unlock_bh(&pxmitpriv->lock);

				if (pstapriv->tim_bitmap & CHKBIT(psta->aid)) {
					if (psta->sleepq_len == 0) {
						DBG_8723A("no buffered packets "
							  "to xmit\n");

						/* issue nulldata with More data bit = 0 to indicate we have no buffered packets */
						issue_nulldata23a(padapter,
							       psta->hwaddr,
							       0, 0, 0);
					} else {
						DBG_8723A("error!psta->sleepq"
							  "_len =%d\n",
							  psta->sleepq_len);
						psta->sleepq_len = 0;
					}

					pstapriv->tim_bitmap &= ~CHKBIT(psta->aid);

					update_beacon23a(padapter, WLAN_EID_TIM,
							 NULL, false);
				}
			}
		}
	}

#endif
	return _FAIL;
}

struct recv_frame *recvframe_chk_defrag23a(struct rtw_adapter *padapter,
					struct recv_frame *precv_frame);
static int validate_recv_mgnt_frame(struct rtw_adapter *padapter,
				    struct recv_frame *precv_frame)
{
	struct sta_info *psta;
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;

	RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
		 "+validate_recv_mgnt_frame\n");

	precv_frame = recvframe_chk_defrag23a(padapter, precv_frame);
	if (precv_frame == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
			 "%s: fragment packet\n", __func__);
		return _SUCCESS;
	}

	skb = precv_frame->pkt;
	hdr = (struct ieee80211_hdr *) skb->data;

		/* for rx pkt statistics */
	psta = rtw_get_stainfo23a(&padapter->stapriv, hdr->addr2);
	if (psta) {
		psta->sta_stats.rx_mgnt_pkts++;

		if (ieee80211_is_beacon(hdr->frame_control))
			psta->sta_stats.rx_beacon_pkts++;
		else if (ieee80211_is_probe_req(hdr->frame_control))
			psta->sta_stats.rx_probereq_pkts++;
		else if (ieee80211_is_probe_resp(hdr->frame_control)) {
			if (ether_addr_equal(padapter->eeprompriv.mac_addr,
				    hdr->addr1))
				psta->sta_stats.rx_probersp_pkts++;
			else if (is_broadcast_ether_addr(hdr->addr1) ||
				 is_multicast_ether_addr(hdr->addr1))
				psta->sta_stats.rx_probersp_bm_pkts++;
			else
				psta->sta_stats.rx_probersp_uo_pkts++;
		}
	}

	mgt_dispatcher23a(padapter, precv_frame);

	return _SUCCESS;
}

static int validate_recv_data_frame(struct rtw_adapter *adapter,
				    struct recv_frame *precv_frame)
{
	u8 bretry;
	u8 *psa, *pda;
	struct sta_info *psta = NULL;
	struct rx_pkt_attrib *pattrib = & precv_frame->attrib;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	int ret = _SUCCESS;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;



	bretry = ieee80211_has_retry(hdr->frame_control);
	pda = ieee80211_get_DA(hdr);
	psa = ieee80211_get_SA(hdr);

	ether_addr_copy(pattrib->dst, pda);
	ether_addr_copy(pattrib->src, psa);

	switch (hdr->frame_control &
		cpu_to_le16(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) {
	case cpu_to_le16(0):
		ether_addr_copy(pattrib->bssid, hdr->addr3);
		ether_addr_copy(pattrib->ra, pda);
		ether_addr_copy(pattrib->ta, psa);
		ret = sta2sta_data_frame(adapter, precv_frame, &psta);
		break;

	case cpu_to_le16(IEEE80211_FCTL_FROMDS):
		ether_addr_copy(pattrib->bssid, hdr->addr2);
		ether_addr_copy(pattrib->ra, pda);
		ether_addr_copy(pattrib->ta, hdr->addr2);
		ret = ap2sta_data_frame(adapter, precv_frame, &psta);
		break;

	case cpu_to_le16(IEEE80211_FCTL_TODS):
		ether_addr_copy(pattrib->bssid, hdr->addr1);
		ether_addr_copy(pattrib->ra, hdr->addr1);
		ether_addr_copy(pattrib->ta, psa);
		ret = sta2ap_data_frame(adapter, precv_frame, &psta);
		break;

	case cpu_to_le16(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS):
		/*
		 * There is no BSSID in this case, but the driver has been
		 * using addr1 so far, so keep it for now.
		 */
		ether_addr_copy(pattrib->bssid, hdr->addr1);
		ether_addr_copy(pattrib->ra, hdr->addr1);
		ether_addr_copy(pattrib->ta, hdr->addr2);
		ret = _FAIL;
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, "case 3\n");
		break;
	}

	if ((ret == _FAIL) || (ret == RTW_RX_HANDLED))
		goto exit;

	if (!psta) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
			 "after to_fr_ds_chk; psta == NULL\n");
		ret = _FAIL;
		goto exit;
	}

	precv_frame->psta = psta;

	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	if (ieee80211_has_a4(hdr->frame_control))
		pattrib->hdrlen += ETH_ALEN;

	/* parsing QC field */
	if (pattrib->qos == 1) {
		__le16 *qptr = (__le16 *)ieee80211_get_qos_ctl(hdr);
		u16 qos_ctrl = le16_to_cpu(*qptr);

		pattrib->priority = qos_ctrl & IEEE80211_QOS_CTL_TID_MASK;
		pattrib->ack_policy = (qos_ctrl >> 5) & 3;
		pattrib->amsdu =
			(qos_ctrl & IEEE80211_QOS_CTL_A_MSDU_PRESENT) >> 7;
		pattrib->hdrlen += IEEE80211_QOS_CTL_LEN;

		if (pattrib->priority != 0 && pattrib->priority != 3) {
			adapter->recvpriv.bIsAnyNonBEPkts = true;
		}
	} else {
		pattrib->priority = 0;
		pattrib->ack_policy = 0;
		pattrib->amsdu = 0;
	}

	if (pattrib->order) { /* HT-CTRL 11n */
		pattrib->hdrlen += 4;
	}

	precv_frame->preorder_ctrl = &psta->recvreorder_ctrl[pattrib->priority];

	/*  decache, drop duplicate recv packets */
	if (recv_decache(precv_frame, bretry, &psta->sta_recvpriv.rxcache) ==
	    _FAIL) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
			 "decache : drop pkt\n");
		ret = _FAIL;
		goto exit;
	}

	if (pattrib->privacy) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 "validate_recv_data_frame:pattrib->privacy =%x\n",
			 pattrib->privacy);
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 "^^^^^^^^^^^is_multicast_ether_addr(pattrib->ra(0x%02x)) =%d^^^^^^^^^^^^^^^6\n",
			 pattrib->ra[0],
			 is_multicast_ether_addr(pattrib->ra));

		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt,
			       is_multicast_ether_addr(pattrib->ra));

		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 "pattrib->encrypt =%d\n", pattrib->encrypt);

		switch (pattrib->encrypt) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			pattrib->iv_len = IEEE80211_WEP_IV_LEN;
			pattrib->icv_len = IEEE80211_WEP_ICV_LEN;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			pattrib->iv_len = IEEE80211_TKIP_IV_LEN;
			pattrib->icv_len = IEEE80211_TKIP_ICV_LEN;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			pattrib->iv_len = IEEE80211_CCMP_HDR_LEN;
			pattrib->icv_len = IEEE80211_CCMP_MIC_LEN;
			break;
		default:
			pattrib->iv_len = 0;
			pattrib->icv_len = 0;
			break;
		}
	} else {
		pattrib->encrypt = 0;
		pattrib->iv_len = 0;
		pattrib->icv_len = 0;
	}

exit:



	return ret;
}

static void dump_rx_pkt(struct sk_buff *skb, u16 type, int level)
{
	int i;
	u8 *ptr;

	if ((level == 1) ||
	    ((level == 2) && (type == IEEE80211_FTYPE_MGMT)) ||
	    ((level == 3) && (type == IEEE80211_FTYPE_DATA))) {

		ptr = skb->data;

		DBG_8723A("#############################\n");

		for (i = 0; i < 64; i = i + 8)
			DBG_8723A("%*phC:\n", 8, ptr + i);
		DBG_8723A("#############################\n");
	}
}

static int validate_recv_frame(struct rtw_adapter *adapter,
			       struct recv_frame *precv_frame)
{
	/* shall check frame subtype, to / from ds, da, bssid */

	/* then call check if rx seq/frag. duplicated. */
	u8 type;
	u8 subtype;
	int retval = _SUCCESS;
	struct rx_pkt_attrib *pattrib = & precv_frame->attrib;
	struct sk_buff *skb = precv_frame->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 ver;
	u8 bDumpRxPkt;
	u16 seq_ctrl, fctl;

	fctl = le16_to_cpu(hdr->frame_control);
	ver = fctl & IEEE80211_FCTL_VERS;
	type = fctl & IEEE80211_FCTL_FTYPE;
	subtype = fctl & IEEE80211_FCTL_STYPE;

	/* add version chk */
	if (ver != 0) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
			 "validate_recv_data_frame fail! (ver!= 0)\n");
		retval = _FAIL;
		goto exit;
	}

	seq_ctrl = le16_to_cpu(hdr->seq_ctrl);
	pattrib->frag_num = seq_ctrl & IEEE80211_SCTL_FRAG;
	pattrib->seq_num = seq_ctrl >> 4;

	pattrib->pw_save = ieee80211_has_pm(hdr->frame_control);
	pattrib->mfrag = ieee80211_has_morefrags(hdr->frame_control);
	pattrib->mdata = ieee80211_has_moredata(hdr->frame_control);
	pattrib->privacy = ieee80211_has_protected(hdr->frame_control);
	pattrib->order = ieee80211_has_order(hdr->frame_control);

	GetHalDefVar8192CUsb(adapter, HAL_DEF_DBG_DUMP_RXPKT, &bDumpRxPkt);

	if (unlikely(bDumpRxPkt == 1))
		dump_rx_pkt(skb, type, bDumpRxPkt);

	switch (type) {
	case IEEE80211_FTYPE_MGMT:
		retval = validate_recv_mgnt_frame(adapter, precv_frame);
		if (retval == _FAIL) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "validate_recv_mgnt_frame fail\n");
		}
		retval = _FAIL; /*  only data frame return _SUCCESS */
		break;
	case IEEE80211_FTYPE_CTL:
		retval = validate_recv_ctrl_frame(adapter, precv_frame);
		if (retval == _FAIL) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "validate_recv_ctrl_frame fail\n");
		}
		retval = _FAIL; /*  only data frame return _SUCCESS */
		break;
	case IEEE80211_FTYPE_DATA:
		pattrib->qos = (subtype & IEEE80211_STYPE_QOS_DATA) ? 1 : 0;
		retval = validate_recv_data_frame(adapter, precv_frame);
		if (retval == _FAIL) {
			struct recv_priv *precvpriv = &adapter->recvpriv;

			precvpriv->rx_drop++;
		}
		break;
	default:
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
			 "validate_recv_data_frame fail! type = 0x%x\n", type);
		retval = _FAIL;
		break;
	}

exit:
	return retval;
}

/* remove the wlanhdr and add the eth_hdr */

static int wlanhdr_to_ethhdr (struct recv_frame *precvframe)
{
	u16	eth_type, len, hdrlen;
	u8	bsnaphdr;
	u8	*psnap;
	struct rtw_adapter *adapter = precvframe->adapter;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	struct sk_buff *skb = precvframe->pkt;
	u8 *ptr;
	struct rx_pkt_attrib *pattrib = &precvframe->attrib;



	ptr = skb->data;
	hdrlen = pattrib->hdrlen;
	psnap = ptr + hdrlen;
	eth_type = (psnap[6] << 8) | psnap[7];
	/* convert hdr + possible LLC headers into Ethernet header */
	if ((ether_addr_equal(psnap, rfc1042_header) &&
	     eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
	    ether_addr_equal(psnap, bridge_tunnel_header)) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation
		   and replace EtherType */
		bsnaphdr = true;
		hdrlen += SNAP_SIZE;
	} else {
		/* Leave Ethernet header part of hdr and full payload */
		bsnaphdr = false;
		eth_type = (psnap[0] << 8) | psnap[1];
	}

	len = skb->len - hdrlen;

	RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
		 "=== pattrib->hdrlen: %x,  pattrib->iv_len:%x ===\n",
		 pattrib->hdrlen,  pattrib->iv_len);

	pattrib->eth_type = eth_type;
	if (check_fwstate(pmlmepriv, WIFI_MP_STATE)) {
		ptr += hdrlen;
		*ptr = 0x87;
		*(ptr + 1) = 0x12;

		eth_type = 0x8712;
		/*  append rx status for mp test packets */

		ptr = skb_pull(skb, (hdrlen - sizeof(struct ethhdr) + 2) - 24);
		memcpy(ptr, skb->head, 24);
		ptr += 24;
	} else {
		ptr = skb_pull(skb, (hdrlen - sizeof(struct ethhdr) +
				     (bsnaphdr ? 2:0)));
	}

	ether_addr_copy(ptr, pattrib->dst);
	ether_addr_copy(ptr + ETH_ALEN, pattrib->src);

	if (!bsnaphdr) {
		put_unaligned_be16(len, ptr + 12);
	}


	return _SUCCESS;
}

/* perform defrag */
struct recv_frame *recvframe_defrag(struct rtw_adapter *adapter,
				    struct rtw_queue *defrag_q);
struct recv_frame *recvframe_defrag(struct rtw_adapter *adapter,
				    struct rtw_queue *defrag_q)
{
	struct list_head *phead;
	u8 wlanhdr_offset;
	u8 curfragnum;
	struct recv_frame *pnfhdr, *ptmp;
	struct recv_frame *prframe, *pnextrframe;
	struct rtw_queue *pfree_recv_queue;
	struct sk_buff *skb;

	curfragnum = 0;
	pfree_recv_queue = &adapter->recvpriv.free_recv_queue;

	phead = get_list_head(defrag_q);
	prframe = list_first_entry(phead, struct recv_frame, list);
	list_del_init(&prframe->list);
	skb = prframe->pkt;

	if (curfragnum != prframe->attrib.frag_num) {
		/* the first fragment number must be 0 */
		/* free the whole queue */
		rtw_free_recvframe23a(prframe);
		rtw_free_recvframe23a_queue(defrag_q);

		return NULL;
	}

	curfragnum++;

	list_for_each_entry_safe(pnfhdr, ptmp, phead, list) {
		pnextrframe = (struct recv_frame *)pnfhdr;
		/* check the fragment sequence  (2nd ~n fragment frame) */

		if (curfragnum != pnfhdr->attrib.frag_num) {
			/* the fragment number must be increasing
			   (after decache) */
			/* release the defrag_q & prframe */
			rtw_free_recvframe23a(prframe);
			rtw_free_recvframe23a_queue(defrag_q);
			return NULL;
		}

		curfragnum++;

		/* copy the 2nd~n fragment frame's payload to the
		   first fragment */
		/* get the 2nd~last fragment frame's payload */

		wlanhdr_offset = pnfhdr->attrib.hdrlen + pnfhdr->attrib.iv_len;

		skb_pull(pnfhdr->pkt, wlanhdr_offset);

		/* append  to first fragment frame's tail
		   (if privacy frame, pull the ICV) */

		skb_trim(skb, skb->len - prframe->attrib.icv_len);

		memcpy(skb_tail_pointer(skb), pnfhdr->pkt->data,
		       pnfhdr->pkt->len);

		skb_put(skb, pnfhdr->pkt->len);

		prframe->attrib.icv_len = pnfhdr->attrib.icv_len;
	}

	/* free the defrag_q queue and return the prframe */
	rtw_free_recvframe23a_queue(defrag_q);

	RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
		 "Performance defrag!!!!!\n");

	return prframe;
}

/* check if need to defrag, if needed queue the frame to defrag_q */
struct recv_frame *recvframe_chk_defrag23a(struct rtw_adapter *padapter,
					struct recv_frame *precv_frame)
{
	u8	ismfrag;
	u8	fragnum;
	u8	*psta_addr;
	struct recv_frame *pfhdr;
	struct sta_info *psta;
	struct sta_priv *pstapriv;
	struct list_head *phead;
	struct recv_frame *prtnframe = NULL;
	struct rtw_queue *pfree_recv_queue, *pdefrag_q;



	pstapriv = &padapter->stapriv;

	pfhdr = precv_frame;

	pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

	/* need to define struct of wlan header frame ctrl */
	ismfrag = pfhdr->attrib.mfrag;
	fragnum = pfhdr->attrib.frag_num;

	psta_addr = pfhdr->attrib.ta;
	psta = rtw_get_stainfo23a(pstapriv, psta_addr);
	if (!psta) {
		struct ieee80211_hdr *hdr =
			(struct ieee80211_hdr *) pfhdr->pkt->data;
		if (!ieee80211_is_data(hdr->frame_control)) {
			psta = rtw_get_bcmc_stainfo23a(padapter);
			pdefrag_q = &psta->sta_recvpriv.defrag_q;
		} else
			pdefrag_q = NULL;
	} else
		pdefrag_q = &psta->sta_recvpriv.defrag_q;

	if ((ismfrag == 0) && (fragnum == 0)) {
		prtnframe = precv_frame;/* isn't a fragment frame */
	}

	if (ismfrag == 1) {
		/* 0~(n-1) fragment frame */
		/* enqueue to defraf_g */
		if (pdefrag_q != NULL) {
			if (fragnum == 0) {
				/* the first fragment */
				if (!list_empty(&pdefrag_q->queue)) {
					/* free current defrag_q */
					rtw_free_recvframe23a_queue(pdefrag_q);
				}
			}

			/* Then enqueue the 0~(n-1) fragment into the
			   defrag_q */

			/* spin_lock(&pdefrag_q->lock); */
			phead = get_list_head(pdefrag_q);
			list_add_tail(&pfhdr->list, phead);
			/* spin_unlock(&pdefrag_q->lock); */

			RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
				 "Enqueuq: ismfrag = %d, fragnum = %d\n",
				 ismfrag, fragnum);

			prtnframe = NULL;

		} else {
			/* can't find this ta's defrag_queue,
			   so free this recv_frame */
			rtw_free_recvframe23a(precv_frame);
			prtnframe = NULL;
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "Free because pdefrag_q == NULL: ismfrag = %d, fragnum = %d\n",
				 ismfrag, fragnum);
		}
	}

	if ((ismfrag == 0) && (fragnum != 0)) {
		/* the last fragment frame */
		/* enqueue the last fragment */
		if (pdefrag_q != NULL) {
			/* spin_lock(&pdefrag_q->lock); */
			phead = get_list_head(pdefrag_q);
			list_add_tail(&pfhdr->list, phead);
			/* spin_unlock(&pdefrag_q->lock); */

			/* call recvframe_defrag to defrag */
			RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
				 "defrag: ismfrag = %d, fragnum = %d\n",
				 ismfrag, fragnum);
			precv_frame = recvframe_defrag(padapter, pdefrag_q);
			prtnframe = precv_frame;
		} else {
			/* can't find this ta's defrag_queue,
			   so free this recv_frame */
			rtw_free_recvframe23a(precv_frame);
			prtnframe = NULL;
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "Free because pdefrag_q == NULL: ismfrag = %d, fragnum = %d\n",
				 ismfrag, fragnum);
		}

	}

	if ((prtnframe != NULL) && (prtnframe->attrib.privacy))	{
		/* after defrag we must check tkip mic code */
		if (recvframe_chkmic(padapter,  prtnframe) == _FAIL) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "recvframe_chkmic(padapter,  prtnframe) ==_FAIL\n");
			rtw_free_recvframe23a(prtnframe);
			prtnframe = NULL;
		}
	}



	return prtnframe;
}

int amsdu_to_msdu(struct rtw_adapter *padapter, struct recv_frame *prframe);
int amsdu_to_msdu(struct rtw_adapter *padapter, struct recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib;
	struct sk_buff *skb, *sub_skb;
	struct sk_buff_head skb_list;

	pattrib = &prframe->attrib;

	skb = prframe->pkt;
	skb_pull(skb, prframe->attrib.hdrlen);
	__skb_queue_head_init(&skb_list);

	ieee80211_amsdu_to_8023s(skb, &skb_list, NULL, 0, 0, false);

	while (!skb_queue_empty(&skb_list)) {
		sub_skb = __skb_dequeue(&skb_list);

		sub_skb->protocol = eth_type_trans(sub_skb, padapter->pnetdev);
		sub_skb->dev = padapter->pnetdev;

		sub_skb->ip_summed = CHECKSUM_NONE;

		netif_rx(sub_skb);
	}

	prframe->pkt = NULL;
	rtw_free_recvframe23a(prframe);
	return _SUCCESS;
}

int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, u16 seq_num);
int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, u16 seq_num)
{
	u8	wsize = preorder_ctrl->wsize_b;
	u16	wend = (preorder_ctrl->indicate_seq + wsize -1) & 0xFFF;

	/*  Rx Reorder initialize condition. */
	if (preorder_ctrl->indicate_seq == 0xFFFF)
		preorder_ctrl->indicate_seq = seq_num;

	/*  Drop out the packet which SeqNum is smaller than WinStart */
	if (SN_LESS(seq_num, preorder_ctrl->indicate_seq))
		return false;

	/*  */
	/*  Sliding window manipulation. Conditions includes: */
	/*  1. Incoming SeqNum is equal to WinStart =>Window shift 1 */
	/*  2. Incoming SeqNum is larger than the WinEnd => Window shift N */
	/*  */
	if (SN_EQUAL(seq_num, preorder_ctrl->indicate_seq)) {
		preorder_ctrl->indicate_seq =
			(preorder_ctrl->indicate_seq + 1) & 0xFFF;
	} else if (SN_LESS(wend, seq_num)) {
		/*  boundary situation, when seq_num cross 0xFFF */
		if (seq_num >= (wsize - 1))
			preorder_ctrl->indicate_seq = seq_num + 1 -wsize;
		else
			preorder_ctrl->indicate_seq = 0xFFF - (wsize - (seq_num + 1)) + 1;
	}
	return true;
}

static int enqueue_reorder_recvframe23a(struct recv_reorder_ctrl *preorder_ctrl,
					struct recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib = &prframe->attrib;
	struct rtw_queue *ppending_recvframe_queue;
	struct list_head *phead, *plist, *ptmp;
	struct recv_frame *hdr;
	struct rx_pkt_attrib *pnextattrib;

	ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	phead = get_list_head(ppending_recvframe_queue);

	list_for_each_safe(plist, ptmp, phead) {
		hdr = container_of(plist, struct recv_frame, list);
		pnextattrib = &hdr->attrib;

		if (SN_LESS(pnextattrib->seq_num, pattrib->seq_num)) {
			continue;
		} else if (SN_EQUAL(pnextattrib->seq_num, pattrib->seq_num)) {
			/* Duplicate entry is found!! Do not insert current entry. */
			return false;
		} else {
			break;
		}

	}

	list_del_init(&prframe->list);

	list_add_tail(&prframe->list, plist);

	return true;
}

int recv_indicatepkts_in_order(struct rtw_adapter *padapter,
			       struct recv_reorder_ctrl *preorder_ctrl,
			       int bforced);
int recv_indicatepkts_in_order(struct rtw_adapter *padapter,
			       struct recv_reorder_ctrl *preorder_ctrl,
			       int bforced)
{
	struct list_head *phead, *plist;
	struct recv_frame *prframe;
	struct rx_pkt_attrib *pattrib;
	int bPktInBuf = false;
	struct recv_priv *precvpriv;
	struct rtw_queue *ppending_recvframe_queue;

	precvpriv = &padapter->recvpriv;
	ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	phead =	get_list_head(ppending_recvframe_queue);
	plist = phead->next;

	/*  Handling some condition for forced indicate case. */
	if (bforced) {
		if (list_empty(phead)) {
			return true;
		}

		prframe = container_of(plist, struct recv_frame, list);
		pattrib = &prframe->attrib;
		preorder_ctrl->indicate_seq = pattrib->seq_num;
	}

	/*  Prepare indication list and indication. */
	/*  Check if there is any packet need indicate. */
	while (!list_empty(phead)) {

		prframe = container_of(plist, struct recv_frame, list);
		pattrib = &prframe->attrib;

		if (!SN_LESS(preorder_ctrl->indicate_seq, pattrib->seq_num)) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
				 "recv_indicatepkts_in_order: indicate =%d seq =%d amsdu =%d\n",
				 preorder_ctrl->indicate_seq,
				 pattrib->seq_num, pattrib->amsdu);

			plist = plist->next;
			list_del_init(&prframe->list);

			if (SN_EQUAL(preorder_ctrl->indicate_seq,
				     pattrib->seq_num))	{
				preorder_ctrl->indicate_seq =
					(preorder_ctrl->indicate_seq + 1)&0xFFF;
			}

			if (!pattrib->amsdu) {
				if ((padapter->bDriverStopped == false) &&
				    (padapter->bSurpriseRemoved == false)) {
					rtw_recv_indicatepkt23a(padapter, prframe);
				}
			} else {
				if (amsdu_to_msdu(padapter, prframe) !=
				    _SUCCESS)
					rtw_free_recvframe23a(prframe);
			}

			/* Update local variables. */
			bPktInBuf = false;

		} else {
			bPktInBuf = true;
			break;
		}

	}

	return bPktInBuf;
}

int recv_indicatepkt_reorder(struct rtw_adapter *padapter,
			     struct recv_frame *prframe);
int recv_indicatepkt_reorder(struct rtw_adapter *padapter,
			     struct recv_frame *prframe)
{
	int retval = _SUCCESS;
	struct rx_pkt_attrib *pattrib;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct rtw_queue *ppending_recvframe_queue;

	pattrib = &prframe->attrib;
	preorder_ctrl = prframe->preorder_ctrl;
	ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

	if (!pattrib->amsdu) {
		/* s1. */
		wlanhdr_to_ethhdr(prframe);

		if ((pattrib->qos!= 1) || (pattrib->eth_type == ETH_P_ARP) ||
		    (pattrib->ack_policy != 0)) {
			if ((padapter->bDriverStopped == false) &&
			    (padapter->bSurpriseRemoved == false)) {
				RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
					 "@@@@  recv_indicatepkt_reorder -recv_func recv_indicatepkt\n");

				rtw_recv_indicatepkt23a(padapter, prframe);
				return _SUCCESS;
			}

			return _FAIL;
		}

		if (preorder_ctrl->enable == false) {
			/* indicate this recv_frame */
			preorder_ctrl->indicate_seq = pattrib->seq_num;
			rtw_recv_indicatepkt23a(padapter, prframe);

			preorder_ctrl->indicate_seq =
				(preorder_ctrl->indicate_seq + 1) % 4096;
			return _SUCCESS;
		}
	} else {
		 /* temp filter -> means didn't support A-MSDUs in a A-MPDU */
		if (preorder_ctrl->enable == false) {
			preorder_ctrl->indicate_seq = pattrib->seq_num;
			retval = amsdu_to_msdu(padapter, prframe);

			preorder_ctrl->indicate_seq =
				(preorder_ctrl->indicate_seq + 1) % 4096;
			return retval;
		}
	}

	spin_lock_bh(&ppending_recvframe_queue->lock);

	RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
		 "recv_indicatepkt_reorder: indicate =%d seq =%d\n",
		 preorder_ctrl->indicate_seq, pattrib->seq_num);

	/* s2. check if winstart_b(indicate_seq) needs to been updated */
	if (!check_indicate_seq(preorder_ctrl, pattrib->seq_num)) {
		goto _err_exit;
	}

	/* s3. Insert all packet into Reorder Queue to maintain its ordering. */
	if (!enqueue_reorder_recvframe23a(preorder_ctrl, prframe)) {
		goto _err_exit;
	}

	/* s4. */
	/*  Indication process. */
	/*  After Packet dropping and Sliding Window shifting as above,
	    we can now just indicate the packets */
	/*  with the SeqNum smaller than latest WinStart and buffer
	    other packets. */
	/*  */
	/*  For Rx Reorder condition: */
	/*  1. All packets with SeqNum smaller than WinStart => Indicate */
	/*  2. All packets with SeqNum larger than or equal to WinStart =>
	    Buffer it. */
	/*  */

	if (recv_indicatepkts_in_order(padapter, preorder_ctrl, false) == true) {
		mod_timer(&preorder_ctrl->reordering_ctrl_timer,
			  jiffies + msecs_to_jiffies(REORDER_WAIT_TIME));
		spin_unlock_bh(&ppending_recvframe_queue->lock);
	} else {
		spin_unlock_bh(&ppending_recvframe_queue->lock);
		del_timer_sync(&preorder_ctrl->reordering_ctrl_timer);
	}
	return _SUCCESS;

_err_exit:

	spin_unlock_bh(&ppending_recvframe_queue->lock);
	return _FAIL;
}

void rtw_reordering_ctrl_timeout_handler23a(unsigned long pcontext)
{
	struct recv_reorder_ctrl *preorder_ctrl;
	struct rtw_adapter *padapter;
	struct rtw_queue *ppending_recvframe_queue;

	preorder_ctrl = (struct recv_reorder_ctrl *)pcontext;
	padapter = preorder_ctrl->padapter;
	ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

	if (padapter->bDriverStopped || padapter->bSurpriseRemoved) {
		return;
	}

	spin_lock_bh(&ppending_recvframe_queue->lock);

	if (recv_indicatepkts_in_order(padapter, preorder_ctrl, true) == true) {
		mod_timer(&preorder_ctrl->reordering_ctrl_timer,
			  jiffies + msecs_to_jiffies(REORDER_WAIT_TIME));
	}

	spin_unlock_bh(&ppending_recvframe_queue->lock);
}

int process_recv_indicatepkts(struct rtw_adapter *padapter,
			      struct recv_frame *prframe);
int process_recv_indicatepkts(struct rtw_adapter *padapter,
			      struct recv_frame *prframe)
{
	int retval = _SUCCESS;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;

	if (phtpriv->ht_option == true) { /* B/G/N Mode */
		/*  including perform A-MPDU Rx Ordering Buffer Control */
		if (recv_indicatepkt_reorder(padapter, prframe) != _SUCCESS) {
			if ((padapter->bDriverStopped == false) &&
			    (padapter->bSurpriseRemoved == false)) {
				retval = _FAIL;
				return retval;
			}
		}
	} else { /* B/G mode */
		retval = wlanhdr_to_ethhdr(prframe);
		if (retval != _SUCCESS) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 "wlanhdr_to_ethhdr: drop pkt\n");
			return retval;
		}

		if ((padapter->bDriverStopped == false) &&
		    (padapter->bSurpriseRemoved == false)) {
			/* indicate this recv_frame */
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
				 "@@@@ process_recv_indicatepkts- recv_func recv_indicatepkt\n");
			rtw_recv_indicatepkt23a(padapter, prframe);
		} else {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
				 "@@@@ process_recv_indicatepkts- recv_func free_indicatepkt\n");

			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
				 "recv_func:bDriverStopped(%d) OR bSurpriseRemoved(%d)\n",
				 padapter->bDriverStopped,
				 padapter->bSurpriseRemoved);
			retval = _FAIL;
			return retval;
		}

	}

	return retval;
}

static int recv_func_prehandle(struct rtw_adapter *padapter,
			       struct recv_frame *rframe)
{
	int ret;

	/* check the frame crtl field and decache */
	ret = validate_recv_frame(padapter, rframe);
	if (ret != _SUCCESS) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 "recv_func: validate_recv_frame fail! drop pkt\n");
		rtw_free_recvframe23a(rframe);
		goto exit;
	}

exit:
	return ret;
}

static int recv_func_posthandle(struct rtw_adapter *padapter,
				struct recv_frame *prframe)
{
	int ret = _SUCCESS;
	struct recv_frame *orig_prframe = prframe;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	/*  DATA FRAME */
	prframe = decryptor(padapter, prframe);
	if (prframe == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
			 "decryptor: drop pkt\n");
		ret = _FAIL;
		goto _recv_data_drop;
	}

	prframe = recvframe_chk_defrag23a(padapter, prframe);
	if (!prframe) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
			 "recvframe_chk_defrag23a: drop pkt\n");
		goto _recv_data_drop;
	}

	/*
	 * Pull off crypto headers
	 */
	if (prframe->attrib.iv_len > 0) {
		skb_pull(prframe->pkt, prframe->attrib.iv_len);
	}

	if (prframe->attrib.icv_len > 0) {
		skb_trim(prframe->pkt,
			 prframe->pkt->len - prframe->attrib.icv_len);
	}

	prframe = portctrl(padapter, prframe);
	if (!prframe) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
			 "portctrl: drop pkt\n");
		ret = _FAIL;
		goto _recv_data_drop;
	}

	count_rx_stats(padapter, prframe, NULL);

	ret = process_recv_indicatepkts(padapter, prframe);
	if (ret != _SUCCESS) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
			 "recv_func: process_recv_indicatepkts fail!\n");
		rtw_free_recvframe23a(orig_prframe);/* free this recv_frame */
		goto _recv_data_drop;
	}
	return ret;

_recv_data_drop:
	precvpriv->rx_drop++;
	return ret;
}

int rtw_recv_entry23a(struct recv_frame *rframe)
{
	int ret, r;
	struct rtw_adapter *padapter = rframe->adapter;
	struct rx_pkt_attrib *prxattrib = &rframe->attrib;
	struct recv_priv *recvpriv = &padapter->recvpriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;

	/* check if need to handle uc_swdec_pending_queue*/
	if (check_fwstate(mlmepriv, WIFI_STATION_STATE) &&
	    psecuritypriv->busetkipkey)	{
		struct recv_frame *pending_frame;

		while ((pending_frame = rtw_alloc_recvframe23a(&padapter->recvpriv.uc_swdec_pending_queue))) {
			r = recv_func_posthandle(padapter, pending_frame);
			if (r == _SUCCESS)
				DBG_8723A("%s: dequeue uc_swdec_pending_queue\n", __func__);
		}
	}

	ret = recv_func_prehandle(padapter, rframe);

	if (ret == _SUCCESS) {
		/* check if need to enqueue into uc_swdec_pending_queue*/
		if (check_fwstate(mlmepriv, WIFI_STATION_STATE) &&
		    !is_multicast_ether_addr(prxattrib->ra) &&
		    prxattrib->encrypt > 0 &&
		    (prxattrib->bdecrypted == 0) &&
		    !is_wep_enc(psecuritypriv->dot11PrivacyAlgrthm) &&
		    !psecuritypriv->busetkipkey) {
			rtw_enqueue_recvframe23a(rframe, &padapter->recvpriv.uc_swdec_pending_queue);
			DBG_8723A("%s: no key, enqueue uc_swdec_pending_queue\n", __func__);
			goto exit;
		}

		ret = recv_func_posthandle(padapter, rframe);

		recvpriv->rx_pkts++;
	}

exit:
	return ret;
}

void rtw_signal_stat_timer_hdl23a(unsigned long data)
{
	struct rtw_adapter *adapter = (struct rtw_adapter *)data;
	struct recv_priv *recvpriv = &adapter->recvpriv;

	u32 tmp_s, tmp_q;
	u8 avg_signal_strength = 0;
	u8 avg_signal_qual = 0;
	u32 num_signal_strength = 0;
	u32 num_signal_qual = 0;
	u8 _alpha = 3;	/* this value is based on converging_constant = 5000 */
			/* and sampling_interval = 1000 */

	if (recvpriv->signal_strength_data.update_req == 0) {
		/*  update_req is clear, means we got rx */
		avg_signal_strength = recvpriv->signal_strength_data.avg_val;
		num_signal_strength = recvpriv->signal_strength_data.total_num;
		/*  after avg_vals are acquired, we can re-stat */
		/* the signal values */
		recvpriv->signal_strength_data.update_req = 1;
	}

	if (recvpriv->signal_qual_data.update_req == 0) {
		/*  update_req is clear, means we got rx */
		avg_signal_qual = recvpriv->signal_qual_data.avg_val;
		num_signal_qual = recvpriv->signal_qual_data.total_num;
		/*  after avg_vals are acquired, we can re-stat */
		/*the signal values */
		recvpriv->signal_qual_data.update_req = 1;
	}

	/* update value of signal_strength, rssi, signal_qual */
	if (!check_fwstate(&adapter->mlmepriv, _FW_UNDER_SURVEY)) {
		tmp_s = avg_signal_strength + (_alpha - 1) *
			 recvpriv->signal_strength;
		if (tmp_s %_alpha)
			tmp_s = tmp_s / _alpha + 1;
		else
			tmp_s = tmp_s / _alpha;
		if (tmp_s > 100)
			tmp_s = 100;

		tmp_q = avg_signal_qual + (_alpha - 1) * recvpriv->signal_qual;
		if (tmp_q %_alpha)
			tmp_q = tmp_q / _alpha + 1;
		else
			tmp_q = tmp_q / _alpha;
		if (tmp_q > 100)
			tmp_q = 100;

		recvpriv->signal_strength = tmp_s;
		recvpriv->signal_qual = tmp_q;

		DBG_8723A("%s signal_strength:%3u, signal_qual:%3u, "
			  "num_signal_strength:%u, num_signal_qual:%u\n",
			  __func__, recvpriv->signal_strength,
			  recvpriv->signal_qual, num_signal_strength,
			  num_signal_qual);
	}

	rtw_set_signal_stat_timer(recvpriv);
}
