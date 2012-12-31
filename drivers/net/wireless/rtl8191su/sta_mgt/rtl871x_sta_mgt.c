/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#define _RTL871X_STA_MGT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>


#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

#include <sta_info.h>


static void _init_stainfo(struct sta_info *psta)
{

_func_enter_;

	_memset((u8 *)psta, 0, sizeof (struct sta_info));

	 _spinlock_init(&psta->lock);
	_init_listhead(&psta->list);
	_init_listhead(&psta->hash_list);
	//_init_listhead(&psta->asoc_list);
	//_init_listhead(&psta->sleep_list);
	//_init_listhead(&psta->wakeup_list);	

	_init_sta_xmit_priv(&psta->sta_xmitpriv);
	_init_sta_recv_priv(&psta->sta_recvpriv);
	
#ifdef CONFIG_AP_MODE	
	_init_listhead(&psta->asoc_list);
	_init_listhead(&psta->auth_list);
#endif	
	
_func_exit_;	

}




u32	_init_sta_priv(struct	sta_priv *pstapriv)
{
	struct sta_info *psta;
	s32 i;
	
_func_enter_;	

	pstapriv->pallocated_stainfo_buf = _vmalloc (sizeof(struct sta_info) * NUM_STA+ 4);

	if(pstapriv->pallocated_stainfo_buf == NULL){
		RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_err_,("_init_sta_priv : alloc stainfo_buf  FAIL !\n"));
		return _FAIL;
	}

	pstapriv->pstainfo_buf = pstapriv->pallocated_stainfo_buf + 4 - 
		((unsigned int)(pstapriv->pallocated_stainfo_buf ) & 3);

	_init_queue(&pstapriv->free_sta_queue);

	_spinlock_init(&pstapriv->sta_hash_lock);
	
	//_init_queue(&pstapriv->asoc_q);
	pstapriv->asoc_sta_count = 0;
	_init_queue(&pstapriv->sleep_q);
	_init_queue(&pstapriv->wakeup_q);

	psta = (struct sta_info *)(pstapriv->pstainfo_buf);

		
	for(i = 0; i < NUM_STA; i++)
	{
		_init_stainfo(psta);

		_init_listhead(&(pstapriv->sta_hash[i]));

		list_insert_tail(&psta->list, get_list_head(&pstapriv->free_sta_queue));

		psta++;
	}

#ifdef CONFIG_AP_MODE	
	_init_listhead(&pstapriv->asoc_list);
	_init_listhead(&pstapriv->auth_list);
	//pstapriv->auth_to = ???;
	//pstapriv->assoc_to = ???;
	//pstapriv->expire_to = ???;
#endif
	
_func_exit_;		

	return _SUCCESS;
	
}

void	_free_sta_xmit_priv_lock(struct sta_xmit_priv *psta_xmitpriv)
{
_func_enter_;

	_spinlock_free(&psta_xmitpriv->lock);

	_spinlock_free(&(psta_xmitpriv->be_q.sta_pending.lock));
	_spinlock_free(&(psta_xmitpriv->bk_q.sta_pending.lock));
	_spinlock_free(&(psta_xmitpriv->vi_q.sta_pending.lock));
	_spinlock_free(&(psta_xmitpriv->vo_q.sta_pending.lock));
_func_exit_;	
}

void	_free_sta_recv_priv_lock(struct sta_recv_priv *psta_recvpriv)
{
_func_enter_;	

	_spinlock_free(&psta_recvpriv->lock);

	_spinlock_free(&(psta_recvpriv->defrag_q.lock));

_func_exit_;

}

static void mfree_stainfo(struct sta_info *psta)
{
_func_enter_;

	if(&psta->lock != NULL)
		 _spinlock_free(&psta->lock);

	_free_sta_xmit_priv_lock(&psta->sta_xmitpriv);
	_free_sta_recv_priv_lock(&psta->sta_recvpriv);
	
_func_exit_;	
}


// this function is used to free the memory of lock || sema for all stainfos
void mfree_all_stainfo(struct sta_priv *pstapriv )
{
	_irqL	 irqL;
	_list	*plist, *phead;
	struct sta_info *psta = NULL;
	
_func_enter_;	

	_enter_critical(&pstapriv->sta_hash_lock, &irqL);

	phead = get_list_head(&pstapriv->free_sta_queue);
	plist = get_next(phead);
		
	while ((end_of_queue_search(phead, plist)) == _FALSE)
	{
		psta = LIST_CONTAINOR(plist, struct sta_info ,list);
		plist = get_next(plist);

		mfree_stainfo(psta);
	}
	
	_exit_critical(&pstapriv->sta_hash_lock, &irqL);

_func_exit_;	

}


void mfree_sta_priv_lock(struct	sta_priv *pstapriv)
{
	 mfree_all_stainfo(pstapriv); //be done before free sta_hash_lock

	_spinlock_free(&pstapriv->free_sta_queue.lock);

	_spinlock_free(&pstapriv->sta_hash_lock);
	_spinlock_free(&pstapriv->wakeup_q.lock);
	_spinlock_free(&pstapriv->sleep_q.lock);

}

u32	_free_sta_priv(struct	sta_priv *pstapriv)
{
_func_enter_;
	if(pstapriv){
		mfree_sta_priv_lock(pstapriv);
		if(pstapriv->pallocated_stainfo_buf)
			_vmfree(pstapriv->pallocated_stainfo_buf, (sizeof(struct sta_info) * NUM_STA+ 4));
	}
_func_exit_;
	return _SUCCESS;
}


//struct	sta_info *alloc_stainfo(_queue *pfree_sta_queue, unsigned char *hwaddr)
struct	sta_info *alloc_stainfo(struct	sta_priv *pstapriv, u8 *hwaddr) 
{
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

    {	
        unsigned long   flags;
        
        spin_lock_irqsave(&(pfree_sta_queue->lock), flags);
    
    	if (_queue_empty(pfree_sta_queue) == _TRUE)
    	{
    		psta = NULL;
    	}
    	else
    	{
    		psta = LIST_CONTAINOR(get_next(&pfree_sta_queue->queue), struct sta_info, list);
    		
    		list_delete(&(psta->list));
    	
    		tmp_aid = psta->aid;	
    	
    		_init_stainfo(psta);
    
    		_memcpy(psta->hwaddr, hwaddr, ETH_ALEN);
    
    		index = wifi_mac_hash(hwaddr);
    
    		RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_info_,("alloc_stainfo: index  = %x", index));
    
    		if(index >= NUM_STA){
    			RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_err_,("ERROR=> alloc_stainfo: index >= NUM_STA"));
    			psta= NULL;	
    			goto exit;
    		}
    		phash_list = &(pstapriv->sta_hash[index]);

            {
                unsigned long   iflags;
                
                spin_lock_irqsave(&(pstapriv->sta_hash_lock), iflags);
        
        		list_insert_tail(&psta->hash_list, phash_list);
        
        		pstapriv->asoc_sta_count ++ ;
        
                spin_unlock_irqrestore(&(pstapriv->sta_hash_lock), iflags);
            }
    
    // Commented by Albert 2009/08/13
    // For the SMC router, the sequence number of first packet of WPS handshake will be 0.
    // In this case, this packet will be dropped by recv_decache function if we use the 0x00 as the default value for tid_rxseq variable.
    // So, we initialize the tid_rxseq variable as the 0xffff.
    
    		for( i = 0; i < 16; i++ )
    		{
                         _memcpy( &psta->sta_recvpriv.rxcache.tid_rxseq[ i ], &wRxSeqInitialValue, 2 );
    		}
    
    		RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_info_,("alloc number_%d stainfo  with hwaddr = %x %x %x %x %x %x  \n", 
    		pstapriv->asoc_sta_count , hwaddr[0], hwaddr[1], hwaddr[2],hwaddr[3],hwaddr[4],hwaddr[5]));
    
    		
    
    		//for A-MPDU Rx reordering buffer control
    		for(i=0; i < 16 ; i++)
    		{
    			preorder_ctrl = &psta->recvreorder_ctrl[i];
    
    			preorder_ctrl->padapter = pstapriv->padapter;
    		
    			preorder_ctrl->indicate_seq = 0xffff;
    			preorder_ctrl->wend_b= 0xffff;       
    			//preorder_ctrl->wsize_b = (NR_RECVBUFF-2);
    			preorder_ctrl->wsize_b = 64;
    			preorder_ctrl->enable = _FALSE;
    
    			_init_queue(&preorder_ctrl->pending_recvframe_queue);
    
    			init_recv_timer(preorder_ctrl);
    		}
    
    	}
exit:

        spin_unlock_irqrestore(&(pfree_sta_queue->lock), flags);
	}
_func_exit_;	

	return psta;


}


// using pstapriv->sta_hash_lock to protect
u32	free_stainfo(_adapter *padapter , struct sta_info *psta)
{	
	int i;
	_irqL irqL0;
	_queue *pfree_sta_queue;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct	sta_xmit_priv	*pstaxmitpriv;
	struct	xmit_priv	*pxmitpriv= &padapter->xmitpriv;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	

_func_enter_;	
	
	if (psta == NULL)
		goto exit;

	pfree_sta_queue = &pstapriv->free_sta_queue;


	pstaxmitpriv = &psta->sta_xmitpriv;
	
	//list_delete(&psta->sleep_list);
	
	//list_delete(&psta->wakeup_list);
	
	_enter_critical(&(pxmitpriv->vo_pending.lock), &irqL0);

	free_xmitframe_queue( pxmitpriv, &pstaxmitpriv->vo_q.sta_pending);

	list_delete(&(pstaxmitpriv->vo_q.tx_pending));

	_exit_critical(&(pxmitpriv->vo_pending.lock), &irqL0);
	

	_enter_critical(&(pxmitpriv->vi_pending.lock), &irqL0);

	free_xmitframe_queue( pxmitpriv, &pstaxmitpriv->vi_q.sta_pending);

	list_delete(&(pstaxmitpriv->vi_q.tx_pending));

	_exit_critical(&(pxmitpriv->vi_pending.lock), &irqL0);


	_enter_critical(&(pxmitpriv->bk_pending.lock), &irqL0);

	free_xmitframe_queue( pxmitpriv, &pstaxmitpriv->bk_q.sta_pending);

	list_delete(&(pstaxmitpriv->bk_q.tx_pending));

	_exit_critical(&(pxmitpriv->bk_pending.lock), &irqL0);

	_enter_critical(&(pxmitpriv->be_pending.lock), &irqL0);

	free_xmitframe_queue( pxmitpriv, &pstaxmitpriv->be_q.sta_pending);

	list_delete(&(pstaxmitpriv->be_q.tx_pending));

	_exit_critical(&(pxmitpriv->be_pending.lock), &irqL0);
	
	
	list_delete(&psta->hash_list);
	RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_err_,("\n free number_%d stainfo  with hwaddr = 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x  \n",pstapriv->asoc_sta_count , psta->hwaddr[0], psta->hwaddr[1], psta->hwaddr[2],psta->hwaddr[3],psta->hwaddr[4],psta->hwaddr[5]));
	pstapriv->asoc_sta_count --;
	
	
	// re-init sta_info; 20061114
	_init_sta_xmit_priv(&psta->sta_xmitpriv);
	_init_sta_recv_priv(&psta->sta_recvpriv);


	//for A-MPDU Rx reordering buffer control, cancel reordering_ctrl_timer
	for(i=0; i < 16 ; i++)
	{
		preorder_ctrl = &psta->recvreorder_ctrl[i];
		
		_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);		
	}

    {
        unsigned long   flags;
        
        spin_lock_irqsave(&(pfree_sta_queue->lock), flags);

    	// insert into free_sta_queue; 20061114
    	list_insert_tail(&psta->list, get_list_head(pfree_sta_queue));

        spin_unlock_irqrestore(&(pfree_sta_queue->lock), flags);
    }
	

exit:	
	
_func_exit_;	

	return _SUCCESS;
	
}

// free all stainfo which in sta_hash[all]
void free_all_stainfo(_adapter *padapter)
{
	_irqL	 irqL;
	_list	*plist, *phead;
	s32	index;
	struct sta_info *psta = NULL;
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info* pbcmc_stainfo =get_bcmc_stainfo( padapter);
	
_func_enter_;	

	if(pstapriv->asoc_sta_count==1)
		goto exit;

	_enter_critical(&pstapriv->sta_hash_lock, &irqL);

	for(index=0; index< NUM_STA; index++)
	{
		phead = &(pstapriv->sta_hash[index]);
		plist = get_next(phead);
		
		while ((end_of_queue_search(phead, plist)) == _FALSE)
		{
			psta = LIST_CONTAINOR(plist, struct sta_info ,hash_list);

			plist = get_next(plist);

			if(pbcmc_stainfo!=psta)					
				free_stainfo(padapter , psta);
			
		}
	}
	
	_exit_critical(&pstapriv->sta_hash_lock, &irqL);
	
exit:	
	
_func_exit_;	

}

/* any station allocated can be searched by hash list */
struct sta_info *get_stainfo(struct sta_priv *pstapriv, u8 *hwaddr)
{

	_irqL	 irqL;

	_list	*plist, *phead;

	struct sta_info *psta = NULL;
	
	u32	index;

_func_enter_;

	if(hwaddr==NULL)
		return NULL;
		

	index = wifi_mac_hash(hwaddr);

	_enter_critical(&pstapriv->sta_hash_lock, &irqL);
	
	phead = &(pstapriv->sta_hash[index]);
	plist = get_next(phead);


	while ((end_of_queue_search(phead, plist)) == _FALSE)
	{
	
		psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
		
		if ((_memcmp(psta->hwaddr,hwaddr, ETH_ALEN))== _TRUE) 
		{ // if found the matched address
			break;
		}
		psta=NULL;
		plist = get_next(plist);
	}

	_exit_critical(&pstapriv->sta_hash_lock, &irqL);
_func_exit_;	
	return psta;
	
}

u32 init_bcmc_stainfo(_adapter* padapter)
{

	struct sta_info 	*psta;
	struct tx_servq	*ptxservq;
	u32 res=_SUCCESS;
	NDIS_802_11_MAC_ADDRESS	bcast_addr= {0xff,0xff,0xff,0xff,0xff,0xff};
	
	struct	sta_priv *pstapriv = &padapter->stapriv;
	_queue	*pstapending = &padapter->xmitpriv.bm_pending; 
	
_func_enter_;

	psta = alloc_stainfo(pstapriv, bcast_addr);
	
	if(psta==NULL){
		res=_FAIL;
		RT_TRACE(_module_rtl871x_sta_mgt_c_,_drv_err_,("alloc_stainfo fail"));
		goto exit;
	}
	
	ptxservq= &(psta->sta_xmitpriv.be_q);

/*
	_enter_critical(&pstapending->lock, &irqL0);

	if (is_list_empty(&ptxservq->tx_pending))
		list_insert_tail(&ptxservq->tx_pending, get_list_head(pstapending));

	_exit_critical(&pstapending->lock, &irqL0);
*/
	
exit:
_func_exit_;		
	return _SUCCESS;

}


struct sta_info* get_bcmc_stainfo(_adapter* padapter)
{
	struct sta_info 	*psta;
	struct sta_priv 	*pstapriv = &padapter->stapriv;
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
_func_enter_;
	 psta = get_stainfo(pstapriv, bc_addr);
_func_exit_;		 
	return psta;

}


u8 access_ctrl(struct wlan_acl_pool* pacl_list, u8 * mac_addr)
{
	return _TRUE;
}






