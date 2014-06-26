/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#define _RTW_STA_MGT_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <mlme_osdep.h>
#include <sta_info.h>
#include <rtl8723a_hal.h>

static u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static void _rtw_init_stainfo(struct sta_info *psta)
{
	memset((u8 *)psta, 0, sizeof (struct sta_info));
	spin_lock_init(&psta->lock);
	INIT_LIST_HEAD(&psta->list);
	INIT_LIST_HEAD(&psta->hash_list);
	_rtw_init_queue23a(&psta->sleep_q);
	psta->sleepq_len = 0;
	_rtw_init_sta_xmit_priv23a(&psta->sta_xmitpriv);
	_rtw_init_sta_recv_priv23a(&psta->sta_recvpriv);
#ifdef CONFIG_8723AU_AP_MODE
	INIT_LIST_HEAD(&psta->asoc_list);
	INIT_LIST_HEAD(&psta->auth_list);
	psta->expire_to = 0;
	psta->flags = 0;
	psta->capability = 0;
	psta->bpairwise_key_installed = false;
	psta->nonerp_set = 0;
	psta->no_short_slot_time_set = 0;
	psta->no_short_preamble_set = 0;
	psta->no_ht_gf_set = 0;
	psta->no_ht_set = 0;
	psta->ht_20mhz_set = 0;
	psta->keep_alive_trycnt = 0;
#endif	/*  CONFIG_8723AU_AP_MODE */
}

int _rtw_init_sta_priv23a(struct sta_priv *pstapriv)
{
	int i;

	spin_lock_init(&pstapriv->sta_hash_lock);
	pstapriv->asoc_sta_count = 0;
	for (i = 0; i < NUM_STA; i++)
		INIT_LIST_HEAD(&pstapriv->sta_hash[i]);

#ifdef CONFIG_8723AU_AP_MODE
	pstapriv->sta_dz_bitmap = 0;
	pstapriv->tim_bitmap = 0;
	INIT_LIST_HEAD(&pstapriv->asoc_list);
	INIT_LIST_HEAD(&pstapriv->auth_list);
	spin_lock_init(&pstapriv->asoc_list_lock);
	spin_lock_init(&pstapriv->auth_list_lock);
	pstapriv->asoc_list_cnt = 0;
	pstapriv->auth_list_cnt = 0;
	pstapriv->auth_to = 3; /*  3*2 = 6 sec */
	pstapriv->assoc_to = 3;
	/* pstapriv->expire_to = 900;  900*2 = 1800 sec = 30 min, expire after no any traffic. */
	/* pstapriv->expire_to = 30;  30*2 = 60 sec = 1 min, expire after no any traffic. */
	pstapriv->expire_to = 3; /*  3*2 = 6 sec */
	pstapriv->max_num_sta = NUM_STA;
#endif
	return _SUCCESS;
}

int _rtw_free_sta_priv23a(struct sta_priv *pstapriv)
{
	struct list_head *phead, *plist, *ptmp;
	struct sta_info *psta;
	struct recv_reorder_ctrl *preorder_ctrl;
	int index;

	if (pstapriv) {
		/*	delete all reordering_ctrl_timer		*/
		spin_lock_bh(&pstapriv->sta_hash_lock);
		for (index = 0; index < NUM_STA; index++) {
			phead = &pstapriv->sta_hash[index];

			list_for_each_safe(plist, ptmp, phead) {
				int i;
				psta = container_of(plist, struct sta_info,
						    hash_list);
				for (i = 0; i < 16 ; i++) {
					preorder_ctrl = &psta->recvreorder_ctrl[i];
					del_timer_sync(&preorder_ctrl->reordering_ctrl_timer);
				}
			}
		}
		spin_unlock_bh(&pstapriv->sta_hash_lock);
		/*===============================*/
	}
	return _SUCCESS;
}

struct sta_info *
rtw_alloc_stainfo23a(struct sta_priv *pstapriv, u8 *hwaddr, gfp_t gfp)
{
	struct list_head	*phash_list;
	struct sta_info	*psta;
	struct recv_reorder_ctrl *preorder_ctrl;
	s32	index;
	int i = 0;
	u16  wRxSeqInitialValue = 0xffff;

	psta = kmalloc(sizeof(struct sta_info), gfp);
	if (!psta)
		return NULL;

	spin_lock_bh(&pstapriv->sta_hash_lock);

	_rtw_init_stainfo(psta);

	psta->padapter = pstapriv->padapter;

	ether_addr_copy(psta->hwaddr, hwaddr);

	index = wifi_mac_hash(hwaddr);

	RT_TRACE(_module_rtl871x_sta_mgt_c_, _drv_info_,
		 ("rtw_alloc_stainfo23a: index  = %x", index));
	if (index >= NUM_STA) {
		RT_TRACE(_module_rtl871x_sta_mgt_c_, _drv_err_,
			 ("ERROR => rtw_alloc_stainfo23a: index >= NUM_STA"));
		psta = NULL;
		goto exit;
	}
	phash_list = &pstapriv->sta_hash[index];

	list_add_tail(&psta->hash_list, phash_list);

	pstapriv->asoc_sta_count ++ ;

/*  For the SMC router, the sequence number of first packet of WPS handshake will be 0. */
/*  In this case, this packet will be dropped by recv_decache function if we use the 0x00 as the default value for tid_rxseq variable. */
/*  So, we initialize the tid_rxseq variable as the 0xffff. */

	for (i = 0; i < 16; i++)
		memcpy(&psta->sta_recvpriv.rxcache.tid_rxseq[i], &wRxSeqInitialValue, 2);

	RT_TRACE(_module_rtl871x_sta_mgt_c_, _drv_info_,
		 ("alloc number_%d stainfo  with hwaddr = %pM\n",
		 pstapriv->asoc_sta_count, hwaddr));

	init_addba_retry_timer23a(psta);

	/* for A-MPDU Rx reordering buffer control */
	for (i = 0; i < 16; i++) {
		preorder_ctrl = &psta->recvreorder_ctrl[i];

		preorder_ctrl->padapter = pstapriv->padapter;

		preorder_ctrl->enable = false;

		preorder_ctrl->indicate_seq = 0xffff;
		preorder_ctrl->wend_b = 0xffff;
		/* preorder_ctrl->wsize_b = (NR_RECVBUFF-2); */
		preorder_ctrl->wsize_b = 64;/* 64; */

		_rtw_init_queue23a(&preorder_ctrl->pending_recvframe_queue);

		rtw_init_recv_timer23a(preorder_ctrl);
	}
	/* init for DM */
	psta->rssi_stat.UndecoratedSmoothedPWDB = (-1);
	psta->rssi_stat.UndecoratedSmoothedCCK = (-1);

	/* init for the sequence number of received management frame */
	psta->RxMgmtFrameSeqNum = 0xffff;
exit:
	spin_unlock_bh(&pstapriv->sta_hash_lock);
	return psta;
}

/*  using pstapriv->sta_hash_lock to protect */
int rtw_free_stainfo23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	struct recv_reorder_ctrl *preorder_ctrl;
	struct	sta_xmit_priv	*pstaxmitpriv;
	struct	xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct hw_xmit *phwxmit;
	int i;

	if (psta == NULL)
		goto exit;

	spin_lock_bh(&psta->lock);
	psta->state &= ~_FW_LINKED;
	spin_unlock_bh(&psta->lock);

	pstaxmitpriv = &psta->sta_xmitpriv;

	spin_lock_bh(&pxmitpriv->lock);

	rtw_free_xmitframe_queue23a(pxmitpriv, &psta->sleep_q);
	psta->sleepq_len = 0;

	/* vo */
	rtw_free_xmitframe_queue23a(pxmitpriv, &pstaxmitpriv->vo_q.sta_pending);
	list_del_init(&pstaxmitpriv->vo_q.tx_pending);
	phwxmit = pxmitpriv->hwxmits;
	phwxmit->accnt -= pstaxmitpriv->vo_q.qcnt;
	pstaxmitpriv->vo_q.qcnt = 0;

	/* vi */
	rtw_free_xmitframe_queue23a(pxmitpriv, &pstaxmitpriv->vi_q.sta_pending);
	list_del_init(&pstaxmitpriv->vi_q.tx_pending);
	phwxmit = pxmitpriv->hwxmits+1;
	phwxmit->accnt -= pstaxmitpriv->vi_q.qcnt;
	pstaxmitpriv->vi_q.qcnt = 0;

	/* be */
	rtw_free_xmitframe_queue23a(pxmitpriv, &pstaxmitpriv->be_q.sta_pending);
	list_del_init(&pstaxmitpriv->be_q.tx_pending);
	phwxmit = pxmitpriv->hwxmits+2;
	phwxmit->accnt -= pstaxmitpriv->be_q.qcnt;
	pstaxmitpriv->be_q.qcnt = 0;

	/* bk */
	rtw_free_xmitframe_queue23a(pxmitpriv, &pstaxmitpriv->bk_q.sta_pending);
	list_del_init(&pstaxmitpriv->bk_q.tx_pending);
	phwxmit = pxmitpriv->hwxmits+3;
	phwxmit->accnt -= pstaxmitpriv->bk_q.qcnt;
	pstaxmitpriv->bk_q.qcnt = 0;

	spin_unlock_bh(&pxmitpriv->lock);

	list_del_init(&psta->hash_list);
	RT_TRACE(_module_rtl871x_sta_mgt_c_, _drv_err_, ("\n free number_%d stainfo  with hwaddr = 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x \n", pstapriv->asoc_sta_count, psta->hwaddr[0], psta->hwaddr[1], psta->hwaddr[2], psta->hwaddr[3], psta->hwaddr[4], psta->hwaddr[5]));
	pstapriv->asoc_sta_count --;

	/*  re-init sta_info; 20061114  will be init in alloc_stainfo */
	/* _rtw_init_sta_xmit_priv23a(&psta->sta_xmitpriv); */
	/* _rtw_init_sta_recv_priv23a(&psta->sta_recvpriv); */

	del_timer_sync(&psta->addba_retry_timer);

	/* for A-MPDU Rx reordering buffer control, cancel reordering_ctrl_timer */
	for (i = 0; i < 16; i++) {
		struct list_head	*phead, *plist;
		struct recv_frame *prframe;
		struct rtw_queue *ppending_recvframe_queue;

		preorder_ctrl = &psta->recvreorder_ctrl[i];

		del_timer_sync(&preorder_ctrl->reordering_ctrl_timer);

		ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

		spin_lock_bh(&ppending_recvframe_queue->lock);
		phead =		get_list_head(ppending_recvframe_queue);
		plist = phead->next;

		while (!list_empty(phead)) {
			prframe = container_of(plist, struct recv_frame, list);
			plist = plist->next;
			list_del_init(&prframe->list);
			rtw_free_recvframe23a(prframe);
		}
		spin_unlock_bh(&ppending_recvframe_queue->lock);
	}
	if (!(psta->state & WIFI_AP_STATE))
		rtl8723a_SetHalODMVar(padapter, HAL_ODM_STA_INFO, psta, false);
#ifdef CONFIG_8723AU_AP_MODE
	spin_lock_bh(&pstapriv->auth_list_lock);
	if (!list_empty(&psta->auth_list)) {
		list_del_init(&psta->auth_list);
		pstapriv->auth_list_cnt--;
	}
	spin_unlock_bh(&pstapriv->auth_list_lock);

	psta->expire_to = 0;

	psta->sleepq_ac_len = 0;
	psta->qos_info = 0;

	psta->max_sp_len = 0;
	psta->uapsd_bk = 0;
	psta->uapsd_be = 0;
	psta->uapsd_vi = 0;
	psta->uapsd_vo = 0;

	psta->has_legacy_ac = 0;

	pstapriv->sta_dz_bitmap &= ~CHKBIT(psta->aid);
	pstapriv->tim_bitmap &= ~CHKBIT(psta->aid);

	if ((psta->aid >0) && (pstapriv->sta_aid[psta->aid - 1] == psta)) {
		pstapriv->sta_aid[psta->aid - 1] = NULL;
		psta->aid = 0;
	}
#endif	/*  CONFIG_8723AU_AP_MODE */

	kfree(psta);
exit:
	return _SUCCESS;
}

/*  free all stainfo which in sta_hash[all] */
void rtw_free_all_stainfo23a(struct rtw_adapter *padapter)
{
	struct list_head *plist, *phead, *ptmp;
	struct sta_info *psta;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info* pbcmc_stainfo = rtw_get_bcmc_stainfo23a(padapter);
	s32 index;

	if (pstapriv->asoc_sta_count == 1)
		return;

	spin_lock_bh(&pstapriv->sta_hash_lock);

	for (index = 0; index < NUM_STA; index++) {
		phead = &pstapriv->sta_hash[index];

		list_for_each_safe(plist, ptmp, phead) {
			psta = container_of(plist, struct sta_info, hash_list);

			if (pbcmc_stainfo!= psta)
				rtw_free_stainfo23a(padapter, psta);
		}
	}
	spin_unlock_bh(&pstapriv->sta_hash_lock);
}

/* any station allocated can be searched by hash list */
struct sta_info *rtw_get_stainfo23a(struct sta_priv *pstapriv, const u8 *hwaddr)
{
	struct list_head *plist, *phead;
	struct sta_info *psta = NULL;
	u32	index;
	const u8 *addr;

	if (hwaddr == NULL)
		return NULL;

	if (is_multicast_ether_addr(hwaddr))
		addr = bc_addr;
	else
		addr = hwaddr;

	index = wifi_mac_hash(addr);

	spin_lock_bh(&pstapriv->sta_hash_lock);

	phead = &pstapriv->sta_hash[index];

	list_for_each(plist, phead) {
		psta = container_of(plist, struct sta_info, hash_list);

		/*  if found the matched address */
		if (ether_addr_equal(psta->hwaddr, addr))
			break;

		psta = NULL;
	}
	spin_unlock_bh(&pstapriv->sta_hash_lock);
	return psta;
}

int rtw_init_bcmc_stainfo23a(struct rtw_adapter* padapter)
{
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info		*psta;
	struct tx_servq	*ptxservq;
	int res = _SUCCESS;

	psta = rtw_alloc_stainfo23a(pstapriv, bc_addr, GFP_KERNEL);
	if (psta == NULL) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_sta_mgt_c_, _drv_err_,
			 ("rtw_alloc_stainfo23a fail"));
		return res;
	}
	/*  default broadcast & multicast use macid 1 */
	psta->mac_id = 1;

	ptxservq = &psta->sta_xmitpriv.be_q;
	return _SUCCESS;
}

struct sta_info *rtw_get_bcmc_stainfo23a(struct rtw_adapter *padapter)
{
	struct sta_info		*psta;
	struct sta_priv		*pstapriv = &padapter->stapriv;

	psta = rtw_get_stainfo23a(pstapriv, bc_addr);
	return psta;
}

bool rtw_access_ctrl23a(struct rtw_adapter *padapter, u8 *mac_addr)
{
	bool res = true;
#ifdef CONFIG_8723AU_AP_MODE
	struct list_head *plist, *phead;
	struct rtw_wlan_acl_node *paclnode;
	bool match = false;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct rtw_queue *pacl_node_q = &pacl_list->acl_node_q;

	spin_lock_bh(&pacl_node_q->lock);
	phead = get_list_head(pacl_node_q);

	list_for_each(plist, phead) {
		paclnode = container_of(plist, struct rtw_wlan_acl_node, list);

		if (ether_addr_equal(paclnode->addr, mac_addr)) {
			if (paclnode->valid) {
				match = true;
				break;
			}
		}
	}
	spin_unlock_bh(&pacl_node_q->lock);

	if (pacl_list->mode == 1)/* accept unless in deny list */
		res = (match) ?  false : true;
	else if (pacl_list->mode == 2)/* deny unless in accept list */
		res = (match) ?  true : false;
	else
		 res = true;
#endif
	return res;
}
