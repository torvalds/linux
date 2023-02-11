// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTW_STA_MGT_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/sta_info.h"

static void _rtw_init_stainfo(struct sta_info *psta)
{

	memset((u8 *)psta, 0, sizeof(struct sta_info));

	spin_lock_init(&psta->lock);
	INIT_LIST_HEAD(&psta->list);
	INIT_LIST_HEAD(&psta->hash_list);
	rtw_init_queue(&psta->sleep_q);
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

int _rtw_init_sta_priv(struct sta_priv *pstapriv)
{
	struct sta_info *psta;
	s32 i;

	pstapriv->pallocated_stainfo_buf = vzalloc(sizeof(struct sta_info) * NUM_STA + 4);

	if (!pstapriv->pallocated_stainfo_buf)
		return -ENOMEM;

	pstapriv->pstainfo_buf = pstapriv->pallocated_stainfo_buf + 4 -
		((size_t)(pstapriv->pallocated_stainfo_buf) & 3);

	rtw_init_queue(&pstapriv->free_sta_queue);

	spin_lock_init(&pstapriv->sta_hash_lock);

	pstapriv->asoc_sta_count = 0;
	rtw_init_queue(&pstapriv->sleep_q);
	rtw_init_queue(&pstapriv->wakeup_q);

	psta = (struct sta_info *)(pstapriv->pstainfo_buf);

	for (i = 0; i < NUM_STA; i++) {
		_rtw_init_stainfo(psta);

		INIT_LIST_HEAD(&pstapriv->sta_hash[i]);

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

	return 0;
}

inline int rtw_stainfo_offset(struct sta_priv *stapriv, struct sta_info *sta)
{
	return (((u8 *)sta) - stapriv->pstainfo_buf) / sizeof(struct sta_info);
}

inline struct sta_info *rtw_get_stainfo_by_offset(struct sta_priv *stapriv, int offset)
{
	return (struct sta_info *)(stapriv->pstainfo_buf + offset * sizeof(struct sta_info));
}

void _rtw_free_sta_priv(struct	sta_priv *pstapriv)
{
	struct list_head *phead, *plist;
	struct sta_info *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	int	index;

	if (pstapriv) {
		/*	delete all reordering_ctrl_timer		*/
		spin_lock_bh(&pstapriv->sta_hash_lock);
		for (index = 0; index < NUM_STA; index++) {
			phead = &pstapriv->sta_hash[index];
			plist = phead->next;

			while (phead != plist) {
				int i;
				psta = container_of(plist, struct sta_info, hash_list);
				plist = plist->next;

				for (i = 0; i < 16; i++) {
					preorder_ctrl = &psta->recvreorder_ctrl[i];
					_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);
				}
			}
		}
		spin_unlock_bh(&pstapriv->sta_hash_lock);
		/*===============================*/

		vfree(pstapriv->pallocated_stainfo_buf);
	}
}

static void _rtw_reordering_ctrl_timeout_handler(struct timer_list *t)
{
	struct recv_reorder_ctrl *preorder_ctrl;

	preorder_ctrl = from_timer(preorder_ctrl, t, reordering_ctrl_timer);
	rtw_reordering_ctrl_timeout_handler(preorder_ctrl);
}

static void rtw_init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl)
{
	timer_setup(&preorder_ctrl->reordering_ctrl_timer, _rtw_reordering_ctrl_timeout_handler, 0);
}

static void _addba_timer_hdl(struct timer_list *t)
{
	struct sta_info *psta = from_timer(psta, t, addba_retry_timer);

	addba_timer_hdl(psta);
}

static void init_addba_retry_timer(struct adapter *padapter, struct sta_info *psta)
{
	timer_setup(&psta->addba_retry_timer, _addba_timer_hdl, 0);
}

struct	sta_info *rtw_alloc_stainfo(struct sta_priv *pstapriv, u8 *hwaddr)
{
	s32	index;
	struct list_head *phash_list;
	struct sta_info	*psta;
	struct __queue *pfree_sta_queue;
	struct recv_reorder_ctrl *preorder_ctrl;
	int i = 0;
	u16  wRxSeqInitialValue = 0xffff;

	pfree_sta_queue = &pstapriv->free_sta_queue;

	spin_lock_bh(&pfree_sta_queue->lock);

	if (list_empty(&pfree_sta_queue->queue)) {
		spin_unlock_bh(&pfree_sta_queue->lock);
		psta = NULL;
	} else {
		psta = container_of((&pfree_sta_queue->queue)->next, struct sta_info, list);
		list_del_init(&psta->list);
		spin_unlock_bh(&pfree_sta_queue->lock);
		_rtw_init_stainfo(psta);
		memcpy(psta->hwaddr, hwaddr, ETH_ALEN);
		index = wifi_mac_hash(hwaddr);
		if (index >= NUM_STA) {
			psta = NULL;
			goto exit;
		}
		phash_list = &pstapriv->sta_hash[index];

		spin_lock_bh(&pstapriv->sta_hash_lock);

		list_add_tail(&psta->hash_list, phash_list);

		pstapriv->asoc_sta_count++;

		spin_unlock_bh(&pstapriv->sta_hash_lock);

/*  Commented by Albert 2009/08/13 */
/*  For the SMC router, the sequence number of first packet of WPS handshake will be 0. */
/*  In this case, this packet will be dropped by recv_decache function if we use the 0x00 as the default value for tid_rxseq variable. */
/*  So, we initialize the tid_rxseq variable as the 0xffff. */

		for (i = 0; i < 16; i++)
			memcpy(&psta->sta_recvpriv.rxcache.tid_rxseq[i], &wRxSeqInitialValue, 2);

		init_addba_retry_timer(pstapriv->padapter, psta);

		/* for A-MPDU Rx reordering buffer control */
		for (i = 0; i < 16; i++) {
			preorder_ctrl = &psta->recvreorder_ctrl[i];

			preorder_ctrl->padapter = pstapriv->padapter;

			preorder_ctrl->enable = false;

			preorder_ctrl->indicate_seq = 0xffff;
			preorder_ctrl->wend_b = 0xffff;
			preorder_ctrl->wsize_b = 64;/* 64; */

			rtw_init_queue(&preorder_ctrl->pending_recvframe_queue);

			rtw_init_recv_timer(preorder_ctrl);
		}

		/* init for DM */
		psta->rssi_stat.UndecoratedSmoothedPWDB = (-1);
		psta->rssi_stat.UndecoratedSmoothedCCK = (-1);

		/* init for the sequence number of received management frame */
		psta->RxMgmtFrameSeqNum = 0xffff;
	}

exit:

	return psta;
}

/*  using pstapriv->sta_hash_lock to protect */
void rtw_free_stainfo(struct adapter *padapter, struct sta_info *psta)
{
	int i;
	struct __queue *pfree_sta_queue;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct	sta_xmit_priv	*pstaxmitpriv;
	struct	xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct	sta_priv *pstapriv = &padapter->stapriv;

	if (!psta)
		return;

	pfree_sta_queue = &pstapriv->free_sta_queue;

	pstaxmitpriv = &psta->sta_xmitpriv;

	spin_lock_bh(&pxmitpriv->lock);

	rtw_free_xmitframe_list(pxmitpriv, get_list_head(&psta->sleep_q));
	psta->sleepq_len = 0;

	rtw_free_xmitframe_list(pxmitpriv, &pstaxmitpriv->vo_q.sta_pending);

	list_del_init(&pstaxmitpriv->vo_q.tx_pending);

	rtw_free_xmitframe_list(pxmitpriv, &pstaxmitpriv->vi_q.sta_pending);

	list_del_init(&pstaxmitpriv->vi_q.tx_pending);

	rtw_free_xmitframe_list(pxmitpriv, &pstaxmitpriv->bk_q.sta_pending);

	list_del_init(&pstaxmitpriv->bk_q.tx_pending);

	rtw_free_xmitframe_list(pxmitpriv, &pstaxmitpriv->be_q.sta_pending);

	list_del_init(&pstaxmitpriv->be_q.tx_pending);

	spin_unlock_bh(&pxmitpriv->lock);

	list_del_init(&psta->hash_list);
	pstapriv->asoc_sta_count--;

	/*  re-init sta_info; 20061114 */
	_rtw_init_sta_xmit_priv(&psta->sta_xmitpriv);
	_rtw_init_sta_recv_priv(&psta->sta_recvpriv);

	_cancel_timer_ex(&psta->addba_retry_timer);

	/* for A-MPDU Rx reordering buffer control, cancel reordering_ctrl_timer */
	for (i = 0; i < 16 ; i++) {
		struct list_head *phead, *plist;
		struct recv_frame *prframe;
		struct __queue *ppending_recvframe_queue;
		struct __queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

		preorder_ctrl = &psta->recvreorder_ctrl[i];

		_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);

		ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

		spin_lock_bh(&ppending_recvframe_queue->lock);

		phead =		get_list_head(ppending_recvframe_queue);
		plist = phead->next;

		while (!list_empty(phead)) {
			prframe = container_of(plist, struct recv_frame, list);

			plist = plist->next;

			list_del_init(&prframe->list);

			rtw_free_recvframe(prframe, pfree_recv_queue);
		}

		spin_unlock_bh(&ppending_recvframe_queue->lock);
	}

	if (!(psta->state & WIFI_AP_STATE))
		rtl8188e_SetHalODMVar(padapter, psta, false);

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

	spin_lock_bh(&pfree_sta_queue->lock);
	list_add_tail(&psta->list, get_list_head(pfree_sta_queue));
	spin_unlock_bh(&pfree_sta_queue->lock);
}

/*  free all stainfo which in sta_hash[all] */
void rtw_free_all_stainfo(struct adapter *padapter)
{
	struct list_head *plist, *phead;
	s32	index;
	struct sta_info *psta = NULL;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *pbcmc_stainfo = rtw_get_bcmc_stainfo(padapter);

	if (pstapriv->asoc_sta_count == 1)
		return;

	spin_lock_bh(&pstapriv->sta_hash_lock);

	for (index = 0; index < NUM_STA; index++) {
		phead = &pstapriv->sta_hash[index];
		plist = phead->next;

		while (phead != plist) {
			psta = container_of(plist, struct sta_info, hash_list);

			plist = plist->next;

			if (pbcmc_stainfo != psta)
				rtw_free_stainfo(padapter, psta);
		}
	}
	spin_unlock_bh(&pstapriv->sta_hash_lock);
}

/* any station allocated can be searched by hash list */
struct sta_info *rtw_get_stainfo(struct sta_priv *pstapriv, u8 *hwaddr)
{
	struct sta_info *ploop, *psta = NULL;
	u32	index;
	u8 *addr;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if (!hwaddr)
		return NULL;

	if (is_multicast_ether_addr(hwaddr))
		addr = bc_addr;
	else
		addr = hwaddr;

	index = wifi_mac_hash(addr);

	spin_lock_bh(&pstapriv->sta_hash_lock);

	list_for_each_entry(ploop, &pstapriv->sta_hash[index], hash_list) {
		if (!memcmp(ploop->hwaddr, addr, ETH_ALEN)) {
			psta = ploop;
			break;
		}
	}

	spin_unlock_bh(&pstapriv->sta_hash_lock);

	return psta;
}

u32 rtw_init_bcmc_stainfo(struct adapter *padapter)
{
	struct sta_info		*psta;
	u32 res = _SUCCESS;
	unsigned char bcast_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct	sta_priv *pstapriv = &padapter->stapriv;

	psta = rtw_alloc_stainfo(pstapriv, bcast_addr);

	if (!psta) {
		res = _FAIL;
		goto exit;
	}

	/*  default broadcast & multicast use macid 1 */
	psta->mac_id = 1;

exit:

	return res;
}

struct sta_info *rtw_get_bcmc_stainfo(struct adapter *padapter)
{
	struct sta_info		*psta;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	psta = rtw_get_stainfo(pstapriv, bc_addr);

	return psta;
}

u8 rtw_access_ctrl(struct adapter *padapter, u8 *mac_addr)
{
	u8 res = true;
	struct list_head *plist, *phead;
	struct rtw_wlan_acl_node *paclnode;
	u8 match = false;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	struct __queue *pacl_node_q = &pacl_list->acl_node_q;

	spin_lock_bh(&pacl_node_q->lock);
	phead = get_list_head(pacl_node_q);
	plist = phead->next;
	while (phead != plist) {
		paclnode = container_of(plist, struct rtw_wlan_acl_node, list);
		plist = plist->next;

		if (!memcmp(paclnode->addr, mac_addr, ETH_ALEN)) {
			if (paclnode->valid) {
				match = true;
				break;
			}
		}
	}
	spin_unlock_bh(&pacl_node_q->lock);

	if (pacl_list->mode == 1)/* accept unless in deny list */
		res = !match;
	else if (pacl_list->mode == 2)/* deny unless in accept list */
		res = match;
	else
		res = true;

	return res;
}
