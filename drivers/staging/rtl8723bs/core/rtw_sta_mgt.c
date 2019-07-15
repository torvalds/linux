// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTW_STA_MGT_C_

#include <drv_types.h>
#include <rtw_debug.h>

void _rtw_init_stainfo(struct sta_info *psta);
void _rtw_init_stainfo(struct sta_info *psta)
{
	memset((u8 *)psta, 0, sizeof(struct sta_info));

	spin_lock_init(&psta->lock);
	INIT_LIST_HEAD(&psta->list);
	INIT_LIST_HEAD(&psta->hash_list);
	/* INIT_LIST_HEAD(&psta->asoc_list); */
	/* INIT_LIST_HEAD(&psta->sleep_list); */
	/* INIT_LIST_HEAD(&psta->wakeup_list); */

	_rtw_init_queue(&psta->sleep_q);
	psta->sleepq_len = 0;

	_rtw_init_sta_xmit_priv(&psta->sta_xmitpriv);
	_rtw_init_sta_recv_priv(&psta->sta_recvpriv);

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

	psta->under_exist_checking = 0;

	psta->keep_alive_trycnt = 0;
}

u32 _rtw_init_sta_priv(struct	sta_priv *pstapriv)
{
	struct sta_info *psta;
	s32 i;

	pstapriv->pallocated_stainfo_buf = vzalloc(sizeof(struct sta_info) * NUM_STA+4);

	if (!pstapriv->pallocated_stainfo_buf)
		return _FAIL;

	pstapriv->pstainfo_buf = pstapriv->pallocated_stainfo_buf + 4 -
		((SIZE_PTR)(pstapriv->pallocated_stainfo_buf) & 3);

	_rtw_init_queue(&pstapriv->free_sta_queue);

	spin_lock_init(&pstapriv->sta_hash_lock);

	/* _rtw_init_queue(&pstapriv->asoc_q); */
	pstapriv->asoc_sta_count = 0;
	_rtw_init_queue(&pstapriv->sleep_q);
	_rtw_init_queue(&pstapriv->wakeup_q);

	psta = (struct sta_info *)(pstapriv->pstainfo_buf);

	for (i = 0; i < NUM_STA; i++) {
		_rtw_init_stainfo(psta);

		INIT_LIST_HEAD(&(pstapriv->sta_hash[i]));

		list_add_tail(&psta->list, get_list_head(&pstapriv->free_sta_queue));

		psta++;
	}

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
	pstapriv->expire_to = 3; /*  3*2 = 6 sec */
	pstapriv->max_num_sta = NUM_STA;
	return _SUCCESS;
}

inline int rtw_stainfo_offset(struct sta_priv *stapriv, struct sta_info *sta)
{
	int offset = (((u8 *)sta) - stapriv->pstainfo_buf)/sizeof(struct sta_info);

	if (!stainfo_offset_valid(offset))
		DBG_871X("%s invalid offset(%d), out of range!!!", __func__, offset);

	return offset;
}

inline struct sta_info *rtw_get_stainfo_by_offset(struct sta_priv *stapriv, int offset)
{
	if (!stainfo_offset_valid(offset))
		DBG_871X("%s invalid offset(%d), out of range!!!", __func__, offset);

	return (struct sta_info *)(stapriv->pstainfo_buf + offset * sizeof(struct sta_info));
}

/*  this function is used to free the memory of lock || sema for all stainfos */
void kfree_all_stainfo(struct sta_priv *pstapriv);
void kfree_all_stainfo(struct sta_priv *pstapriv)
{
	struct list_head	*plist, *phead;
	struct sta_info *psta = NULL;

	spin_lock_bh(&pstapriv->sta_hash_lock);

	phead = get_list_head(&pstapriv->free_sta_queue);
	plist = get_next(phead);

	while (phead != plist) {
		psta = LIST_CONTAINOR(plist, struct sta_info, list);
		plist = get_next(plist);
	}

	spin_unlock_bh(&pstapriv->sta_hash_lock);
}

void kfree_sta_priv_lock(struct	sta_priv *pstapriv);
void kfree_sta_priv_lock(struct	sta_priv *pstapriv)
{
	 kfree_all_stainfo(pstapriv); /* be done before free sta_hash_lock */
}

u32 _rtw_free_sta_priv(struct	sta_priv *pstapriv)
{
	struct list_head	*phead, *plist;
	struct sta_info *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	int	index;

	if (pstapriv) {

		/*delete all reordering_ctrl_timer		*/
		spin_lock_bh(&pstapriv->sta_hash_lock);
		for (index = 0; index < NUM_STA; index++) {
			phead = &(pstapriv->sta_hash[index]);
			plist = get_next(phead);

			while (phead != plist) {
				int i;
				psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
				plist = get_next(plist);

				for (i = 0; i < 16 ; i++) {
					preorder_ctrl = &psta->recvreorder_ctrl[i];
					del_timer_sync(&preorder_ctrl->reordering_ctrl_timer);
				}
			}
		}
		spin_unlock_bh(&pstapriv->sta_hash_lock);
		/*===============================*/

		kfree_sta_priv_lock(pstapriv);

		if (pstapriv->pallocated_stainfo_buf)
			vfree(pstapriv->pallocated_stainfo_buf);

	}
	return _SUCCESS;
}

/* struct	sta_info *rtw_alloc_stainfo(_queue *pfree_sta_queue, unsigned char *hwaddr) */
struct	sta_info *rtw_alloc_stainfo(struct	sta_priv *pstapriv, u8 *hwaddr)
{
	uint tmp_aid;
	s32	index;
	struct list_head	*phash_list;
	struct sta_info *psta;
	struct __queue *pfree_sta_queue;
	struct recv_reorder_ctrl *preorder_ctrl;
	int i = 0;
	u16  wRxSeqInitialValue = 0xffff;

	pfree_sta_queue = &pstapriv->free_sta_queue;

	/* spin_lock_bh(&(pfree_sta_queue->lock)); */
	spin_lock_bh(&(pstapriv->sta_hash_lock));
	if (list_empty(&pfree_sta_queue->queue)) {
		/* spin_unlock_bh(&(pfree_sta_queue->lock)); */
		spin_unlock_bh(&(pstapriv->sta_hash_lock));
		return NULL;
	} else {
		psta = LIST_CONTAINOR(get_next(&pfree_sta_queue->queue), struct sta_info, list);

		list_del_init(&(psta->list));

		/* spin_unlock_bh(&(pfree_sta_queue->lock)); */

		tmp_aid = psta->aid;

		_rtw_init_stainfo(psta);

		psta->padapter = pstapriv->padapter;

		memcpy(psta->hwaddr, hwaddr, ETH_ALEN);

		index = wifi_mac_hash(hwaddr);

		RT_TRACE(_module_rtl871x_sta_mgt_c_, _drv_info_, ("rtw_alloc_stainfo: index  = %x", index));

		if (index >= NUM_STA) {
			RT_TRACE(_module_rtl871x_sta_mgt_c_, _drv_err_, ("ERROR => rtw_alloc_stainfo: index >= NUM_STA"));
			spin_unlock_bh(&(pstapriv->sta_hash_lock));
			psta = NULL;
			goto exit;
		}
		phash_list = &(pstapriv->sta_hash[index]);

		/* spin_lock_bh(&(pstapriv->sta_hash_lock)); */

		list_add_tail(&psta->hash_list, phash_list);

		pstapriv->asoc_sta_count++;

		/* spin_unlock_bh(&(pstapriv->sta_hash_lock)); */

/*  Commented by Albert 2009/08/13 */
/*  For the SMC router, the sequence number of first packet of WPS handshake will be 0. */
/*  In this case, this packet will be dropped by recv_decache function if we use the 0x00 as the default value for tid_rxseq variable. */
/*  So, we initialize the tid_rxseq variable as the 0xffff. */

		for (i = 0; i < 16; i++) {
			memcpy(&psta->sta_recvpriv.rxcache.tid_rxseq[i], &wRxSeqInitialValue, 2);
		}

		RT_TRACE(
			_module_rtl871x_sta_mgt_c_,
			_drv_info_, (
				"alloc number_%d stainfo  with hwaddr = %x %x %x %x %x %x \n",
				pstapriv->asoc_sta_count,
				hwaddr[0],
				hwaddr[1],
				hwaddr[2],
				hwaddr[3],
				hwaddr[4],
				hwaddr[5]
			)
		);

		init_addba_retry_timer(pstapriv->padapter, psta);

		/* for A-MPDU Rx reordering buffer control */
		for (i = 0; i < 16 ; i++) {
			preorder_ctrl = &psta->recvreorder_ctrl[i];

			preorder_ctrl->padapter = pstapriv->padapter;

			preorder_ctrl->enable = false;

			preorder_ctrl->indicate_seq = 0xffff;
			#ifdef DBG_RX_SEQ
			DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d\n", __func__, __LINE__,
				preorder_ctrl->indicate_seq);
			#endif
			preorder_ctrl->wend_b = 0xffff;
			/* preorder_ctrl->wsize_b = (NR_RECVBUFF-2); */
			preorder_ctrl->wsize_b = 64;/* 64; */

			_rtw_init_queue(&preorder_ctrl->pending_recvframe_queue);

			rtw_init_recv_timer(preorder_ctrl);
		}


		/* init for DM */
		psta->rssi_stat.UndecoratedSmoothedPWDB = (-1);
		psta->rssi_stat.UndecoratedSmoothedCCK = (-1);

		/* init for the sequence number of received management frame */
		psta->RxMgmtFrameSeqNum = 0xffff;
		spin_unlock_bh(&(pstapriv->sta_hash_lock));
		/* alloc mac id for non-bc/mc station, */
		rtw_alloc_macid(pstapriv->padapter, psta);

	}

exit:


	return psta;
}

/*  using pstapriv->sta_hash_lock to protect */
u32 rtw_free_stainfo(struct adapter *padapter, struct sta_info *psta)
{
	int i;
	struct __queue *pfree_sta_queue;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct	sta_xmit_priv *pstaxmitpriv;
	struct	xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct hw_xmit *phwxmit;

	if (!psta)
		goto exit;


	spin_lock_bh(&psta->lock);
	psta->state &= ~_FW_LINKED;
	spin_unlock_bh(&psta->lock);

	pfree_sta_queue = &pstapriv->free_sta_queue;


	pstaxmitpriv = &psta->sta_xmitpriv;

	/* list_del_init(&psta->sleep_list); */

	/* list_del_init(&psta->wakeup_list); */

	spin_lock_bh(&pxmitpriv->lock);

	rtw_free_xmitframe_queue(pxmitpriv, &psta->sleep_q);
	psta->sleepq_len = 0;

	/* vo */
	/* spin_lock_bh(&(pxmitpriv->vo_pending.lock)); */
	rtw_free_xmitframe_queue(pxmitpriv, &pstaxmitpriv->vo_q.sta_pending);
	list_del_init(&(pstaxmitpriv->vo_q.tx_pending));
	phwxmit = pxmitpriv->hwxmits;
	phwxmit->accnt -= pstaxmitpriv->vo_q.qcnt;
	pstaxmitpriv->vo_q.qcnt = 0;
	/* spin_unlock_bh(&(pxmitpriv->vo_pending.lock)); */

	/* vi */
	/* spin_lock_bh(&(pxmitpriv->vi_pending.lock)); */
	rtw_free_xmitframe_queue(pxmitpriv, &pstaxmitpriv->vi_q.sta_pending);
	list_del_init(&(pstaxmitpriv->vi_q.tx_pending));
	phwxmit = pxmitpriv->hwxmits+1;
	phwxmit->accnt -= pstaxmitpriv->vi_q.qcnt;
	pstaxmitpriv->vi_q.qcnt = 0;
	/* spin_unlock_bh(&(pxmitpriv->vi_pending.lock)); */

	/* be */
	/* spin_lock_bh(&(pxmitpriv->be_pending.lock)); */
	rtw_free_xmitframe_queue(pxmitpriv, &pstaxmitpriv->be_q.sta_pending);
	list_del_init(&(pstaxmitpriv->be_q.tx_pending));
	phwxmit = pxmitpriv->hwxmits+2;
	phwxmit->accnt -= pstaxmitpriv->be_q.qcnt;
	pstaxmitpriv->be_q.qcnt = 0;
	/* spin_unlock_bh(&(pxmitpriv->be_pending.lock)); */

	/* bk */
	/* spin_lock_bh(&(pxmitpriv->bk_pending.lock)); */
	rtw_free_xmitframe_queue(pxmitpriv, &pstaxmitpriv->bk_q.sta_pending);
	list_del_init(&(pstaxmitpriv->bk_q.tx_pending));
	phwxmit = pxmitpriv->hwxmits+3;
	phwxmit->accnt -= pstaxmitpriv->bk_q.qcnt;
	pstaxmitpriv->bk_q.qcnt = 0;
	/* spin_unlock_bh(&(pxmitpriv->bk_pending.lock)); */

	spin_unlock_bh(&pxmitpriv->lock);

	list_del_init(&psta->hash_list);
	RT_TRACE(
		_module_rtl871x_sta_mgt_c_,
		_drv_err_, (
			"\n free number_%d stainfo  with hwaddr = 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x \n",
			pstapriv->asoc_sta_count,
			psta->hwaddr[0],
			psta->hwaddr[1],
			psta->hwaddr[2],
			psta->hwaddr[3],
			psta->hwaddr[4],
			psta->hwaddr[5]
		)
	);
	pstapriv->asoc_sta_count--;

	/*  re-init sta_info; 20061114 will be init in alloc_stainfo */
	/* _rtw_init_sta_xmit_priv(&psta->sta_xmitpriv); */
	/* _rtw_init_sta_recv_priv(&psta->sta_recvpriv); */

	del_timer_sync(&psta->addba_retry_timer);

	/* for A-MPDU Rx reordering buffer control, cancel reordering_ctrl_timer */
	for (i = 0; i < 16 ; i++) {
		struct list_head	*phead, *plist;
		union recv_frame *prframe;
		struct __queue *ppending_recvframe_queue;
		struct __queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

		preorder_ctrl = &psta->recvreorder_ctrl[i];

		del_timer_sync(&preorder_ctrl->reordering_ctrl_timer);


		ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

		spin_lock_bh(&ppending_recvframe_queue->lock);

		phead =		get_list_head(ppending_recvframe_queue);
		plist = get_next(phead);

		while (!list_empty(phead)) {
			prframe = (union recv_frame *)plist;

			plist = get_next(plist);

			list_del_init(&(prframe->u.hdr.list));

			rtw_free_recvframe(prframe, pfree_recv_queue);
		}

		spin_unlock_bh(&ppending_recvframe_queue->lock);

	}

	if (!(psta->state & WIFI_AP_STATE))
		rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, false);

	/* release mac id for non-bc/mc station, */
	rtw_release_macid(pstapriv->padapter, psta);

/*
	spin_lock_bh(&pstapriv->asoc_list_lock);
	list_del_init(&psta->asoc_list);
	spin_unlock_bh(&pstapriv->asoc_list_lock);
*/
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

	pstapriv->sta_dz_bitmap &= ~BIT(psta->aid);
	pstapriv->tim_bitmap &= ~BIT(psta->aid);

	if ((psta->aid > 0) && (pstapriv->sta_aid[psta->aid - 1] == psta)) {
		pstapriv->sta_aid[psta->aid - 1] = NULL;
		psta->aid = 0;
	}

	psta->under_exist_checking = 0;

	/* spin_lock_bh(&(pfree_sta_queue->lock)); */
	list_add_tail(&psta->list, get_list_head(pfree_sta_queue));
	/* spin_unlock_bh(&(pfree_sta_queue->lock)); */

exit:
	return _SUCCESS;
}

/*  free all stainfo which in sta_hash[all] */
void rtw_free_all_stainfo(struct adapter *padapter)
{
	struct list_head	*plist, *phead;
	s32	index;
	struct sta_info *psta = NULL;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *pbcmc_stainfo = rtw_get_bcmc_stainfo(padapter);

	if (pstapriv->asoc_sta_count == 1)
		return;

	spin_lock_bh(&pstapriv->sta_hash_lock);

	for (index = 0; index < NUM_STA; index++) {
		phead = &(pstapriv->sta_hash[index]);
		plist = get_next(phead);

		while (phead != plist) {
			psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

			plist = get_next(plist);

			if (pbcmc_stainfo != psta)
				rtw_free_stainfo(padapter, psta);

		}
	}

	spin_unlock_bh(&pstapriv->sta_hash_lock);
}

/* any station allocated can be searched by hash list */
struct sta_info *rtw_get_stainfo(struct sta_priv *pstapriv, u8 *hwaddr)
{
	struct list_head	*plist, *phead;
	struct sta_info *psta = NULL;
	u32 index;
	u8 *addr;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if (!hwaddr)
		return NULL;

	if (IS_MCAST(hwaddr))
		addr = bc_addr;
	else
		addr = hwaddr;

	index = wifi_mac_hash(addr);

	spin_lock_bh(&pstapriv->sta_hash_lock);

	phead = &(pstapriv->sta_hash[index]);
	plist = get_next(phead);


	while (phead != plist) {

		psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

		if ((!memcmp(psta->hwaddr, addr, ETH_ALEN)))
		 /*  if found the matched address */
			break;

		psta = NULL;
		plist = get_next(plist);
	}

	spin_unlock_bh(&pstapriv->sta_hash_lock);
	return psta;
}

u32 rtw_init_bcmc_stainfo(struct adapter *padapter)
{

	struct sta_info *psta;
	struct tx_servq	*ptxservq;
	u32 res = _SUCCESS;
	NDIS_802_11_MAC_ADDRESS	bcast_addr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	struct	sta_priv *pstapriv = &padapter->stapriv;
	/* struct __queue	*pstapending = &padapter->xmitpriv.bm_pending; */

	psta = rtw_alloc_stainfo(pstapriv, bcast_addr);

	if (!psta) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_sta_mgt_c_, _drv_err_, ("rtw_alloc_stainfo fail"));
		goto exit;
	}

	/*  default broadcast & multicast use macid 1 */
	psta->mac_id = 1;

	ptxservq = &(psta->sta_xmitpriv.be_q);
exit:
	return _SUCCESS;
}

struct sta_info *rtw_get_bcmc_stainfo(struct adapter *padapter)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	return rtw_get_stainfo(pstapriv, bc_addr);
}

u8 rtw_access_ctrl(struct adapter *padapter, u8 *mac_addr)
{
	bool res = true;
	struct list_head	*plist, *phead;
	struct rtw_wlan_acl_node *paclnode;
	bool match = false;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct __queue	*pacl_node_q = &pacl_list->acl_node_q;

	spin_lock_bh(&(pacl_node_q->lock));
	phead = get_list_head(pacl_node_q);
	plist = get_next(phead);
	while (phead != plist) {
		paclnode = LIST_CONTAINOR(plist, struct rtw_wlan_acl_node, list);
		plist = get_next(plist);

		if (!memcmp(paclnode->addr, mac_addr, ETH_ALEN))
			if (paclnode->valid == true) {
				match = true;
				break;
			}

	}
	spin_unlock_bh(&(pacl_node_q->lock));

	if (pacl_list->mode == 1) /* accept unless in deny list */
		res = !match;

	else if (pacl_list->mode == 2)/* deny unless in accept list */
		res = match;
	else
		 res = true;

	return res;
}
