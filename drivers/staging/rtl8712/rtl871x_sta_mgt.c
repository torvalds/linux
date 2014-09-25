/******************************************************************************
 * rtl871x_sta_mgt.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _RTL871X_STA_MGT_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "recv_osdep.h"
#include "xmit_osdep.h"
#include "sta_info.h"

static void _init_stainfo(struct sta_info *psta)
{
	memset((u8 *)psta, 0, sizeof(struct sta_info));
	 spin_lock_init(&psta->lock);
	INIT_LIST_HEAD(&psta->list);
	INIT_LIST_HEAD(&psta->hash_list);
	_r8712_init_sta_xmit_priv(&psta->sta_xmitpriv);
	_r8712_init_sta_recv_priv(&psta->sta_recvpriv);
	INIT_LIST_HEAD(&psta->asoc_list);
	INIT_LIST_HEAD(&psta->auth_list);
}

u32 _r8712_init_sta_priv(struct	sta_priv *pstapriv)
{
	struct sta_info *psta;
	s32 i;

	pstapriv->pallocated_stainfo_buf = kmalloc(sizeof(struct sta_info) *
						   NUM_STA + 4, GFP_ATOMIC);
	if (pstapriv->pallocated_stainfo_buf == NULL)
		return _FAIL;
	pstapriv->pstainfo_buf = pstapriv->pallocated_stainfo_buf + 4 -
		((addr_t)(pstapriv->pallocated_stainfo_buf) & 3);
	_init_queue(&pstapriv->free_sta_queue);
	spin_lock_init(&pstapriv->sta_hash_lock);
	pstapriv->asoc_sta_count = 0;
	_init_queue(&pstapriv->sleep_q);
	_init_queue(&pstapriv->wakeup_q);
	psta = (struct sta_info *)(pstapriv->pstainfo_buf);
	for (i = 0; i < NUM_STA; i++) {
		_init_stainfo(psta);
		INIT_LIST_HEAD(&(pstapriv->sta_hash[i]));
		list_add_tail(&psta->list, &pstapriv->free_sta_queue.queue);
		psta++;
	}
	INIT_LIST_HEAD(&pstapriv->asoc_list);
	INIT_LIST_HEAD(&pstapriv->auth_list);
	return _SUCCESS;
}

/* this function is used to free the memory of lock || sema for all stainfos */
static void mfree_all_stainfo(struct sta_priv *pstapriv)
{
	unsigned long irqL;
	struct list_head *plist, *phead;
	struct sta_info *psta = NULL;

	spin_lock_irqsave(&pstapriv->sta_hash_lock, irqL);
	phead = &pstapriv->free_sta_queue.queue;
	plist = phead->next;
	while ((end_of_queue_search(phead, plist)) == false) {
		psta = LIST_CONTAINOR(plist, struct sta_info, list);
		plist = plist->next;
	}

	spin_unlock_irqrestore(&pstapriv->sta_hash_lock, irqL);
}


static void mfree_sta_priv_lock(struct	sta_priv *pstapriv)
{
	 mfree_all_stainfo(pstapriv); /* be done before free sta_hash_lock */
}

u32 _r8712_free_sta_priv(struct sta_priv *pstapriv)
{
	if (pstapriv) {
		mfree_sta_priv_lock(pstapriv);
		kfree(pstapriv->pallocated_stainfo_buf);
	}
	return _SUCCESS;
}

struct sta_info *r8712_alloc_stainfo(struct sta_priv *pstapriv, u8 *hwaddr)
{
	uint tmp_aid;
	s32	index;
	struct list_head *phash_list;
	struct sta_info	*psta;
	struct  __queue *pfree_sta_queue;
	struct recv_reorder_ctrl *preorder_ctrl;
	int i = 0;
	u16  wRxSeqInitialValue = 0xffff;
	unsigned long flags;

	pfree_sta_queue = &pstapriv->free_sta_queue;
	spin_lock_irqsave(&(pfree_sta_queue->lock), flags);
	if (list_empty(&pfree_sta_queue->queue))
		psta = NULL;
	else {
		psta = LIST_CONTAINOR(pfree_sta_queue->queue.next,
				      struct sta_info, list);
		list_del_init(&(psta->list));
		tmp_aid = psta->aid;
		_init_stainfo(psta);
		memcpy(psta->hwaddr, hwaddr, ETH_ALEN);
		index = wifi_mac_hash(hwaddr);
		if (index >= NUM_STA) {
			psta = NULL;
			goto exit;
		}
		phash_list = &(pstapriv->sta_hash[index]);
		list_add_tail(&psta->hash_list, phash_list);
		pstapriv->asoc_sta_count++;

/* For the SMC router, the sequence number of first packet of WPS handshake
 * will be 0. In this case, this packet will be dropped by recv_decache function
 * if we use the 0x00 as the default value for tid_rxseq variable. So, we
 * initialize the tid_rxseq variable as the 0xffff.
 */
		for (i = 0; i < 16; i++)
			memcpy(&psta->sta_recvpriv.rxcache.tid_rxseq[i],
				&wRxSeqInitialValue, 2);
		/* for A-MPDU Rx reordering buffer control */
		for (i = 0; i < 16; i++) {
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			preorder_ctrl->padapter = pstapriv->padapter;
			preorder_ctrl->indicate_seq = 0xffff;
			preorder_ctrl->wend_b = 0xffff;
			preorder_ctrl->wsize_b = 64;
			_init_queue(&preorder_ctrl->pending_recvframe_queue);
			r8712_init_recv_timer(preorder_ctrl);
		}
	}
exit:
	spin_unlock_irqrestore(&(pfree_sta_queue->lock), flags);
	return psta;
}

/* using pstapriv->sta_hash_lock to protect */
void r8712_free_stainfo(struct _adapter *padapter, struct sta_info *psta)
{
	int i;
	unsigned long irqL0;
	struct  __queue *pfree_sta_queue;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct	sta_xmit_priv *pstaxmitpriv;
	struct	xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct	sta_priv *pstapriv = &padapter->stapriv;

	if (psta == NULL)
		return;
	pfree_sta_queue = &pstapriv->free_sta_queue;
	pstaxmitpriv = &psta->sta_xmitpriv;
	spin_lock_irqsave(&(pxmitpriv->vo_pending.lock), irqL0);
	r8712_free_xmitframe_queue(pxmitpriv, &pstaxmitpriv->vo_q.sta_pending);
	list_del_init(&(pstaxmitpriv->vo_q.tx_pending));
	spin_unlock_irqrestore(&(pxmitpriv->vo_pending.lock), irqL0);
	spin_lock_irqsave(&(pxmitpriv->vi_pending.lock), irqL0);
	r8712_free_xmitframe_queue(pxmitpriv, &pstaxmitpriv->vi_q.sta_pending);
	list_del_init(&(pstaxmitpriv->vi_q.tx_pending));
	spin_unlock_irqrestore(&(pxmitpriv->vi_pending.lock), irqL0);
	spin_lock_irqsave(&(pxmitpriv->bk_pending.lock), irqL0);
	r8712_free_xmitframe_queue(pxmitpriv, &pstaxmitpriv->bk_q.sta_pending);
	list_del_init(&(pstaxmitpriv->bk_q.tx_pending));
	spin_unlock_irqrestore(&(pxmitpriv->bk_pending.lock), irqL0);
	spin_lock_irqsave(&(pxmitpriv->be_pending.lock), irqL0);
	r8712_free_xmitframe_queue(pxmitpriv, &pstaxmitpriv->be_q.sta_pending);
	list_del_init(&(pstaxmitpriv->be_q.tx_pending));
	spin_unlock_irqrestore(&(pxmitpriv->be_pending.lock), irqL0);
	list_del_init(&psta->hash_list);
	pstapriv->asoc_sta_count--;
	/* re-init sta_info; 20061114 */
	_r8712_init_sta_xmit_priv(&psta->sta_xmitpriv);
	_r8712_init_sta_recv_priv(&psta->sta_recvpriv);
	/* for A-MPDU Rx reordering buffer control,
	 * cancel reordering_ctrl_timer */
	for (i = 0; i < 16; i++) {
		preorder_ctrl = &psta->recvreorder_ctrl[i];
		_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);
	}
	spin_lock(&(pfree_sta_queue->lock));
	/* insert into free_sta_queue; 20061114 */
	list_add_tail(&psta->list, &pfree_sta_queue->queue);
	spin_unlock(&(pfree_sta_queue->lock));
}

/* free all stainfo which in sta_hash[all] */
void r8712_free_all_stainfo(struct _adapter *padapter)
{
	unsigned long irqL;
	struct list_head *plist, *phead;
	s32 index;
	struct sta_info *psta = NULL;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *pbcmc_stainfo = r8712_get_bcmc_stainfo(padapter);

	if (pstapriv->asoc_sta_count == 1)
		return;
	spin_lock_irqsave(&pstapriv->sta_hash_lock, irqL);
	for (index = 0; index < NUM_STA; index++) {
		phead = &(pstapriv->sta_hash[index]);
		plist = phead->next;
		while ((end_of_queue_search(phead, plist)) == false) {
			psta = LIST_CONTAINOR(plist,
					      struct sta_info, hash_list);
			plist = plist->next;
			if (pbcmc_stainfo != psta)
				r8712_free_stainfo(padapter , psta);
		}
	}
	spin_unlock_irqrestore(&pstapriv->sta_hash_lock, irqL);
}

/* any station allocated can be searched by hash list */
struct sta_info *r8712_get_stainfo(struct sta_priv *pstapriv, u8 *hwaddr)
{
	unsigned long	 irqL;
	struct list_head *plist, *phead;
	struct sta_info *psta = NULL;
	u32	index;

	if (hwaddr == NULL)
		return NULL;
	index = wifi_mac_hash(hwaddr);
	spin_lock_irqsave(&pstapriv->sta_hash_lock, irqL);
	phead = &(pstapriv->sta_hash[index]);
	plist = phead->next;
	while ((end_of_queue_search(phead, plist)) == false) {
		psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
		if ((!memcmp(psta->hwaddr, hwaddr, ETH_ALEN))) {
			/* if found the matched address */
			break;
		}
		psta = NULL;
		plist = plist->next;
	}
	spin_unlock_irqrestore(&pstapriv->sta_hash_lock, irqL);
	return psta;
}

void r8712_init_bcmc_stainfo(struct _adapter *padapter)
{
	struct sta_info	*psta;
	struct tx_servq	*ptxservq;
	unsigned char bcast_addr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct	sta_priv *pstapriv = &padapter->stapriv;

	psta = r8712_alloc_stainfo(pstapriv, bcast_addr);
	if (psta == NULL)
		return;
	ptxservq = &(psta->sta_xmitpriv.be_q);
}

struct sta_info *r8712_get_bcmc_stainfo(struct _adapter *padapter)
{
	struct sta_info *psta;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	psta = r8712_get_stainfo(pstapriv, bc_addr);
	return psta;
}


u8 r8712_access_ctrl(struct wlan_acl_pool *pacl_list, u8 *mac_addr)
{
	return true;
}
