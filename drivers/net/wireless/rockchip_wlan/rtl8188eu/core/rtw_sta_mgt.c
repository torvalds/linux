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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_STA_MGT_C_

#include <drv_types.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif


void _rtw_init_stainfo(struct sta_info *psta);
void _rtw_init_stainfo(struct sta_info *psta)
{

_func_enter_;

	_rtw_memset((u8 *)psta, 0, sizeof (struct sta_info));

	 _rtw_spinlock_init(&psta->lock);
	_rtw_init_listhead(&psta->list);
	_rtw_init_listhead(&psta->hash_list);
	//_rtw_init_listhead(&psta->asoc_list);
	//_rtw_init_listhead(&psta->sleep_list);
	//_rtw_init_listhead(&psta->wakeup_list);	

	_rtw_init_queue(&psta->sleep_q);
	psta->sleepq_len = 0;

	_rtw_init_sta_xmit_priv(&psta->sta_xmitpriv);
	_rtw_init_sta_recv_priv(&psta->sta_recvpriv);
	
#ifdef CONFIG_AP_MODE

	_rtw_init_listhead(&psta->asoc_list);

	_rtw_init_listhead(&psta->auth_list);
	
	psta->expire_to = 0;
	
	psta->flags = 0;
	
	psta->capability = 0;

	psta->bpairwise_key_installed = _FALSE;


#ifdef CONFIG_NATIVEAP_MLME
	psta->nonerp_set = 0;
	psta->no_short_slot_time_set = 0;
	psta->no_short_preamble_set = 0;
	psta->no_ht_gf_set = 0;
	psta->no_ht_set = 0;
	psta->ht_20mhz_set = 0;
#endif	

#ifdef CONFIG_TX_MCAST2UNI
	psta->under_exist_checking = 0;
#endif	// CONFIG_TX_MCAST2UNI
	
	psta->keep_alive_trycnt = 0;

#endif	// CONFIG_AP_MODE	
	
_func_exit_;	

}

u32	_rtw_init_sta_priv(struct	sta_priv *pstapriv)
{
	struct sta_info *psta;
	s32 i;

_func_enter_;	

	pstapriv->pallocated_stainfo_buf = rtw_zvmalloc (sizeof(struct sta_info) * NUM_STA+ 4);
	
	if(!pstapriv->pallocated_stainfo_buf)
		return _FAIL;

	pstapriv->pstainfo_buf = pstapriv->pallocated_stainfo_buf + 4 - 
		((SIZE_PTR)(pstapriv->pallocated_stainfo_buf ) & 3);

	_rtw_init_queue(&pstapriv->free_sta_queue);

	_rtw_spinlock_init(&pstapriv->sta_hash_lock);
	
	//_rtw_init_queue(&pstapriv->asoc_q);
	pstapriv->asoc_sta_count = 0;
	_rtw_init_queue(&pstapriv->sleep_q);
	_rtw_init_queue(&pstapriv->wakeup_q);

	psta = (struct sta_info *)(pstapriv->pstainfo_buf);

		
	for(i = 0; i < NUM_STA; i++)
	{
		_rtw_init_stainfo(psta);

		_rtw_init_listhead(&(pstapriv->sta_hash[i]));

		rtw_list_insert_tail(&psta->list, get_list_head(&pstapriv->free_sta_queue));

		psta++;
	}

	

#ifdef CONFIG_AP_MODE

	pstapriv->sta_dz_bitmap = 0;
	pstapriv->tim_bitmap = 0;

	_rtw_init_listhead(&pstapriv->asoc_list);
	_rtw_init_listhead(&pstapriv->auth_list);
	_rtw_spinlock_init(&pstapriv->asoc_list_lock);
	_rtw_spinlock_init(&pstapriv->auth_list_lock);
	pstapriv->asoc_list_cnt = 0;
	pstapriv->auth_list_cnt = 0;

	pstapriv->auth_to = 3; // 3*2 = 6 sec 
	pstapriv->assoc_to = 3;
	//pstapriv->expire_to = 900;// 900*2 = 1800 sec = 30 min, expire after no any traffic.
	//pstapriv->expire_to = 30;// 30*2 = 60 sec = 1 min, expire after no any traffic.
#ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
	pstapriv->expire_to = 3; // 3*2 = 6 sec
#else
	pstapriv->expire_to = 60;// 60*2 = 120 sec = 2 min, expire after no any traffic.
#endif	
#ifdef CONFIG_ATMEL_RC_PATCH
	_rtw_memset(  pstapriv->atmel_rc_pattern, 0, ETH_ALEN);
#endif	
	pstapriv->max_num_sta = NUM_STA;
		
#endif
	
_func_exit_;		

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

void	_rtw_free_sta_xmit_priv_lock(struct sta_xmit_priv *psta_xmitpriv);
void	_rtw_free_sta_xmit_priv_lock(struct sta_xmit_priv *psta_xmitpriv)
{
_func_enter_;

	_rtw_spinlock_free(&psta_xmitpriv->lock);

	_rtw_spinlock_free(&(psta_xmitpriv->be_q.sta_pending.lock));
	_rtw_spinlock_free(&(psta_xmitpriv->bk_q.sta_pending.lock));
	_rtw_spinlock_free(&(psta_xmitpriv->vi_q.sta_pending.lock));
	_rtw_spinlock_free(&(psta_xmitpriv->vo_q.sta_pending.lock));
_func_exit_;	
}

static void	_rtw_free_sta_recv_priv_lock(struct sta_recv_priv *psta_recvpriv)
{
_func_enter_;	

	_rtw_spinlock_free(&psta_recvpriv->lock);

	_rtw_spinlock_free(&(psta_recvpriv->defrag_q.lock));

_func_exit_;

}

void rtw_mfree_stainfo(struct sta_info *psta);
void rtw_mfree_stainfo(struct sta_info *psta)
{
_func_enter_;

	if(&psta->lock != NULL)
		 _rtw_spinlock_free(&psta->lock);

	_rtw_free_sta_xmit_priv_lock(&psta->sta_xmitpriv);
	_rtw_free_sta_recv_priv_lock(&psta->sta_recvpriv);
	
_func_exit_;	
}


// this function is used to free the memory of lock || sema for all stainfos
void rtw_mfree_all_stainfo(struct sta_priv *pstapriv );
void rtw_mfree_all_stainfo(struct sta_priv *pstapriv )
{
	_irqL	 irqL;
	_list	*plist, *phead;
	struct sta_info *psta = NULL;
	
_func_enter_;	

	_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	phead = get_list_head(&pstapriv->free_sta_queue);
	plist = get_next(phead);
		
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
	{
		psta = LIST_CONTAINOR(plist, struct sta_info ,list);
		plist = get_next(plist);

		rtw_mfree_stainfo(psta);
	}
	
	_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

_func_exit_;	

}

void rtw_mfree_sta_priv_lock(struct	sta_priv *pstapriv);
void rtw_mfree_sta_priv_lock(struct	sta_priv *pstapriv)
{
#ifdef CONFIG_AP_MODE
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
#endif

	 rtw_mfree_all_stainfo(pstapriv); //be done before free sta_hash_lock

	_rtw_spinlock_free(&pstapriv->free_sta_queue.lock);

	_rtw_spinlock_free(&pstapriv->sta_hash_lock);
	_rtw_spinlock_free(&pstapriv->wakeup_q.lock);
	_rtw_spinlock_free(&pstapriv->sleep_q.lock);

#ifdef CONFIG_AP_MODE
	_rtw_spinlock_free(&pstapriv->asoc_list_lock);
	_rtw_spinlock_free(&pstapriv->auth_list_lock);
	_rtw_spinlock_free(&pacl_list->acl_node_q.lock);
#endif

}

u32	_rtw_free_sta_priv(struct	sta_priv *pstapriv)
{
	_irqL 	irqL;
	_list	*phead, *plist;
	struct sta_info *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	int 	index;

_func_enter_;
	if(pstapriv){

		/*	delete all reordering_ctrl_timer		*/ 
		_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);
		for(index = 0; index < NUM_STA; index++)
		{
			phead = &(pstapriv->sta_hash[index]);
			plist = get_next(phead);
			
			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
			{
				int i;	
				psta = LIST_CONTAINOR(plist, struct sta_info ,hash_list);
				plist = get_next(plist);

				for(i=0; i < 16 ; i++)
				{
					preorder_ctrl = &psta->recvreorder_ctrl[i];
					_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);	
				}
			}
		}
		_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);
		/*===============================*/
		
		rtw_mfree_sta_priv_lock(pstapriv);

		if(pstapriv->pallocated_stainfo_buf) {
			rtw_vmfree(pstapriv->pallocated_stainfo_buf, sizeof(struct sta_info)*NUM_STA+4);
		}
	}
	
_func_exit_;
	return _SUCCESS;
}


//struct	sta_info *rtw_alloc_stainfo(_queue *pfree_sta_queue, unsigned char *hwaddr)
struct	sta_info *rtw_alloc_stainfo(struct	sta_priv *pstapriv, u8 *hwaddr) 
{	
	_irqL irqL, irqL2;
	uint tmp_aid;
	s32	index;
	_list	*phash_list;
	struct sta_info	*psta;
	_queue *pfree_sta_queue;
	struct recv_reorder_ctrl *preorder_ctrl;
	int i = 0;
	u16  wRxSeqInitialValue = 0xffff;
	
_func_enter_;	

	pfree_sta_queue = &pstapriv->free_sta_queue;

	//_enter_critical_bh(&(pfree_sta_queue->lock), &irqL);
	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL2);
	if (_rtw_queue_empty(pfree_sta_queue) == _TRUE)
	{
		//_exit_critical_bh(&(pfree_sta_queue->lock), &irqL);
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL2);
		psta = NULL;
	}
	else
	{
		psta = LIST_CONTAINOR(get_next(&pfree_sta_queue->queue), struct sta_info, list);
		
		rtw_list_delete(&(psta->list));

		//_exit_critical_bh(&(pfree_sta_queue->lock), &irqL);
		
		tmp_aid = psta->aid;	
	
		_rtw_init_stainfo(psta);

		psta->padapter = pstapriv->padapter;

		_rtw_memcpy(psta->hwaddr, hwaddr, ETH_ALEN);

		index = wifi_mac_hash(hwaddr);

		RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_info_,("rtw_alloc_stainfo: index  = %x", index));

		if(index >= NUM_STA){
			RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_err_,("ERROR=> rtw_alloc_stainfo: index >= NUM_STA"));
			psta= NULL;	
			goto exit;
		}
		phash_list = &(pstapriv->sta_hash[index]);

		//_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL2);

		rtw_list_insert_tail(&psta->hash_list, phash_list);

		pstapriv->asoc_sta_count ++ ;

		//_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL2);

// Commented by Albert 2009/08/13
// For the SMC router, the sequence number of first packet of WPS handshake will be 0.
// In this case, this packet will be dropped by recv_decache function if we use the 0x00 as the default value for tid_rxseq variable.
// So, we initialize the tid_rxseq variable as the 0xffff.

		for( i = 0; i < 16; i++ )
		{
                     _rtw_memcpy( &psta->sta_recvpriv.rxcache.tid_rxseq[ i ], &wRxSeqInitialValue, 2 );
		}

		RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_info_,("alloc number_%d stainfo  with hwaddr = %x %x %x %x %x %x  \n", 
		pstapriv->asoc_sta_count , hwaddr[0], hwaddr[1], hwaddr[2],hwaddr[3],hwaddr[4],hwaddr[5]));

		init_addba_retry_timer(pstapriv->padapter, psta);

#ifdef CONFIG_TDLS
		rtw_init_tdls_timer(pstapriv->padapter, psta);
#endif //CONFIG_TDLS

		//for A-MPDU Rx reordering buffer control
		for(i=0; i < 16 ; i++)
		{
			preorder_ctrl = &psta->recvreorder_ctrl[i];

			preorder_ctrl->padapter = pstapriv->padapter;
		
			preorder_ctrl->enable = _FALSE;
		
			preorder_ctrl->indicate_seq = 0xffff;
			#ifdef DBG_RX_SEQ
			DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d\n", __FUNCTION__, __LINE__,
				preorder_ctrl->indicate_seq);
			#endif
			preorder_ctrl->wend_b= 0xffff;       
			//preorder_ctrl->wsize_b = (NR_RECVBUFF-2);
			preorder_ctrl->wsize_b = 64;//64;

			_rtw_init_queue(&preorder_ctrl->pending_recvframe_queue);

			rtw_init_recv_timer(preorder_ctrl);
		}


		//init for DM
		psta->rssi_stat.UndecoratedSmoothedPWDB = (-1);
		psta->rssi_stat.UndecoratedSmoothedCCK = (-1);
#ifdef CONFIG_ATMEL_RC_PATCH
		psta->flag_atmel_rc = 0;
#endif
		/* init for the sequence number of received management frame */
		psta->RxMgmtFrameSeqNum = 0xffff;

		//alloc mac id for non-bc/mc station,
		rtw_alloc_macid(pstapriv->padapter, psta);

	}
	
exit:

	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL2);

_func_exit_;

	return psta;


}


// using pstapriv->sta_hash_lock to protect
u32	rtw_free_stainfo(_adapter *padapter , struct sta_info *psta)
{	
	int i;
	_irqL irqL0;
	_queue *pfree_sta_queue;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct	sta_xmit_priv	*pstaxmitpriv;
	struct	xmit_priv	*pxmitpriv= &padapter->xmitpriv;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct hw_xmit *phwxmit;
	int pending_qcnt[4];

_func_enter_;	
	
	if (psta == NULL)
		goto exit;

	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL0);
	rtw_list_delete(&psta->hash_list);
	RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_err_,("\n free number_%d stainfo  with hwaddr = 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x  \n",pstapriv->asoc_sta_count , psta->hwaddr[0], psta->hwaddr[1], psta->hwaddr[2],psta->hwaddr[3],psta->hwaddr[4],psta->hwaddr[5]));
	pstapriv->asoc_sta_count --;
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL0);

	_enter_critical_bh(&psta->lock, &irqL0);
	psta->state &= ~_FW_LINKED;
	_exit_critical_bh(&psta->lock, &irqL0);

	pfree_sta_queue = &pstapriv->free_sta_queue;


	pstaxmitpriv = &psta->sta_xmitpriv;
	
	//rtw_list_delete(&psta->sleep_list);
	
	//rtw_list_delete(&psta->wakeup_list);
	
	_enter_critical_bh(&pxmitpriv->lock, &irqL0);
	
	rtw_free_xmitframe_queue(pxmitpriv, &psta->sleep_q);
	psta->sleepq_len = 0;
	
	//vo
	//_enter_critical_bh(&(pxmitpriv->vo_pending.lock), &irqL0);
	rtw_free_xmitframe_queue( pxmitpriv, &pstaxmitpriv->vo_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->vo_q.tx_pending));
	phwxmit = pxmitpriv->hwxmits;
	phwxmit->accnt -= pstaxmitpriv->vo_q.qcnt;
	pending_qcnt[0] = pstaxmitpriv->vo_q.qcnt;
	pstaxmitpriv->vo_q.qcnt = 0;
	//_exit_critical_bh(&(pxmitpriv->vo_pending.lock), &irqL0);

	//vi
	//_enter_critical_bh(&(pxmitpriv->vi_pending.lock), &irqL0);
	rtw_free_xmitframe_queue( pxmitpriv, &pstaxmitpriv->vi_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->vi_q.tx_pending));
	phwxmit = pxmitpriv->hwxmits+1;
	phwxmit->accnt -= pstaxmitpriv->vi_q.qcnt;
	pending_qcnt[1] = pstaxmitpriv->vi_q.qcnt;
	pstaxmitpriv->vi_q.qcnt = 0;
	//_exit_critical_bh(&(pxmitpriv->vi_pending.lock), &irqL0);

	//be
	//_enter_critical_bh(&(pxmitpriv->be_pending.lock), &irqL0);
	rtw_free_xmitframe_queue( pxmitpriv, &pstaxmitpriv->be_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->be_q.tx_pending));
	phwxmit = pxmitpriv->hwxmits+2;
	phwxmit->accnt -= pstaxmitpriv->be_q.qcnt;
	pending_qcnt[2] = pstaxmitpriv->be_q.qcnt;
	pstaxmitpriv->be_q.qcnt = 0;
	//_exit_critical_bh(&(pxmitpriv->be_pending.lock), &irqL0);
	
	//bk
	//_enter_critical_bh(&(pxmitpriv->bk_pending.lock), &irqL0);
	rtw_free_xmitframe_queue( pxmitpriv, &pstaxmitpriv->bk_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->bk_q.tx_pending));
	phwxmit = pxmitpriv->hwxmits+3;
	phwxmit->accnt -= pstaxmitpriv->bk_q.qcnt;
	pending_qcnt[3] = pstaxmitpriv->bk_q.qcnt;
	pstaxmitpriv->bk_q.qcnt = 0;
	//_exit_critical_bh(&(pxmitpriv->bk_pending.lock), &irqL0);

	rtw_os_wake_queue_at_free_stainfo(padapter, pending_qcnt);

	_exit_critical_bh(&pxmitpriv->lock, &irqL0);
	
	// re-init sta_info; 20061114 // will be init in alloc_stainfo
	//_rtw_init_sta_xmit_priv(&psta->sta_xmitpriv);
	//_rtw_init_sta_recv_priv(&psta->sta_recvpriv);

	_cancel_timer_ex(&psta->addba_retry_timer);

#ifdef CONFIG_TDLS
	rtw_free_tdls_timer(psta);
#endif //CONFIG_TDLS

	//for A-MPDU Rx reordering buffer control, cancel reordering_ctrl_timer
	for(i=0; i < 16 ; i++)
	{
		_irqL irqL;
		_list	*phead, *plist;
		union recv_frame *prframe;
		_queue *ppending_recvframe_queue;
		_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
	
		preorder_ctrl = &psta->recvreorder_ctrl[i];
		
		_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);		

		
		ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

		_enter_critical_bh(&ppending_recvframe_queue->lock, &irqL);

		phead = 	get_list_head(ppending_recvframe_queue);
		plist = get_next(phead);
		
		while(!rtw_is_list_empty(phead))
		{	
			prframe = LIST_CONTAINOR(plist, union recv_frame, u);
			
			plist = get_next(plist);
			
			rtw_list_delete(&(prframe->u.hdr.list));

			rtw_free_recvframe(prframe, pfree_recv_queue);
		}

		_exit_critical_bh(&ppending_recvframe_queue->lock, &irqL);
		
	}

	if (!(psta->state & WIFI_AP_STATE))
		rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, _FALSE);
			

	//release mac id for non-bc/mc station,
	rtw_release_macid(pstapriv->padapter, psta);

#ifdef CONFIG_AP_MODE

/*
	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL0);
	rtw_list_delete(&psta->asoc_list);	
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL0);
*/
	_enter_critical_bh(&pstapriv->auth_list_lock, &irqL0);
	if (!rtw_is_list_empty(&psta->auth_list)) {
		rtw_list_delete(&psta->auth_list);
		pstapriv->auth_list_cnt--;
	}
	_exit_critical_bh(&pstapriv->auth_list_lock, &irqL0);
	
	psta->expire_to = 0;
#ifdef CONFIG_ATMEL_RC_PATCH
	psta->flag_atmel_rc = 0;
#endif
	psta->sleepq_ac_len = 0;
	psta->qos_info = 0;

	psta->max_sp_len = 0;
	psta->uapsd_bk = 0;
	psta->uapsd_be = 0;
	psta->uapsd_vi = 0;
	psta->uapsd_vo = 0;

	psta->has_legacy_ac = 0;

#ifdef CONFIG_NATIVEAP_MLME
	
	pstapriv->sta_dz_bitmap &=~BIT(psta->aid);
	pstapriv->tim_bitmap &=~BIT(psta->aid);	

	//rtw_indicate_sta_disassoc_event(padapter, psta);

	if ((psta->aid >0)&&(pstapriv->sta_aid[psta->aid - 1] == psta))
	{
		pstapriv->sta_aid[psta->aid - 1] = NULL;
		psta->aid = 0;
	}	
	
#endif	// CONFIG_NATIVEAP_MLME	

#ifdef CONFIG_TX_MCAST2UNI
	psta->under_exist_checking = 0;
#endif	// CONFIG_TX_MCAST2UNI

#endif	// CONFIG_AP_MODE	

	 _rtw_spinlock_free(&psta->lock);

	//_enter_critical_bh(&(pfree_sta_queue->lock), &irqL0);
	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL0);
	rtw_list_insert_tail(&psta->list, get_list_head(pfree_sta_queue));
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL0);
	//_exit_critical_bh(&(pfree_sta_queue->lock), &irqL0);

exit:
	
_func_exit_;	

	return _SUCCESS;
	
}

// free all stainfo which in sta_hash[all]
void rtw_free_all_stainfo(_adapter *padapter)
{
	_irqL	 irqL;
	_list	*plist, *phead;
	s32	index;
	struct sta_info *psta = NULL;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info* pbcmc_stainfo =rtw_get_bcmc_stainfo( padapter);
	u8 free_sta_num = 0;
	char free_sta_list[NUM_STA];
	int stainfo_offset;
	
_func_enter_;	

	if(pstapriv->asoc_sta_count==1)
		goto exit;

	_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	for(index=0; index< NUM_STA; index++)
	{
		phead = &(pstapriv->sta_hash[index]);
		plist = get_next(phead);
		
		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
		{
			psta = LIST_CONTAINOR(plist, struct sta_info ,hash_list);

			plist = get_next(plist);

			if(pbcmc_stainfo!=psta)
			{
				rtw_list_delete(&psta->hash_list);
				//rtw_free_stainfo(padapter , psta);
				stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
				if (stainfo_offset_valid(stainfo_offset)) {
					free_sta_list[free_sta_num++] = stainfo_offset;
				}
			}
		}
	}
	
	_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	for (index = 0; index < free_sta_num; index++) 
	{
		psta = rtw_get_stainfo_by_offset(pstapriv, free_sta_list[index]);
		rtw_free_stainfo(padapter , psta);
	}
	
exit:	
	
_func_exit_;	

}

/* any station allocated can be searched by hash list */
struct sta_info *rtw_get_stainfo(struct sta_priv *pstapriv, u8 *hwaddr)
{

	_irqL	 irqL;

	_list	*plist, *phead;

	struct sta_info *psta = NULL;
	
	u32	index;

	u8 *addr;

	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};

_func_enter_;

	if(hwaddr==NULL)
		return NULL;
		
	if(IS_MCAST(hwaddr))
	{
		addr = bc_addr;
	}
	else
	{
		addr = hwaddr;
	}

	index = wifi_mac_hash(addr);

	_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);
	
	phead = &(pstapriv->sta_hash[index]);
	plist = get_next(phead);


	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
	{
	
		psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
		
		if ((_rtw_memcmp(psta->hwaddr, addr, ETH_ALEN))== _TRUE) 
		{ // if found the matched address
			break;
		}
		psta=NULL;
		plist = get_next(plist);
	}

	_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);
_func_exit_;	
	return psta;
	
}

u32 rtw_init_bcmc_stainfo(_adapter* padapter)
{

	struct sta_info 	*psta;
	struct tx_servq	*ptxservq;
	u32 res=_SUCCESS;
	NDIS_802_11_MAC_ADDRESS	bcast_addr= {0xff,0xff,0xff,0xff,0xff,0xff};
	
	struct	sta_priv *pstapriv = &padapter->stapriv;
	//_queue	*pstapending = &padapter->xmitpriv.bm_pending; 
	
_func_enter_;

	psta = rtw_alloc_stainfo(pstapriv, bcast_addr);
	
	if(psta==NULL){
		res=_FAIL;
		RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_err_,("rtw_alloc_stainfo fail"));
		goto exit;
	}

	// default broadcast & multicast use macid 1
	psta->mac_id = 1;

	ptxservq= &(psta->sta_xmitpriv.be_q);

/*
	_enter_critical(&pstapending->lock, &irqL0);

	if (rtw_is_list_empty(&ptxservq->tx_pending))
		rtw_list_insert_tail(&ptxservq->tx_pending, get_list_head(pstapending));

	_exit_critical(&pstapending->lock, &irqL0);
*/
	
exit:
_func_exit_;		
	return _SUCCESS;

}


struct sta_info* rtw_get_bcmc_stainfo(_adapter* padapter)
{
	struct sta_info 	*psta;
	struct sta_priv 	*pstapriv = &padapter->stapriv;
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
_func_enter_;
	 psta = rtw_get_stainfo(pstapriv, bc_addr);
_func_exit_;		 
	return psta;

}

u8 rtw_access_ctrl(_adapter *padapter, u8 *mac_addr)
{
	u8 res = _TRUE;
#ifdef  CONFIG_AP_MODE
	_irqL irqL;
	_list	*plist, *phead;
	struct rtw_wlan_acl_node *paclnode;
	u8 match = _FALSE;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct wlan_acl_pool *pacl_list = &pstapriv->acl_list;
	_queue	*pacl_node_q =&pacl_list->acl_node_q;
	
	_enter_critical_bh(&(pacl_node_q->lock), &irqL);
	phead = get_list_head(pacl_node_q);
	plist = get_next(phead);		
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
	{
		paclnode = LIST_CONTAINOR(plist, struct rtw_wlan_acl_node, list);
		plist = get_next(plist);

		if(_rtw_memcmp(paclnode->addr, mac_addr, ETH_ALEN))
		{
			if(paclnode->valid == _TRUE)
			{
				match = _TRUE;
				break;
			}
		}		
	}	
	_exit_critical_bh(&(pacl_node_q->lock), &irqL);
	

	if(pacl_list->mode == 1)//accept unless in deny list
	{
		res = (match == _TRUE) ?  _FALSE:_TRUE;
	}	
	else if(pacl_list->mode == 2)//deny unless in accept list
	{
		res = (match == _TRUE) ?  _TRUE:_FALSE;
	}
	else
	{
		 res = _TRUE;
	}		
	
#endif

	return res;

}

