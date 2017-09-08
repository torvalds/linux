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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_RECV_C_

#include <drv_types.h>
#include <hal_data.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif


#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
void rtw_signal_stat_timer_hdl(RTW_TIMER_HDL_ARGS);

enum {
	SIGNAL_STAT_CALC_PROFILE_0 = 0,
	SIGNAL_STAT_CALC_PROFILE_1,
	SIGNAL_STAT_CALC_PROFILE_MAX
};

u8 signal_stat_calc_profile[SIGNAL_STAT_CALC_PROFILE_MAX][2] = {
	{4, 1},	/* Profile 0 => pre_stat : curr_stat = 4 : 1 */
	{3, 7}	/* Profile 1 => pre_stat : curr_stat = 3 : 7 */
};

#ifndef RTW_SIGNAL_STATE_CALC_PROFILE	
#define RTW_SIGNAL_STATE_CALC_PROFILE SIGNAL_STAT_CALC_PROFILE_1
#endif

#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

void _rtw_init_sta_recv_priv(struct sta_recv_priv *psta_recvpriv)
{


_func_enter_;

	_rtw_memset((u8 *)psta_recvpriv, 0, sizeof (struct sta_recv_priv));

	_rtw_spinlock_init(&psta_recvpriv->lock);

	//for(i=0; i<MAX_RX_NUMBLKS; i++)
	//	_rtw_init_queue(&psta_recvpriv->blk_strms[i]);

	_rtw_init_queue(&psta_recvpriv->defrag_q);

_func_exit_;

}

sint _rtw_init_recv_priv(struct recv_priv *precvpriv, _adapter *padapter)
{
	sint i;

	union recv_frame *precvframe;

	sint	res=_SUCCESS;

_func_enter_;

	// We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc().
	//_rtw_memset((unsigned char *)precvpriv, 0, sizeof (struct  recv_priv));

	_rtw_spinlock_init(&precvpriv->lock);

	_rtw_init_queue(&precvpriv->free_recv_queue);
	_rtw_init_queue(&precvpriv->recv_pending_queue);
	_rtw_init_queue(&precvpriv->uc_swdec_pending_queue);

	precvpriv->adapter = padapter;

	precvpriv->free_recvframe_cnt = NR_RECVFRAME;

	precvpriv->sink_udpport = 0;
	precvpriv->pre_rtp_rxseq = 0;
	precvpriv->cur_rtp_rxseq = 0;

	rtw_os_recv_resource_init(precvpriv, padapter);

	precvpriv->pallocated_frame_buf = rtw_zvmalloc(NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);
	
	if(precvpriv->pallocated_frame_buf==NULL){
		res= _FAIL;
		goto exit;
	}
	//_rtw_memset(precvpriv->pallocated_frame_buf, 0, NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);

	precvpriv->precv_frame_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_frame_buf), RXFRAME_ALIGN_SZ);
	//precvpriv->precv_frame_buf = precvpriv->pallocated_frame_buf + RXFRAME_ALIGN_SZ -
	//						((SIZE_PTR) (precvpriv->pallocated_frame_buf) &(RXFRAME_ALIGN_SZ-1));

	precvframe = (union recv_frame*) precvpriv->precv_frame_buf;


	for(i=0; i < NR_RECVFRAME ; i++)
	{
		_rtw_init_listhead(&(precvframe->u.list));

		rtw_list_insert_tail(&(precvframe->u.list), &(precvpriv->free_recv_queue.queue));

		res = rtw_os_recv_resource_alloc(padapter, precvframe);

		precvframe->u.hdr.len = 0;

		precvframe->u.hdr.adapter =padapter;
		precvframe++;

	}

#ifdef CONFIG_USB_HCI

	ATOMIC_SET(&(precvpriv->rx_pending_cnt), 1);

	_rtw_init_sema(&precvpriv->allrxreturnevt, 0);

#endif

	res = rtw_hal_init_recv_priv(padapter);

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_init_timer(&precvpriv->signal_stat_timer, padapter, RTW_TIMER_HDL_NAME(signal_stat));

	precvpriv->signal_stat_sampling_interval = 2000; //ms
	//precvpriv->signal_stat_converging_constant = 5000; //ms

	rtw_set_signal_stat_timer(precvpriv);
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

exit:

_func_exit_;

	return res;

}

void rtw_mfree_recv_priv_lock(struct recv_priv *precvpriv);
void rtw_mfree_recv_priv_lock(struct recv_priv *precvpriv)
{
	_rtw_spinlock_free(&precvpriv->lock);
#ifdef CONFIG_RECV_THREAD_MODE	
	_rtw_free_sema(&precvpriv->recv_sema);
	_rtw_free_sema(&precvpriv->terminate_recvthread_sema);
#endif

	_rtw_spinlock_free(&precvpriv->free_recv_queue.lock);
	_rtw_spinlock_free(&precvpriv->recv_pending_queue.lock);

	_rtw_spinlock_free(&precvpriv->free_recv_buf_queue.lock);

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	_rtw_spinlock_free(&precvpriv->recv_buf_pending_queue.lock);
#endif	// CONFIG_USE_USB_BUFFER_ALLOC_RX
}

void _rtw_free_recv_priv (struct recv_priv *precvpriv)
{
	_adapter	*padapter = precvpriv->adapter;

_func_enter_;

	rtw_free_uc_swdec_pending_queue(padapter);

	rtw_mfree_recv_priv_lock(precvpriv);

	rtw_os_recv_resource_free(precvpriv);

	if(precvpriv->pallocated_frame_buf) {
		rtw_vmfree(precvpriv->pallocated_frame_buf, NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);
	}

	rtw_hal_free_recv_priv(padapter);

_func_exit_;

}

bool rtw_rframe_del_wfd_ie(union recv_frame *rframe, u8 ies_offset)
{
#define DBG_RFRAME_DEL_WFD_IE 0
	u8 *ies = rframe->u.hdr.rx_data + sizeof(struct rtw_ieee80211_hdr_3addr) + ies_offset;
	uint ies_len_ori = rframe->u.hdr.len - (ies - rframe->u.hdr.rx_data);
	uint ies_len;

	ies_len = rtw_del_wfd_ie(ies, ies_len_ori, DBG_RFRAME_DEL_WFD_IE ? __func__ : NULL);
	rframe->u.hdr.len -= ies_len_ori - ies_len;

	return ies_len_ori != ies_len;
}

union recv_frame *_rtw_alloc_recvframe (_queue *pfree_recv_queue)
{

	union recv_frame  *precvframe;
	_list	*plist, *phead;
	_adapter *padapter;
	struct recv_priv *precvpriv;
_func_enter_;

	if(_rtw_queue_empty(pfree_recv_queue) == _TRUE)
	{
		precvframe = NULL;
	}
	else
	{
		phead = get_list_head(pfree_recv_queue);

		plist = get_next(phead);

		precvframe = LIST_CONTAINOR(plist, union recv_frame, u);

		rtw_list_delete(&precvframe->u.hdr.list);
		padapter=precvframe->u.hdr.adapter;
		if(padapter !=NULL){
			precvpriv=&padapter->recvpriv;
			if(pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt--;
		}
	}

_func_exit_;

	return precvframe;

}

union recv_frame *rtw_alloc_recvframe (_queue *pfree_recv_queue)
{
	_irqL irqL;
	union recv_frame  *precvframe;
	
	_enter_critical_bh(&pfree_recv_queue->lock, &irqL);

	precvframe = _rtw_alloc_recvframe(pfree_recv_queue);

	_exit_critical_bh(&pfree_recv_queue->lock, &irqL);

	return precvframe;
}

void rtw_init_recvframe(union recv_frame *precvframe, struct recv_priv *precvpriv)
{
	/* Perry: This can be removed */
	_rtw_init_listhead(&precvframe->u.hdr.list);

	precvframe->u.hdr.len=0;
}

int rtw_free_recvframe(union recv_frame *precvframe, _queue *pfree_recv_queue)
{
	_irqL irqL;
	_adapter *padapter=precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

_func_enter_;

#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->adapter_type > PRIMARY_ADAPTER)
	{
		padapter = padapter->pbuddy_adapter;//get primary_padapter
		precvpriv = &padapter->recvpriv;
		pfree_recv_queue = &precvpriv->free_recv_queue;
		precvframe->u.hdr.adapter = padapter;		
	}	
#endif

	rtw_os_free_recvframe(precvframe);


	_enter_critical_bh(&pfree_recv_queue->lock, &irqL);

	rtw_list_delete(&(precvframe->u.hdr.list));

	precvframe->u.hdr.len = 0;

	rtw_list_insert_tail(&(precvframe->u.hdr.list), get_list_head(pfree_recv_queue));

	if(padapter !=NULL){
		if(pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt++;
	}

      _exit_critical_bh(&pfree_recv_queue->lock, &irqL);

_func_exit_;

	return _SUCCESS;

}




sint _rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{

	_adapter *padapter=precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

_func_enter_;

	//_rtw_init_listhead(&(precvframe->u.hdr.list));
	rtw_list_delete(&(precvframe->u.hdr.list));


	rtw_list_insert_tail(&(precvframe->u.hdr.list), get_list_head(queue));

	if (padapter != NULL) {
		if (queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;
	}

_func_exit_;

	return _SUCCESS;
}

sint rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{
	sint ret;
	_irqL irqL;
	
	//_spinlock(&pfree_recv_queue->lock);
	_enter_critical_bh(&queue->lock, &irqL);
	ret = _rtw_enqueue_recvframe(precvframe, queue);
	//_rtw_spinunlock(&pfree_recv_queue->lock);
	_exit_critical_bh(&queue->lock, &irqL);

	return ret;
}

/*
sint	rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{
	return rtw_free_recvframe(precvframe, queue);
}
*/




/*
caller : defrag ; recvframe_chk_defrag in recv_thread  (passive)
pframequeue: defrag_queue : will be accessed in recv_thread  (passive)

using spinlock to protect

*/

void rtw_free_recvframe_queue(_queue *pframequeue,  _queue *pfree_recv_queue)
{
	union	recv_frame 	*precvframe;
	_list	*plist, *phead;

_func_enter_;
	_rtw_spinlock(&pframequeue->lock);

	phead = get_list_head(pframequeue);
	plist = get_next(phead);

	while(rtw_end_of_queue_search(phead, plist) == _FALSE)
	{
		precvframe = LIST_CONTAINOR(plist, union recv_frame, u);

		plist = get_next(plist);

		//rtw_list_delete(&precvframe->u.hdr.list); // will do this in rtw_free_recvframe()

		rtw_free_recvframe(precvframe, pfree_recv_queue);
	}

	_rtw_spinunlock(&pframequeue->lock);

_func_exit_;

}

u32 rtw_free_uc_swdec_pending_queue(_adapter *adapter)
{
	u32 cnt = 0;
	union recv_frame *pending_frame;
	while((pending_frame=rtw_alloc_recvframe(&adapter->recvpriv.uc_swdec_pending_queue))) {
		rtw_free_recvframe(pending_frame, &adapter->recvpriv.free_recv_queue);
		cnt++;
	}

	if (cnt)
		DBG_871X(FUNC_ADPT_FMT" dequeue %d\n", FUNC_ADPT_ARG(adapter), cnt);

	return cnt;
}


sint rtw_enqueue_recvbuf_to_head(struct recv_buf *precvbuf, _queue *queue)
{
	_irqL irqL;

	_enter_critical_bh(&queue->lock, &irqL);

	rtw_list_delete(&precvbuf->list);
	rtw_list_insert_head(&precvbuf->list, get_list_head(queue));

	_exit_critical_bh(&queue->lock, &irqL);

	return _SUCCESS;
}

sint rtw_enqueue_recvbuf(struct recv_buf *precvbuf, _queue *queue)
{
	_irqL irqL;	
#ifdef CONFIG_SDIO_HCI
	_enter_critical_bh(&queue->lock, &irqL);
#else
	_enter_critical_ex(&queue->lock, &irqL);
#endif/*#ifdef  CONFIG_SDIO_HCI*/

	rtw_list_delete(&precvbuf->list);

	rtw_list_insert_tail(&precvbuf->list, get_list_head(queue));
#ifdef CONFIG_SDIO_HCI	
	_exit_critical_bh(&queue->lock, &irqL);
#else
	_exit_critical_ex(&queue->lock, &irqL);
#endif/*#ifdef  CONFIG_SDIO_HCI*/
	return _SUCCESS;
	
}

struct recv_buf *rtw_dequeue_recvbuf (_queue *queue)
{
	_irqL irqL;
	struct recv_buf *precvbuf;
	_list	*plist, *phead;	

#ifdef CONFIG_SDIO_HCI
	_enter_critical_bh(&queue->lock, &irqL);
#else
	_enter_critical_ex(&queue->lock, &irqL);
#endif/*#ifdef  CONFIG_SDIO_HCI*/
	
	if(_rtw_queue_empty(queue) == _TRUE)
	{
		precvbuf = NULL;
	}
	else
	{
		phead = get_list_head(queue);

		plist = get_next(phead);

		precvbuf = LIST_CONTAINOR(plist, struct recv_buf, list);

		rtw_list_delete(&precvbuf->list);
		
	}

#ifdef CONFIG_SDIO_HCI
	_exit_critical_bh(&queue->lock, &irqL);
#else
	_exit_critical_ex(&queue->lock, &irqL);
#endif/*#ifdef  CONFIG_SDIO_HCI*/

	return precvbuf;

}

sint recvframe_chkmic(_adapter *adapter,  union recv_frame *precvframe);
sint recvframe_chkmic(_adapter *adapter,  union recv_frame *precvframe){

	sint	i,res=_SUCCESS;
	u32	datalen;
	u8	miccode[8];
	u8	bmic_err=_FALSE,brpt_micerror = _TRUE;
	u8	*pframe, *payload,*pframemic;
	u8	*mickey;
	//u8	*iv,rxdata_key_idx=0;
	struct	sta_info		*stainfo;
	struct	rx_pkt_attrib	*prxattrib=&precvframe->u.hdr.attrib;
	struct 	security_priv	*psecuritypriv=&adapter->securitypriv;

	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
_func_enter_;

	stainfo=rtw_get_stainfo(&adapter->stapriv ,&prxattrib->ta[0]);

	if(prxattrib->encrypt ==_TKIP_)
	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n recvframe_chkmic:prxattrib->encrypt ==_TKIP_\n"));
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n recvframe_chkmic:da=0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
			prxattrib->ra[0],prxattrib->ra[1],prxattrib->ra[2],prxattrib->ra[3],prxattrib->ra[4],prxattrib->ra[5]));

		//calculate mic code
		if(stainfo!= NULL)
		{
			if(IS_MCAST(prxattrib->ra))
			{
				//mickey=&psecuritypriv->dot118021XGrprxmickey.skey[0];
				//iv = precvframe->u.hdr.rx_data+prxattrib->hdrlen;
				//rxdata_key_idx =( ((iv[3])>>6)&0x3) ;
				mickey=&psecuritypriv->dot118021XGrprxmickey[prxattrib->key_index].skey[0];
				
				RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n recvframe_chkmic: bcmc key \n"));
				//DBG_871X("\n recvframe_chkmic: bcmc key psecuritypriv->dot118021XGrpKeyid(%d),pmlmeinfo->key_index(%d) ,recv key_id(%d)\n",
				//								psecuritypriv->dot118021XGrpKeyid,pmlmeinfo->key_index,rxdata_key_idx);
				
				if(psecuritypriv->binstallGrpkey==_FALSE)
				{
					res=_FAIL;
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n recvframe_chkmic:didn't install group key!!!!!!!!!!\n"));
					DBG_871X("\n recvframe_chkmic:didn't install group key!!!!!!!!!!\n");
					goto exit;
				}
			}
			else{
				mickey=&stainfo->dot11tkiprxmickey.skey[0];
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n recvframe_chkmic: unicast key \n"));
			}

			datalen=precvframe->u.hdr.len-prxattrib->hdrlen-prxattrib->iv_len-prxattrib->icv_len-8;//icv_len included the mic code
			pframe=precvframe->u.hdr.rx_data;
			payload=pframe+prxattrib->hdrlen+prxattrib->iv_len;

			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n prxattrib->iv_len=%d prxattrib->icv_len=%d\n",prxattrib->iv_len,prxattrib->icv_len));

			//rtw_seccalctkipmic(&stainfo->dot11tkiprxmickey.skey[0],pframe,payload, datalen ,&miccode[0],(unsigned char)prxattrib->priority); //care the length of the data

			rtw_seccalctkipmic(mickey,pframe,payload, datalen ,&miccode[0],(unsigned char)prxattrib->priority); //care the length of the data

			pframemic=payload+datalen;

			bmic_err=_FALSE;

			for(i=0;i<8;i++){
				if(miccode[i] != *(pframemic+i)){
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvframe_chkmic:miccode[%d](%02x) != *(pframemic+%d)(%02x) ",i,miccode[i],i,*(pframemic+i)));
					bmic_err=_TRUE;
				}
			}


			if(bmic_err==_TRUE){

				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n *(pframemic-8)-*(pframemic-1)=0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
					*(pframemic-8),*(pframemic-7),*(pframemic-6),*(pframemic-5),*(pframemic-4),*(pframemic-3),*(pframemic-2),*(pframemic-1)));
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n *(pframemic-16)-*(pframemic-9)=0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
					*(pframemic-16),*(pframemic-15),*(pframemic-14),*(pframemic-13),*(pframemic-12),*(pframemic-11),*(pframemic-10),*(pframemic-9)));

				{
					uint i;
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n ======demp packet (len=%d)======\n",precvframe->u.hdr.len));
					for(i=0;i<precvframe->u.hdr.len;i=i+8){
						RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x",
							*(precvframe->u.hdr.rx_data+i),*(precvframe->u.hdr.rx_data+i+1),
							*(precvframe->u.hdr.rx_data+i+2),*(precvframe->u.hdr.rx_data+i+3),
							*(precvframe->u.hdr.rx_data+i+4),*(precvframe->u.hdr.rx_data+i+5),
							*(precvframe->u.hdr.rx_data+i+6),*(precvframe->u.hdr.rx_data+i+7)));
					}
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n ======demp packet end [len=%d]======\n",precvframe->u.hdr.len));
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n hrdlen=%d, \n",prxattrib->hdrlen));
				}

				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("ra=0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x psecuritypriv->binstallGrpkey=%d ",
					prxattrib->ra[0],prxattrib->ra[1],prxattrib->ra[2],
					prxattrib->ra[3],prxattrib->ra[4],prxattrib->ra[5],psecuritypriv->binstallGrpkey));

				// double check key_index for some timing issue ,
				// cannot compare with psecuritypriv->dot118021XGrpKeyid also cause timing issue
				if((IS_MCAST(prxattrib->ra)==_TRUE)  && (prxattrib->key_index != pmlmeinfo->key_index ))
					brpt_micerror = _FALSE;
				
				if((prxattrib->bdecrypted ==_TRUE)&& (brpt_micerror == _TRUE))
				{
					rtw_handle_tkip_mic_err(adapter, stainfo, (u8)IS_MCAST(prxattrib->ra));			
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" mic error :prxattrib->bdecrypted=%d ",prxattrib->bdecrypted));
					DBG_871X(" mic error :prxattrib->bdecrypted=%d\n",prxattrib->bdecrypted);
				}
				else
				{
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" mic error :prxattrib->bdecrypted=%d ",prxattrib->bdecrypted));
					DBG_871X(" mic error :prxattrib->bdecrypted=%d\n",prxattrib->bdecrypted);
				}

				res=_FAIL;

			}
			else{
				//mic checked ok
				if((psecuritypriv->bcheck_grpkey ==_FALSE)&&(IS_MCAST(prxattrib->ra)==_TRUE)){
					psecuritypriv->bcheck_grpkey =_TRUE;
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("psecuritypriv->bcheck_grpkey =_TRUE"));
				}
			}

		}
		else
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvframe_chkmic: rtw_get_stainfo==NULL!!!\n"));
		}

		recvframe_pull_tail(precvframe, 8);

	}

exit:

_func_exit_;

	return res;

}

//decrypt and set the ivlen,icvlen of the recv_frame
union recv_frame * decryptor(_adapter *padapter,union recv_frame *precv_frame);
union recv_frame * decryptor(_adapter *padapter,union recv_frame *precv_frame)
{

	struct rx_pkt_attrib *prxattrib = &precv_frame->u.hdr.attrib;
	struct security_priv *psecuritypriv=&padapter->securitypriv;
	union recv_frame *return_packet=precv_frame;
	u32	 res=_SUCCESS;

_func_enter_;

	DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt);

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("prxstat->decrypted=%x prxattrib->encrypt = 0x%03x\n",prxattrib->bdecrypted,prxattrib->encrypt));

	if(prxattrib->encrypt>0)
	{
		u8 *iv = precv_frame->u.hdr.rx_data+prxattrib->hdrlen;
		prxattrib->key_index = ( ((iv[3])>>6)&0x3) ;

		if(prxattrib->key_index > WEP_KEYS)
		{
			DBG_871X("prxattrib->key_index(%d) > WEP_KEYS \n", prxattrib->key_index);

			switch(prxattrib->encrypt){
				case _WEP40_:
				case _WEP104_:
					prxattrib->key_index = psecuritypriv->dot11PrivacyKeyIndex;
					break;
				case _TKIP_:			
				case _AES_:						
				default:
					prxattrib->key_index = psecuritypriv->dot118021XGrpKeyid;
					break;
			}	
		}			
	}

	if((prxattrib->encrypt>0) && ((prxattrib->bdecrypted==0) ||(psecuritypriv->sw_decrypt==_TRUE)))
	{

#ifdef CONFIG_CONCURRENT_MODE
		if(!IS_MCAST(prxattrib->ra))//bc/mc packets use sw decryption for concurrent mode
#endif			
			psecuritypriv->hw_decrypted=_FALSE;

		#ifdef DBG_RX_DECRYPTOR
		DBG_871X("[%s] %d:prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n",
			__FUNCTION__,
			__LINE__,
			prxattrib->bdecrypted,
			prxattrib->encrypt,
			psecuritypriv->hw_decrypted);
		#endif

		switch(prxattrib->encrypt){
		case _WEP40_:
		case _WEP104_:
			DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_wep);
			rtw_wep_decrypt(padapter, (u8 *)precv_frame);
			break;
		case _TKIP_:
			DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_tkip);
			res = rtw_tkip_decrypt(padapter, (u8 *)precv_frame);
			break;
		case _AES_:
			DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_aes);
			res = rtw_aes_decrypt(padapter, (u8 * )precv_frame);
			break;
#ifdef CONFIG_WAPI_SUPPORT
		case _SMS4_:
			DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_wapi);
			rtw_sms4_decrypt(padapter, (u8 * )precv_frame);
			break;
#endif
		default:
				break;
		}
	}
	else if(prxattrib->bdecrypted==1
		&& prxattrib->encrypt >0
		&& (psecuritypriv->busetkipkey==1 || prxattrib->encrypt !=_TKIP_ )
		)
	{
#if 0
		if((prxstat->icv==1)&&(prxattrib->encrypt!=_AES_))
		{
			psecuritypriv->hw_decrypted=_FALSE;

			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("psecuritypriv->hw_decrypted=_FALSE"));

			rtw_free_recvframe(precv_frame, &padapter->recvpriv.free_recv_queue);

			return_packet=NULL;

		}
		else
#endif
		{
			DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_hw);

			psecuritypriv->hw_decrypted=_TRUE;
			#ifdef DBG_RX_DECRYPTOR
			DBG_871X("[%s] %d:prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n",
				__FUNCTION__,
				__LINE__,
				prxattrib->bdecrypted,
				prxattrib->encrypt,
				psecuritypriv->hw_decrypted);

			#endif
		}
	}
	else {
		DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_unknown);
		#ifdef DBG_RX_DECRYPTOR
		DBG_871X("[%s] %d:prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n",
			__FUNCTION__,
			__LINE__,
			prxattrib->bdecrypted,
			prxattrib->encrypt,
			psecuritypriv->hw_decrypted);
		#endif
	}
	
	if(res == _FAIL)
	{
		rtw_free_recvframe(return_packet,&padapter->recvpriv.free_recv_queue);			
		return_packet = NULL;
	}
	else
	{
		prxattrib->bdecrypted = _TRUE;
	}
	//recvframe_chkmic(adapter, precv_frame);   //move to recvframme_defrag function

_func_exit_;

	return return_packet;

}
//###set the security information in the recv_frame
union recv_frame * portctrl(_adapter *adapter,union recv_frame * precv_frame);
union recv_frame * portctrl(_adapter *adapter,union recv_frame * precv_frame)
{
	u8 *psta_addr = NULL;
	u8 *ptr;
	uint  auth_alg;
	struct recv_frame_hdr *pfhdr;
	struct sta_info *psta;
	struct sta_priv *pstapriv ;
	union recv_frame *prtnframe;
	u16	ether_type=0;
	u16  eapol_type = 0x888e;//for Funia BD's WPA issue  
	struct rx_pkt_attrib *pattrib;

_func_enter_;

	pstapriv = &adapter->stapriv;

	auth_alg = adapter->securitypriv.dot11AuthAlgrthm;

	ptr = get_recvframe_data(precv_frame);
	pfhdr = &precv_frame->u.hdr;
	pattrib = &pfhdr->attrib;
	psta_addr = pattrib->ta;

	prtnframe = NULL;

	psta = rtw_get_stainfo(pstapriv, psta_addr);

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:adapter->securitypriv.dot11AuthAlgrthm=%d\n",adapter->securitypriv.dot11AuthAlgrthm));

	if(auth_alg==2)
	{
		if ((psta!=NULL) && (psta->ieee8021x_blocked))
		{
			//blocked
			//only accept EAPOL frame
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:psta->ieee8021x_blocked==1\n"));

			prtnframe=precv_frame;

			//get ether_type
			ptr=ptr+pfhdr->attrib.hdrlen+pfhdr->attrib.iv_len+LLC_HEADER_SIZE;
			_rtw_memcpy(&ether_type,ptr, 2);
			ether_type= ntohs((unsigned short )ether_type);

		        if (ether_type == eapol_type) {
				prtnframe=precv_frame;
			}
			else {
				//free this frame
				rtw_free_recvframe(precv_frame, &adapter->recvpriv.free_recv_queue);
				prtnframe=NULL;
			}
		}
		else
		{
			//allowed
			//check decryption status, and decrypt the frame if needed
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:psta->ieee8021x_blocked==0\n"));
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("portctrl:precv_frame->hdr.attrib.privacy=%x\n",precv_frame->u.hdr.attrib.privacy));

			if (pattrib->bdecrypted == 0)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("portctrl:prxstat->decrypted=%x\n", pattrib->bdecrypted));
			}

			prtnframe=precv_frame;
			//check is the EAPOL frame or not (Rekey)
			//if(ether_type == eapol_type){
			//	RT_TRACE(_module_rtl871x_recv_c_,_drv_notice_,("########portctrl:ether_type == 0x888e\n"));
				//check Rekey

			//	prtnframe=precv_frame;
			//}
			//else{
			//	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:ether_type=0x%04x\n", ether_type));
			//}
		}
	}
	else
	{
		prtnframe=precv_frame;
	}

_func_exit_;

		return prtnframe;

}

sint recv_decache(union recv_frame *precv_frame, u8 bretry, struct stainfo_rxcache *prxcache);
sint recv_decache(union recv_frame *precv_frame, u8 bretry, struct stainfo_rxcache *prxcache)
{
	sint tid = precv_frame->u.hdr.attrib.priority;

	u16 seq_ctrl = ( (precv_frame->u.hdr.attrib.seq_num&0xffff) << 4) |
		(precv_frame->u.hdr.attrib.frag_num & 0xf);

_func_enter_;

	if(tid>15)
	{
		RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("recv_decache, (tid>15)! seq_ctrl=0x%x, tid=0x%x\n", seq_ctrl, tid));

		return _FAIL;
	}

	if(1)//if(bretry)
	{
		if(seq_ctrl == prxcache->tid_rxseq[tid])
		{
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("recv_decache, seq_ctrl=0x%x, tid=0x%x, tid_rxseq=0x%x\n", seq_ctrl, tid, prxcache->tid_rxseq[tid]));

			return _FAIL;
		}
	}

	prxcache->tid_rxseq[tid] = seq_ctrl;

_func_exit_;

	return _SUCCESS;

}

void process_pwrbit_data(_adapter *padapter, union recv_frame *precv_frame);
void process_pwrbit_data(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	unsigned char pwrbit;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta=NULL;

	psta = rtw_get_stainfo(pstapriv, pattrib->src);

	pwrbit = GetPwrMgt(ptr);

	if(psta)
	{
		if(pwrbit)
		{
			if(!(psta->state & WIFI_SLEEP_STATE))
			{
				//psta->state |= WIFI_SLEEP_STATE;
				//pstapriv->sta_dz_bitmap |= BIT(psta->aid);

				stop_sta_xmit(padapter, psta);

				//DBG_871X("to sleep, sta_dz_bitmap=%x\n", pstapriv->sta_dz_bitmap);
			}
		}
		else
		{
			if(psta->state & WIFI_SLEEP_STATE)
			{
				//psta->state ^= WIFI_SLEEP_STATE;
				//pstapriv->sta_dz_bitmap &= ~BIT(psta->aid);

				wakeup_sta_to_xmit(padapter, psta);

				//DBG_871X("to wakeup, sta_dz_bitmap=%x\n", pstapriv->sta_dz_bitmap);
			}
		}

	}

#endif
}

void process_wmmps_data(_adapter *padapter, union recv_frame *precv_frame);
void process_wmmps_data(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE		
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta=NULL;

	psta = rtw_get_stainfo(pstapriv, pattrib->src);
	
	if(!psta) return;

#ifdef CONFIG_TDLS
	if( !(psta->tdls_sta_state & TDLS_LINKED_STATE ) )
	{
#endif //CONFIG_TDLS

	if(!psta->qos_option)
		return;

	if(!(psta->qos_info&0xf))
		return;
		
#ifdef CONFIG_TDLS
	}
#endif //CONFIG_TDLS		

	if(psta->state&WIFI_SLEEP_STATE)
	{
		u8 wmmps_ac=0;	
		
		switch(pattrib->priority)
		{
			case 1:
			case 2:
				wmmps_ac = psta->uapsd_bk&BIT(1);
				break;
			case 4:
			case 5:
				wmmps_ac = psta->uapsd_vi&BIT(1);
				break;
			case 6:
			case 7:
				wmmps_ac = psta->uapsd_vo&BIT(1);
				break;
			case 0:
			case 3:
			default:
				wmmps_ac = psta->uapsd_be&BIT(1);
				break;	
		}

		if(wmmps_ac)
		{
			if(psta->sleepq_ac_len>0)
			{
				//process received triggered frame
				xmit_delivery_enabled_frames(padapter, psta);
			}
			else
			{
				//issue one qos null frame with More data bit = 0 and the EOSP bit set (=1)
				issue_qos_nulldata(padapter, psta->hwaddr, (u16)pattrib->priority, 0, 0);
			}
		}
				
	}

	
#endif	

}

#ifdef CONFIG_TDLS
sint OnTDLS(_adapter *adapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib	*pattrib = & precv_frame->u.hdr.attrib;
	sint ret = _SUCCESS;
	u8 *paction = get_recvframe_data(precv_frame);
	u8 category_field = 1;
#ifdef CONFIG_WFD
	u8 WFA_OUI[3] = { 0x50, 0x6f, 0x9a };
#endif /* CONFIG_WFD */
	struct tdls_info *ptdlsinfo = &(adapter->tdlsinfo);

	/* point to action field */
	paction+=pattrib->hdrlen 
			+ pattrib->iv_len 
			+ SNAP_SIZE 
			+ ETH_TYPE_LEN 
			+ PAYLOAD_TYPE_LEN 
			+ category_field;

	DBG_871X("[TDLS] Recv %s from "MAC_FMT" with SeqNum = %d\n", rtw_tdls_action_txt(*paction), MAC_ARG(pattrib->src), GetSequence(get_recvframe_data(precv_frame)));

	if (hal_chk_wl_func(adapter, WL_FUNC_TDLS) == _FALSE) {
		DBG_871X("Ignore tdls frame since hal doesn't support tdls\n");
		ret = _FAIL;
		return ret;
	}

	if (ptdlsinfo->tdls_enable == _FALSE) {
		DBG_871X("recv tdls frame, "
				"but tdls haven't enabled\n");
		ret = _FAIL;
		return ret;
	}
	
	switch(*paction){
	case TDLS_SETUP_REQUEST:
		ret=On_TDLS_Setup_Req(adapter, precv_frame);
		break;
	case TDLS_SETUP_RESPONSE:
		ret=On_TDLS_Setup_Rsp(adapter, precv_frame);
		break;
	case TDLS_SETUP_CONFIRM:
		ret=On_TDLS_Setup_Cfm(adapter, precv_frame);
		break;
	case TDLS_TEARDOWN:
		ret=On_TDLS_Teardown(adapter, precv_frame);
		break;
	case TDLS_DISCOVERY_REQUEST:
		ret=On_TDLS_Dis_Req(adapter, precv_frame);
		break;
	case TDLS_PEER_TRAFFIC_INDICATION:
		ret=On_TDLS_Peer_Traffic_Indication(adapter, precv_frame);
		break;
	case TDLS_PEER_TRAFFIC_RESPONSE:
		ret=On_TDLS_Peer_Traffic_Rsp(adapter, precv_frame);
		break;
#ifdef CONFIG_TDLS_CH_SW		
	case TDLS_CHANNEL_SWITCH_REQUEST:
		ret=On_TDLS_Ch_Switch_Req(adapter, precv_frame);
		break;
	case TDLS_CHANNEL_SWITCH_RESPONSE:
		ret=On_TDLS_Ch_Switch_Rsp(adapter, precv_frame);
		break;
#endif		
#ifdef CONFIG_WFD			
	/* First byte of WFA OUI */
	case 0x50:
		if (_rtw_memcmp(WFA_OUI, paction, 3)) {
			/* Probe request frame */
			if (*(paction + 3) == 0x04) {
				/* WFDTDLS: for sigma test, do not setup direct link automatically */
				ptdlsinfo->dev_discovered = _TRUE;
				DBG_871X("recv tunneled probe request frame\n");
				issue_tunneled_probe_rsp(adapter, precv_frame);
			}
			/* Probe response frame */ 
			if (*(paction + 3) == 0x05) {
				/* WFDTDLS: for sigma test, do not setup direct link automatically */
				ptdlsinfo->dev_discovered = _TRUE;
				DBG_871X("recv tunneled probe response frame\n");
			}
		}
		break;
#endif /* CONFIG_WFD */
	default:
		DBG_871X("receive TDLS frame %d but not support\n", *paction);
		ret=_FAIL;
		break;
	}

exit:
	return ret;
	
}
#endif /* CONFIG_TDLS */

void count_rx_stats(_adapter *padapter, union recv_frame *prframe, struct sta_info*sta);
void count_rx_stats(_adapter *padapter, union recv_frame *prframe, struct sta_info*sta)
{
	int	sz;
	struct sta_info		*psta = NULL;
	struct stainfo_stats	*pstats = NULL;
	struct rx_pkt_attrib	*pattrib = & prframe->u.hdr.attrib;
	struct recv_priv		*precvpriv = &padapter->recvpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	sz = get_recvframe_len(prframe);
	precvpriv->rx_bytes += sz;

	padapter->mlmepriv.LinkDetectInfo.NumRxOkInPeriod++;

	if( (!MacAddr_isBcst(pattrib->dst)) && (!IS_MCAST(pattrib->dst))){
		padapter->mlmepriv.LinkDetectInfo.NumRxUnicastOkInPeriod++;
	}

	if(sta)
		psta = sta;
	else
		psta = prframe->u.hdr.psta;

	if(psta)
	{
		pstats = &psta->sta_stats;

		pstats->rx_data_pkts++;
		pstats->rx_bytes += sz;
		
		/*record rx packets for every tid*/	
		pstats->rx_data_qos_pkts[pattrib->priority]++;

#ifdef CONFIG_TDLS
		if(psta->tdls_sta_state & TDLS_LINKED_STATE)
		{
			struct sta_info *pap_sta = NULL;
			pap_sta = rtw_get_stainfo(&padapter->stapriv, get_bssid(&padapter->mlmepriv));
			if(pap_sta)
			{
				pstats = &pap_sta->sta_stats;
				pstats->rx_data_pkts++;
				pstats->rx_bytes += sz;
			}
		}
#endif //CONFIG_TDLS
	}

#ifdef CONFIG_CHECK_LEAVE_LPS
	traffic_check_for_leave_lps(padapter, _FALSE, 0);
#endif //CONFIG_LPS

}

sint sta2sta_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta
);
sint sta2sta_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta
)
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	sint ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	struct	sta_priv 		*pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = adapter_mac_addr(adapter);
	u8 * sta_addr = NULL;
	sint bmcast = IS_MCAST(pattrib->dst);

#ifdef CONFIG_TDLS	
	struct tdls_info *ptdlsinfo = &adapter->tdlsinfo;
#ifdef CONFIG_TDLS_CH_SW
	struct tdls_ch_switch *pchsw_info = &ptdlsinfo->chsw_info;
#endif
	struct sta_info *ptdls_sta=NULL;
	u8 *psnap_type=ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	//frame body located after [+2]: ether-type, [+1]: payload type
	u8 *pframe_body = psnap_type+2+1;
#endif

_func_enter_;

	//DBG_871X("[%s] %d, seqnum:%d\n", __FUNCTION__, __LINE__, pattrib->seq_num);

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE))
	{

		// filter packets that SA is myself or multicast or broadcast
		if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" SA==myself \n"));
			ret= _FAIL;
			goto exit;
		}

		if( (!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))	&& (!bmcast) ){
			ret= _FAIL;
			goto exit;
		}

		if( _rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		   _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		   (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) ) {
			ret= _FAIL;
			goto exit;
		}

		sta_addr = pattrib->src;

	}
	else if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
	{
#ifdef CONFIG_TDLS

		/* direct link data transfer */
		if (ptdlsinfo->link_established == _TRUE) {
			ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->src);
			if (ptdls_sta == NULL) {
				ret=_FAIL;
				goto exit;
			} else if (ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE) {
				/* filter packets that SA is myself or multicast or broadcast */
				if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)) {
					ret= _FAIL;
					goto exit;
				}
				/* da should be for me */
				if ((!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN)) && (!bmcast)) {
					ret= _FAIL;
					goto exit;
				}
				/* check BSSID */
				if (_rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
				     _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
				     (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN))) {
					ret= _FAIL;
					goto exit;
				}

#ifdef CONFIG_TDLS_CH_SW
				if(ATOMIC_READ(&pchsw_info->chsw_on) == _TRUE) {
					if (adapter->mlmeextpriv.cur_channel != rtw_get_oper_ch(adapter)) {
						pchsw_info->ch_sw_state |= TDLS_PEER_AT_OFF_STATE;
						if (!(pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE))
							_cancel_timer_ex(&ptdls_sta->ch_sw_timer);
						/* On_TDLS_Peer_Traffic_Rsp(adapter, precv_frame); */
					}
				}
#endif

				/* process UAPSD tdls sta */
				process_pwrbit_data(adapter, precv_frame);

				/* if NULL-frame, check pwrbit */
				if ((GetFrameSubType(ptr) & WIFI_DATA_NULL) == WIFI_DATA_NULL) {
					/* NULL-frame with pwrbit=1, buffer_STA should buffer frames for sleep_STA */
					if (GetPwrMgt(ptr)) {
						/* it would be triggered when we are off channel and receiving NULL DATA */
						/* we can confirm that peer STA is at off channel */
						DBG_871X("TDLS: recv peer null frame with pwr bit 1\n");
						//ptdls_sta->tdls_sta_state|=TDLS_PEER_SLEEP_STATE;
					}

					/* TODO: Updated BSSID's seq. */
					//DBG_871X("drop Null Data\n");
					ptdls_sta->tdls_sta_state &= ~(TDLS_WAIT_PTR_STATE);
					ret= _FAIL;
					goto exit;
				}

				/* receive some of all TDLS management frames, process it at ON_TDLS */
				if (_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_TDLS, 2)) {
					ret= OnTDLS(adapter, precv_frame);
					goto exit;
				}

				if ((GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE) {
					process_wmmps_data(adapter, precv_frame);
				}

				ptdls_sta->tdls_sta_state &= ~(TDLS_WAIT_PTR_STATE);

			}

			sta_addr = pattrib->src;
			
		}		
		else
#endif /* CONFIG_TDLS */
		{
			// For Station mode, sa and bssid should always be BSSID, and DA is my mac-address
			if(!_rtw_memcmp(pattrib->bssid, pattrib->src, ETH_ALEN) )
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("bssid != TA under STATION_MODE; drop pkt\n"));
				ret= _FAIL;
				goto exit;
		}

		sta_addr = pattrib->bssid;
		}

	}
	else if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
		if (bmcast)
		{
			// For AP mode, if DA == MCAST, then BSSID should be also MCAST
			if (!IS_MCAST(pattrib->bssid)){
					ret= _FAIL;
					goto exit;
			}
		}
		else // not mc-frame
		{
			// For AP mode, if DA is non-MCAST, then it must be BSSID, and bssid == BSSID
			if(!_rtw_memcmp(pattrib->bssid, pattrib->dst, ETH_ALEN)) {
				ret= _FAIL;
				goto exit;
			}

			sta_addr = pattrib->src;
		}

	}
	else if(check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
	{
		_rtw_memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

		sta_addr = mybssid;
	}
	else
	{
		ret  = _FAIL;
	}



	if(bmcast)
		*psta = rtw_get_bcmc_stainfo(adapter);
	else
		*psta = rtw_get_stainfo(pstapriv, sta_addr); // get ap_info

#ifdef CONFIG_TDLS
	if(ptdls_sta != NULL)
	{
		*psta = ptdls_sta;
	}
#endif //CONFIG_TDLS

	if (*psta == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("can't get psta under sta2sta_data_frame ; drop pkt\n"));
#ifdef CONFIG_MP_INCLUDED
		if (adapter->registrypriv.mp_mode == 1)
		{
			if(check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
				adapter->mppriv.rx_pktloss++;
		}
#endif
		ret= _FAIL;
		goto exit;
	}

exit:
_func_exit_;
	return ret;

}

sint ap2sta_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta );
sint ap2sta_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta )
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	sint ret = _SUCCESS;
	struct	sta_priv 		*pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = adapter_mac_addr(adapter);
	sint bmcast = IS_MCAST(pattrib->dst);

_func_enter_;

	if ((check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
		&& (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE 
			|| check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE	)
		)
	{

		// filter packets that SA is myself or multicast or broadcast
		if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" SA==myself \n"));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s SA="MAC_FMT", myhwaddr="MAC_FMT"\n",
				__FUNCTION__, MAC_ARG(pattrib->src), MAC_ARG(myhwaddr));
			#endif			
			ret= _FAIL;
			goto exit;
		}

		// da should be for me
		if((!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))&& (!bmcast))
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,
				(" ap2sta_data_frame:  compare DA fail; DA="MAC_FMT"\n", MAC_ARG(pattrib->dst)));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s DA="MAC_FMT"\n", __func__, MAC_ARG(pattrib->dst));
			#endif
			ret= _FAIL;
			goto exit;
		}


		// check BSSID
		if( _rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		     _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		     (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) )
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,
				(" ap2sta_data_frame:  compare BSSID fail ; BSSID="MAC_FMT"\n", MAC_ARG(pattrib->bssid)));
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("mybssid="MAC_FMT"\n", MAC_ARG(mybssid)));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s BSSID="MAC_FMT", mybssid="MAC_FMT"\n",
				__FUNCTION__, MAC_ARG(pattrib->bssid), MAC_ARG(mybssid));
			DBG_871X( "this adapter = %d, buddy adapter = %d\n", adapter->adapter_type, adapter->pbuddy_adapter->adapter_type );
			#endif

			if(!bmcast)
			{
				DBG_871X("issue_deauth to the nonassociated ap=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->bssid));
				issue_deauth(adapter, pattrib->bssid, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
			}

			ret= _FAIL;
			goto exit;
		}

		if(bmcast)
			*psta = rtw_get_bcmc_stainfo(adapter);
		else
			*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); // get ap_info

		if (*psta == NULL) {
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("ap2sta: can't get psta under STATION_MODE ; drop pkt\n"));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s can't get psta under STATION_MODE ; drop pkt\n", __FUNCTION__);
			#endif
			ret= _FAIL;
			goto exit;
		}

		if ((GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE) {
		}

		if (GetFrameSubType(ptr) & BIT(6)) {
			/* No data, will not indicate to upper layer, temporily count it here */
			count_rx_stats(adapter, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}

	}
	else if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) &&
		     (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) )
	{
		_rtw_memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

		//
		if(adapter->mppriv.bRTWSmbCfg==_FALSE)
			_rtw_memcpy(pattrib->bssid,  mybssid, ETH_ALEN);


		*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); // get sta_info
		if (*psta == NULL) {
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("can't get psta under MP_MODE ; drop pkt\n"));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s can't get psta under WIFI_MP_STATE ; drop pkt\n", __FUNCTION__);
			#endif
			ret= _FAIL;
			goto exit;
		}


	}
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
		/* Special case */
		ret = RTW_RX_HANDLED;
		goto exit;
	}
	else
	{
		if(_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN)&& (!bmcast))
		{
			*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); // get sta_info
			if (*psta == NULL)
			{
	
				//for AP multicast issue , modify by yiwei 
				static u32 send_issue_deauth_time=0;
			
				//DBG_871X("After send deauth , %u ms has elapsed.\n", rtw_get_passing_time_ms(send_issue_deauth_time));
				
				if(rtw_get_passing_time_ms(send_issue_deauth_time) > 10000 || send_issue_deauth_time == 0 )
				{
					send_issue_deauth_time = rtw_get_current_time(); 
					
					DBG_871X("issue_deauth to the ap=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->bssid));
					
					issue_deauth(adapter, pattrib->bssid, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
				}
			}
		}	
	
		ret = _FAIL;
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s fw_state:0x%x\n", __FUNCTION__, get_fwstate(pmlmepriv));
		#endif
	}

exit:

_func_exit_;

	return ret;

}

sint sta2ap_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta );
sint sta2ap_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta )
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	struct	sta_priv 		*pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	unsigned char *mybssid  = get_bssid(pmlmepriv);	
	sint ret=_SUCCESS;

_func_enter_;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
		//For AP mode, RA=BSSID, TX=STA(SRC_ADDR), A3=DST_ADDR
		if(!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN))
		{
			ret= _FAIL;
			goto exit;
		}

		*psta = rtw_get_stainfo(pstapriv, pattrib->src);
		if (*psta == NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("can't get psta under AP_MODE; drop pkt\n"));
			DBG_871X("issue_deauth to sta=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->src));

			issue_deauth(adapter, pattrib->src, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);

			ret = RTW_RX_HANDLED;
			goto exit;
		}

		process_pwrbit_data(adapter, precv_frame);

		if ((GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE) {
			process_wmmps_data(adapter, precv_frame);
		}

		if (GetFrameSubType(ptr) & BIT(6)) {
			/* No data, will not indicate to upper layer, temporily count it here */
			count_rx_stats(adapter, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}
	}
	else if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) &&
		     (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) )
	{
		//DBG_871X("%s ,in WIFI_MP_STATE \n",__func__);
		_rtw_memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
		//
		if(adapter->mppriv.bRTWSmbCfg == _FALSE)
			_rtw_memcpy(pattrib->bssid,  mybssid, ETH_ALEN);

		*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); // get sta_info
		if (*psta == NULL) {
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("can't get psta under MP_MODE ; drop pkt\n"));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s can't get psta under WIFI_MP_STATE ; drop pkt\n", __FUNCTION__);
			#endif
			ret= _FAIL;
			goto exit;
		}

	}
	else {
		u8 *myhwaddr = adapter_mac_addr(adapter);
		if (!_rtw_memcmp(pattrib->ra, myhwaddr, ETH_ALEN)) {
			ret = RTW_RX_HANDLED;
			goto exit;
		}
		DBG_871X("issue_deauth to sta=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->src));
		issue_deauth(adapter, pattrib->src, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
		ret = RTW_RX_HANDLED;
		goto exit;
	}

exit:

_func_exit_;

	return ret;

}

sint validate_recv_ctrl_frame(_adapter *padapter, union recv_frame *precv_frame);
sint validate_recv_ctrl_frame(_adapter *padapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_info *psta=NULL;
	//uint len = precv_frame->u.hdr.len;
		
	//DBG_871X("+validate_recv_ctrl_frame\n");

	if (GetFrameType(pframe) != WIFI_CTRL_TYPE)
	{		
		return _FAIL;
	}

	//receive the frames that ra(a1) is my address
	if (!_rtw_memcmp(GetAddr1Ptr(pframe), adapter_mac_addr(padapter), ETH_ALEN))
		return _FAIL;

	psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
	if (psta==NULL)
	{
		return _FAIL;
	}

	//for rx pkt statistics
	psta->sta_stats.rx_ctrl_pkts++;

	//only handle ps-poll
	if(GetFrameSubType(pframe) == WIFI_PSPOLL)
	{
#ifdef CONFIG_AP_MODE
		u16 aid;
		u8 wmmps_ac=0;	
	
		aid = GetAid(pframe);
		if(psta->aid!=aid)
		{
			return _FAIL;
		}

		switch(pattrib->priority)
		{
			case 1:
			case 2:
				wmmps_ac = psta->uapsd_bk&BIT(0);
				break;
			case 4:
			case 5:
				wmmps_ac = psta->uapsd_vi&BIT(0);
				break;
			case 6:
			case 7:
				wmmps_ac = psta->uapsd_vo&BIT(0);
				break;
			case 0:
			case 3:
			default:
				wmmps_ac = psta->uapsd_be&BIT(0);
				break;	
		}

		if(wmmps_ac)
			return _FAIL;

		if(psta->state & WIFI_STA_ALIVE_CHK_STATE)
		{					
			DBG_871X("%s alive check-rx ps-poll\n", __func__);
			psta->expire_to = pstapriv->expire_to;
			psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
		}	

		if((psta->state&WIFI_SLEEP_STATE) && (pstapriv->sta_dz_bitmap&BIT(psta->aid)))
		{
			_irqL irqL;	 
			_list	*xmitframe_plist, *xmitframe_phead;
			struct xmit_frame *pxmitframe=NULL;
			struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
		
			//_enter_critical_bh(&psta->sleep_q.lock, &irqL);
			_enter_critical_bh(&pxmitpriv->lock, &irqL);

			xmitframe_phead = get_list_head(&psta->sleep_q);
			xmitframe_plist = get_next(xmitframe_phead);

			if ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE)
			{			
				pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

				xmitframe_plist = get_next(xmitframe_plist);

				rtw_list_delete(&pxmitframe->list);

				psta->sleepq_len--;

				if(psta->sleepq_len>0)
					pxmitframe->attrib.mdata = 1;
				else
					pxmitframe->attrib.mdata = 0;

				pxmitframe->attrib.triggered = 1;

				//DBG_871X("handling ps-poll, q_len=%d, tim=%x\n", psta->sleepq_len, pstapriv->tim_bitmap);

#if 0
				_exit_critical_bh(&psta->sleep_q.lock, &irqL);
				if(rtw_hal_xmit(padapter, pxmitframe) == _TRUE)
				{
					rtw_os_xmit_complete(padapter, pxmitframe);
				}
				_enter_critical_bh(&psta->sleep_q.lock, &irqL);	
#endif
				rtw_hal_xmitframe_enqueue(padapter, pxmitframe);

				if(psta->sleepq_len==0)
				{
					pstapriv->tim_bitmap &= ~BIT(psta->aid);

					//DBG_871X("after handling ps-poll, tim=%x\n", pstapriv->tim_bitmap);

					//upate BCN for TIM IE
					//update_BCNTIM(padapter);		
					update_beacon(padapter, _TIM_IE_, NULL, _TRUE);
				}
				
				//_exit_critical_bh(&psta->sleep_q.lock, &irqL);
				_exit_critical_bh(&pxmitpriv->lock, &irqL);
				
			}
			else
			{
				//_exit_critical_bh(&psta->sleep_q.lock, &irqL);
				_exit_critical_bh(&pxmitpriv->lock, &irqL);
			
				//DBG_871X("no buffered packets to xmit\n");
				if(pstapriv->tim_bitmap&BIT(psta->aid))
				{
					if(psta->sleepq_len==0)
					{
						DBG_871X("no buffered packets to xmit\n");

						//issue nulldata with More data bit = 0 to indicate we have no buffered packets
						issue_nulldata_in_interrupt(padapter, psta->hwaddr, 0);
					}
					else
					{
						DBG_871X("error!psta->sleepq_len=%d\n", psta->sleepq_len);
						psta->sleepq_len=0;						
					}
				
					pstapriv->tim_bitmap &= ~BIT(psta->aid);					

					//upate BCN for TIM IE
					//update_BCNTIM(padapter);
					update_beacon(padapter, _TIM_IE_, NULL, _TRUE);
				}
			}				
		}
#endif //CONFIG_AP_MODE
	}
	else if(GetFrameSubType(pframe) == WIFI_NDPA) {
#ifdef CONFIG_BEAMFORMING
		beamforming_get_ndpa_frame(padapter, precv_frame);
#endif/*CONFIG_BEAMFORMING*/
	}

	return _FAIL;

}

union recv_frame* recvframe_chk_defrag(PADAPTER padapter, union recv_frame *precv_frame);
sint validate_recv_mgnt_frame(PADAPTER padapter, union recv_frame *precv_frame);
sint validate_recv_mgnt_frame(PADAPTER padapter, union recv_frame *precv_frame)
{
	//struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	RT_TRACE(_module_rtl871x_recv_c_, _drv_info_, ("+validate_recv_mgnt_frame\n"));

#if 0
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
#ifdef CONFIG_NATIVEAP_MLME
		mgt_dispatcher(padapter, precv_frame);
#else
		rtw_hostapd_mlme_rx(padapter, precv_frame);
#endif
	}
	else
	{
		mgt_dispatcher(padapter, precv_frame);
	}
#endif

	precv_frame = recvframe_chk_defrag(padapter, precv_frame);
	if (precv_frame == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,("%s: fragment packet\n",__FUNCTION__));
		return _SUCCESS;
	}

	{
		//for rx pkt statistics
		struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, GetAddr2Ptr(precv_frame->u.hdr.rx_data));
		if (psta) {
			psta->sta_stats.rx_mgnt_pkts++;
			if (GetFrameSubType(precv_frame->u.hdr.rx_data) == WIFI_BEACON)
				psta->sta_stats.rx_beacon_pkts++;
			else if (GetFrameSubType(precv_frame->u.hdr.rx_data) == WIFI_PROBEREQ)
				psta->sta_stats.rx_probereq_pkts++;
			else if (GetFrameSubType(precv_frame->u.hdr.rx_data) == WIFI_PROBERSP) {
				if (_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(precv_frame->u.hdr.rx_data), ETH_ALEN) == _TRUE)
					psta->sta_stats.rx_probersp_pkts++;
				else if (is_broadcast_mac_addr(GetAddr1Ptr(precv_frame->u.hdr.rx_data))
					|| is_multicast_mac_addr(GetAddr1Ptr(precv_frame->u.hdr.rx_data)))
					psta->sta_stats.rx_probersp_bm_pkts++;
				else 
					psta->sta_stats.rx_probersp_uo_pkts++;
			}
		}
	}

#ifdef CONFIG_INTEL_PROXIM
	if(padapter->proximity.proxim_on==_TRUE)
	{
		struct rx_pkt_attrib * pattrib=&precv_frame->u.hdr.attrib;
		 struct recv_stat* prxstat=( struct recv_stat * )  precv_frame->u.hdr.rx_head ;
		 u8 * pda,*psa,*pbssid,*ptr;
		 ptr=precv_frame->u.hdr.rx_data; 
		pda = get_da(ptr);
		psa = get_sa(ptr);
		pbssid = get_hdr_bssid(ptr);


		_rtw_memcpy(pattrib->dst, pda, ETH_ALEN);
		_rtw_memcpy(pattrib->src, psa, ETH_ALEN);

		_rtw_memcpy(pattrib->bssid, pbssid, ETH_ALEN);

	switch(pattrib->to_fr_ds)
	{
		case 0:
			_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
			break;

		case 1:
			_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, pbssid, ETH_ALEN);
			break;

		case 2:
			_rtw_memcpy(pattrib->ra, pbssid, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
			break;

		case 3:
			_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
			_rtw_memcpy(pattrib->ta, GetAddr2Ptr(ptr), ETH_ALEN);
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" case 3\n"));
			break;

		default:
			break;

		}	
			pattrib->priority=0;
			pattrib->hdrlen = pattrib->to_fr_ds==3 ? 30 : 24;

		 padapter->proximity.proxim_rx(padapter,precv_frame);
	}
#endif
	mgt_dispatcher(padapter, precv_frame);

	return _SUCCESS;

}

sint validate_recv_data_frame(_adapter *adapter, union recv_frame *precv_frame);
sint validate_recv_data_frame(_adapter *adapter, union recv_frame *precv_frame)
{
	u8 bretry;
	u8 *psa, *pda, *pbssid;
	struct sta_info *psta = NULL;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*pattrib = & precv_frame->u.hdr.attrib;
	struct sta_priv 	*pstapriv = &adapter->stapriv;
	struct security_priv	*psecuritypriv = &adapter->securitypriv;	
	sint ret = _SUCCESS;

_func_enter_;

	bretry = GetRetry(ptr);
	pda = get_da(ptr);
	psa = get_sa(ptr);
	pbssid = get_hdr_bssid(ptr);

	if(pbssid == NULL){
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s pbssid == NULL\n", __func__);
		#endif
		ret= _FAIL;
		goto exit;
	}

	_rtw_memcpy(pattrib->dst, pda, ETH_ALEN);
	_rtw_memcpy(pattrib->src, psa, ETH_ALEN);

	_rtw_memcpy(pattrib->bssid, pbssid, ETH_ALEN);

	switch(pattrib->to_fr_ds)
	{
		case 0:
			_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
			ret = sta2sta_data_frame(adapter, precv_frame, &psta);
			break;

		case 1:
			_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, pbssid, ETH_ALEN);
			ret = ap2sta_data_frame(adapter, precv_frame, &psta);
			break;

		case 2:
			_rtw_memcpy(pattrib->ra, pbssid, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
			ret = sta2ap_data_frame(adapter, precv_frame, &psta);
			break;

		case 3:
			_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
			_rtw_memcpy(pattrib->ta, GetAddr2Ptr(ptr), ETH_ALEN);
			ret =_FAIL;
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" case 3\n"));
			break;

		default:
			ret =_FAIL;
			break;

	}

	if(ret ==_FAIL){
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s case:%d, res:%d\n", __FUNCTION__, pattrib->to_fr_ds, ret);
		#endif
		goto exit;
	} else if (ret == RTW_RX_HANDLED) {
		goto exit;
	}


	if(psta==NULL){
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" after to_fr_ds_chk; psta==NULL \n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s psta == NULL\n", __func__);
		#endif
		ret= _FAIL;
		goto exit;
	}
	
	//psta->rssi = prxcmd->rssi;
	//psta->signal_quality= prxcmd->sq;
	precv_frame->u.hdr.psta = psta;
		

	pattrib->amsdu=0;
	pattrib->ack_policy = 0;
	//parsing QC field
	if(pattrib->qos == 1)
	{
		pattrib->priority = GetPriority((ptr + 24));
		pattrib->ack_policy = GetAckpolicy((ptr + 24));
		pattrib->amsdu = GetAMsdu((ptr + 24));
		pattrib->hdrlen = pattrib->to_fr_ds==3 ? 32 : 26;

		if(pattrib->priority!=0 && pattrib->priority!=3)
			adapter->recvpriv.bIsAnyNonBEPkts = _TRUE;
		else
			adapter->recvpriv.bIsAnyNonBEPkts = _FALSE;
	}
	else
	{
		pattrib->priority=0;
		pattrib->hdrlen = pattrib->to_fr_ds==3 ? 30 : 24;
	}


	if(pattrib->order)//HT-CTRL 11n
	{
		pattrib->hdrlen += 4;
	}

	precv_frame->u.hdr.preorder_ctrl = &psta->recvreorder_ctrl[pattrib->priority];

	// decache, drop duplicate recv packets
	if(recv_decache(precv_frame, bretry, &psta->sta_recvpriv.rxcache) == _FAIL)
	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("decache : drop pkt\n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s recv_decache return _FAIL\n", __func__);
		#endif
		ret= _FAIL;
		goto exit;
	}

	if(pattrib->privacy){

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("validate_recv_data_frame:pattrib->privacy=%x\n", pattrib->privacy));
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n ^^^^^^^^^^^IS_MCAST(pattrib->ra(0x%02x))=%d^^^^^^^^^^^^^^^6\n", pattrib->ra[0],IS_MCAST(pattrib->ra)));

#ifdef CONFIG_TDLS
		if((psta->tdls_sta_state & TDLS_LINKED_STATE) && (psta->dot118021XPrivacy==_AES_))
		{
			pattrib->encrypt=psta->dot118021XPrivacy;
		}
		else
#endif //CONFIG_TDLS
		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, IS_MCAST(pattrib->ra));

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n pattrib->encrypt=%d\n",pattrib->encrypt));

		SET_ICE_IV_LEN(pattrib->iv_len, pattrib->icv_len, pattrib->encrypt);
	}
	else
	{
		pattrib->encrypt = 0;
		pattrib->iv_len = pattrib->icv_len = 0;
	}

exit:

_func_exit_;

	return ret;
}

#ifdef CONFIG_IEEE80211W
static sint validate_80211w_mgmt(_adapter *adapter, union recv_frame *precv_frame)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct sta_info	*psta;
	struct sta_priv		*pstapriv = &adapter->stapriv;
	u8 type;
	u8 subtype;
			
	type =  GetFrameType(ptr);
	subtype = GetFrameSubType(ptr); //bit(7)~bit(2)
			
	if (adapter->securitypriv.binstallBIPkey == _TRUE)
	{
		//unicast management frame decrypt
		if(pattrib->privacy && !(IS_MCAST(GetAddr1Ptr(ptr))) && 
			(subtype == WIFI_DEAUTH || subtype == WIFI_DISASSOC || subtype == WIFI_ACTION))
		{
			u8 *ppp, *mgmt_DATA;
			u32 data_len=0;
			ppp = GetAddr2Ptr(ptr);
			
			pattrib->bdecrypted = 0;
			pattrib->encrypt = _AES_;
			pattrib->hdrlen = sizeof(struct rtw_ieee80211_hdr_3addr);
			//set iv and icv length
			SET_ICE_IV_LEN(pattrib->iv_len, pattrib->icv_len, pattrib->encrypt);
			_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
			_rtw_memcpy(pattrib->ta, GetAddr2Ptr(ptr), ETH_ALEN);
			//actual management data frame body
			data_len = pattrib->pkt_len - pattrib->hdrlen - pattrib->iv_len - pattrib->icv_len;
			mgmt_DATA = rtw_zmalloc(data_len);
			if(mgmt_DATA == NULL)
			{
				DBG_871X("%s mgmt allocate fail  !!!!!!!!!\n", __FUNCTION__);
				goto validate_80211w_fail;
			}
			/* //dump the packet content before decrypt
			{
				int pp;
				printk("pattrib->pktlen = %d =>", pattrib->pkt_len);
				for(pp=0;pp< pattrib->pkt_len; pp++)
					printk(" %02x ", ptr[pp]);
				printk("\n");
			}*/
			
			precv_frame = decryptor(adapter, precv_frame);
			//save actual management data frame body
			_rtw_memcpy(mgmt_DATA, ptr+pattrib->hdrlen+pattrib->iv_len, data_len);
			//overwrite the iv field
			_rtw_memcpy(ptr+pattrib->hdrlen, mgmt_DATA, data_len);
			//remove the iv and icv length
			pattrib->pkt_len = pattrib->pkt_len - pattrib->iv_len - pattrib->icv_len;
			rtw_mfree(mgmt_DATA, data_len);
			/* //print packet content after decryption
			{
				int pp;
				printk("after decryption pattrib->pktlen = %d @@=>", pattrib->pkt_len);
				for(pp=0;pp< pattrib->pkt_len; pp++)
					printk(" %02x ", ptr[pp]);
				printk("\n");
			}*/
			if(!precv_frame)
			{
				DBG_871X("%s mgmt descrypt fail  !!!!!!!!!\n", __FUNCTION__);
				goto validate_80211w_fail;
			}
		}
		else if(IS_MCAST(GetAddr1Ptr(ptr)) &&
			(subtype == WIFI_DEAUTH || subtype == WIFI_DISASSOC))
		{
			sint BIP_ret = _SUCCESS;
			//verify BIP MME IE of broadcast/multicast de-auth/disassoc packet
			BIP_ret = rtw_BIP_verify(adapter, (u8 * )precv_frame);
			if(BIP_ret == _FAIL)
			{
				//DBG_871X("802.11w BIP verify fail\n");
				goto validate_80211w_fail;
			}
			else if(BIP_ret == RTW_RX_HANDLED)
			{
				DBG_871X("802.11w recv none protected packet\n");
				//drop pkt, don't issue sa query request
				/* issue_action_SA_Query(adapter, NULL, 0, 0, 0); */
				goto validate_80211w_fail;
			}
		}//802.11w protect
		else
		{
			psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(ptr));
			
			if (subtype == WIFI_ACTION && psta && psta->bpairwise_key_installed == _TRUE) {
				//according 802.11-2012 standard, these five types are not robust types
				if( ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_PUBLIC          &&
					ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_HT              &&
					ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_UNPROTECTED_WNM &&
					ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_SELF_PROTECTED  &&
					ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_P2P)
				{
					DBG_871X("action frame category=%d should robust\n", ptr[WLAN_HDR_A3_LEN]);
					goto validate_80211w_fail;
				}
			}
			else if(subtype == WIFI_DEAUTH || subtype == WIFI_DISASSOC)
			{
				unsigned short	reason;
				reason = le16_to_cpu(*(unsigned short *)(ptr + WLAN_HDR_A3_LEN));
				DBG_871X("802.11w recv none protected packet, reason=%d\n", reason);
				if(reason == 6 || reason == 7)
				{
					//issue sa query request
					issue_action_SA_Query(adapter, NULL, 0, 0, IEEE80211W_RIGHT_KEY);
				}
				goto validate_80211w_fail;
			}
		}
	}
	return _SUCCESS;
			
validate_80211w_fail:
	return _FAIL;
	
}
#endif //CONFIG_IEEE80211W

static inline void dump_rx_packet(u8 *ptr)
{
	int i;

	DBG_871X("############################# \n");
	for(i=0; i<64;i=i+8)
		DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(ptr+i),
		*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));
	DBG_871X("############################# \n");
}

sint validate_recv_frame(_adapter *adapter, union recv_frame *precv_frame);
sint validate_recv_frame(_adapter *adapter, union recv_frame *precv_frame)
{
	//shall check frame subtype, to / from ds, da, bssid

	//then call check if rx seq/frag. duplicated.

	u8 type;
	u8 subtype;
	sint retval = _SUCCESS;

	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;

	u8 *ptr = precv_frame->u.hdr.rx_data;
	u8  ver =(unsigned char) (*ptr)&0x3 ;
#ifdef CONFIG_FIND_BEST_CHANNEL
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
#endif

#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &adapter->tdlsinfo;
#endif //CONFIG_TDLS
#ifdef CONFIG_WAPI_SUPPORT
	PRT_WAPI_T	pWapiInfo = &adapter->wapiInfo;
	struct recv_frame_hdr *phdr = &precv_frame->u.hdr;
	u8 wai_pkt = 0;
	u16 sc;
	u8	external_len = 0;
#endif

_func_enter_;

#ifdef CONFIG_FIND_BEST_CHANNEL
	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		int ch_set_idx = rtw_ch_set_search_ch(pmlmeext->channel_set, rtw_get_oper_ch(adapter));
		if (ch_set_idx >= 0)
			pmlmeext->channel_set[ch_set_idx].rx_count++;
	}
#endif

#ifdef CONFIG_TDLS
	if(ptdlsinfo->ch_sensing==1 && ptdlsinfo->cur_channel !=0){
		ptdlsinfo->collect_pkt_num[ptdlsinfo->cur_channel-1]++;
	}
#endif //CONFIG_TDLS

#ifdef RTK_DMP_PLATFORM
	if ( 0 )
	{
		DBG_871X("++\n");
		{
			int i;
			for(i=0; i<64;i=i+8)
				DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:", *(ptr+i),
				*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));

		}
		DBG_871X("--\n");
	}
#endif //RTK_DMP_PLATFORM

	//add version chk
	if(ver!=0){
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_data_frame fail! (ver!=0)\n"));
		retval= _FAIL;
		DBG_COUNTER(adapter->rx_logs.core_rx_pre_ver_err);
		goto exit;
	}

	type =  GetFrameType(ptr);
	subtype = GetFrameSubType(ptr); //bit(7)~bit(2)

	pattrib->to_fr_ds = get_tofr_ds(ptr);

	pattrib->frag_num = GetFragNum(ptr);
	pattrib->seq_num = GetSequence(ptr);

	pattrib->pw_save = GetPwrMgt(ptr);
	pattrib->mfrag = GetMFrag(ptr);
	pattrib->mdata = GetMData(ptr);
	pattrib->privacy = GetPrivacy(ptr);
	pattrib->order = GetOrder(ptr);
#ifdef CONFIG_WAPI_SUPPORT
	sc = (pattrib->seq_num<<4) | pattrib->frag_num;
#endif

#if 1 //Dump rx packets
{
	u8 bDumpRxPkt = 0;

	rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_RXPKT, &(bDumpRxPkt));
	if (bDumpRxPkt == 1) //dump all rx packets
		dump_rx_packet(ptr);
	else if ((bDumpRxPkt == 2) && (type == WIFI_MGT_TYPE))
		dump_rx_packet(ptr);
	else if ((bDumpRxPkt == 3) && (type == WIFI_DATA_TYPE))
		dump_rx_packet(ptr);
}
#endif
	switch (type)
	{
		case WIFI_MGT_TYPE: //mgnt
			DBG_COUNTER(adapter->rx_logs.core_rx_pre_mgmt);
#ifdef CONFIG_IEEE80211W
			if(validate_80211w_mgmt(adapter, precv_frame) == _FAIL)
			{
				retval = _FAIL;
				DBG_COUNTER(adapter->rx_logs.core_rx_pre_mgmt_err_80211w);
				break;
			}
#endif //CONFIG_IEEE80211W
			
			retval = validate_recv_mgnt_frame(adapter, precv_frame);
			if (retval == _FAIL)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_mgnt_frame fail\n"));
				DBG_COUNTER(adapter->rx_logs.core_rx_pre_mgmt_err);
			}
			retval = _FAIL; // only data frame return _SUCCESS
			break;
		case WIFI_CTRL_TYPE: //ctrl
			DBG_COUNTER(adapter->rx_logs.core_rx_pre_ctrl);
			retval = validate_recv_ctrl_frame(adapter, precv_frame);
			if (retval == _FAIL)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_ctrl_frame fail\n"));
				DBG_COUNTER(adapter->rx_logs.core_rx_pre_ctrl_err);
			}
			retval = _FAIL; // only data frame return _SUCCESS
			break;
		case WIFI_DATA_TYPE: //data
			DBG_COUNTER(adapter->rx_logs.core_rx_pre_data);
#ifdef CONFIG_WAPI_SUPPORT
			if(pattrib->qos)
				external_len = 2;
			else
				external_len= 0;
			
			wai_pkt = rtw_wapi_is_wai_packet(adapter,ptr);

			phdr->bIsWaiPacket = wai_pkt;

			if(wai_pkt !=0){
				if(sc != adapter->wapiInfo.wapiSeqnumAndFragNum)
				{
					adapter->wapiInfo.wapiSeqnumAndFragNum = sc;
				}
				else
				{
					retval = _FAIL;
					DBG_COUNTER(adapter->rx_logs.core_rx_pre_data_wapi_seq_err);
					break;
				}
			}
			else{

					if(rtw_wapi_drop_for_key_absent(adapter,GetAddr2Ptr(ptr))){
						retval=_FAIL;
						WAPI_TRACE(WAPI_RX,"drop for key absent for rx \n");
						DBG_COUNTER(adapter->rx_logs.core_rx_pre_data_wapi_key_err);
						break;
					}
			}

#endif

			pattrib->qos = (subtype & BIT(7))? 1:0;
			retval = validate_recv_data_frame(adapter, precv_frame);
			if (retval == _FAIL)
			{
				struct recv_priv *precvpriv = &adapter->recvpriv;
				//RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_data_frame fail\n"));
				precvpriv->rx_drop++;
				DBG_COUNTER(adapter->rx_logs.core_rx_pre_data_err);
			}
			else if (retval == _SUCCESS)
			{
#ifdef DBG_RX_DUMP_EAP
				u8 bDumpRxPkt;
				u16 eth_type;

				// dump eapol
				rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_RXPKT, &(bDumpRxPkt));
				// get ether_type
				_rtw_memcpy(&eth_type, ptr + pattrib->hdrlen + pattrib->iv_len + LLC_HEADER_SIZE, 2);
				eth_type = ntohs((unsigned short) eth_type);
				if ((bDumpRxPkt == 4) && (eth_type == 0x888e))
					dump_rx_packet(ptr);
#endif
			}
			else
			{
				DBG_COUNTER(adapter->rx_logs.core_rx_pre_data_handled);
			}
			break;
		default:
			DBG_COUNTER(adapter->rx_logs.core_rx_pre_unknown);
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_data_frame fail! type=0x%x\n", type));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME validate_recv_data_frame fail! type=0x%x\n", type);
			#endif
			retval = _FAIL;
			break;
	}

exit:

_func_exit_;

	return retval;
}


//remove the wlanhdr and add the eth_hdr
#if 1

sint wlanhdr_to_ethhdr ( union recv_frame *precvframe);
sint wlanhdr_to_ethhdr ( union recv_frame *precvframe)
{
	sint	rmv_len;
	u16	eth_type, len;
	u8	bsnaphdr;
	u8	*psnap_type;
	struct ieee80211_snap_hdr	*psnap;
	
	sint ret=_SUCCESS;
	_adapter			*adapter =precvframe->u.hdr.adapter;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;

	u8	*ptr = get_recvframe_data(precvframe) ; // point to frame_ctrl field
	struct rx_pkt_attrib *pattrib = & precvframe->u.hdr.attrib;

_func_enter_;

	if(pattrib->encrypt){
		recvframe_pull_tail(precvframe, pattrib->icv_len);	
	}

	psnap=(struct ieee80211_snap_hdr	*)(ptr+pattrib->hdrlen + pattrib->iv_len);
	psnap_type=ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	/* convert hdr + possible LLC headers into Ethernet header */
	//eth_type = (psnap_type[0] << 8) | psnap_type[1];
	if((_rtw_memcmp(psnap, rtw_rfc1042_header, SNAP_SIZE) &&
		(_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_IPX, 2) == _FALSE) && 
		(_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_AARP, 2)==_FALSE) )||
		//eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
		 _rtw_memcmp(psnap, rtw_bridge_tunnel_header, SNAP_SIZE)){
		/* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
		bsnaphdr = _TRUE;
	}
	else {
		/* Leave Ethernet header part of hdr and full payload */
		bsnaphdr = _FALSE;
	}

	rmv_len = pattrib->hdrlen + pattrib->iv_len +(bsnaphdr?SNAP_SIZE:0);
	len = precvframe->u.hdr.len - rmv_len;

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n===pattrib->hdrlen: %x,  pattrib->iv_len:%x ===\n\n", pattrib->hdrlen,  pattrib->iv_len));

	_rtw_memcpy(&eth_type, ptr+rmv_len, 2);
	eth_type= ntohs((unsigned short )eth_type); //pattrib->ether_type
	pattrib->eth_type = eth_type;

#ifdef CONFIG_AUTO_AP_MODE
	if (0x8899 == pattrib->eth_type)
	{
		struct sta_info *psta = precvframe->u.hdr.psta;
		
		DBG_871X("wlan rx: got eth_type=0x%x\n", pattrib->eth_type);					
		
		if (psta && psta->isrc && psta->pid>0)
		{
			u16 rx_pid;

			rx_pid = *(u16*)(ptr+rmv_len+2);
			
			DBG_871X("wlan rx(pid=0x%x): sta("MAC_FMT") pid=0x%x\n", 
				rx_pid, MAC_ARG(psta->hwaddr), psta->pid);

			if(rx_pid == psta->pid)
			{
				int i;
				u16 len = *(u16*)(ptr+rmv_len+4);
				//u16 ctrl_type = *(u16*)(ptr+rmv_len+6);

				//DBG_871X("RC: len=0x%x, ctrl_type=0x%x\n", len, ctrl_type); 
				DBG_871X("RC: len=0x%x\n", len); 

				for(i=0;i<len;i++)
					DBG_871X("0x%x\n", *(ptr+rmv_len+6+i));
					//DBG_871X("0x%x\n", *(ptr+rmv_len+8+i));

				DBG_871X("RC-end\n"); 
			}			
		}		
	}
#endif //CONFIG_AUTO_AP_MODE

	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE))	   	
	{
		ptr += rmv_len ;	
		*ptr = 0x87;
		*(ptr+1) = 0x12;

		eth_type = 0x8712;
		// append rx status for mp test packets
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2)-24);
		if (!ptr) {
			ret = _FAIL;
			goto exiting;
		}
		_rtw_memcpy(ptr, get_rxmem(precvframe), 24);
		ptr+=24;
	}
	else {
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+ (bsnaphdr?2:0)));
		if (!ptr) {
			ret = _FAIL;
			goto exiting;
		}
	}

	if (ptr) {
		_rtw_memcpy(ptr, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

		if(!bsnaphdr) {
			len = htons(len);
			_rtw_memcpy(ptr+12, &len, 2);
		}
	}

exiting:
_func_exit_;	
	return ret;

}

#else

sint wlanhdr_to_ethhdr ( union recv_frame *precvframe)
{
	sint rmv_len;
	u16 eth_type;
	u8	bsnaphdr;
	u8	*psnap_type;
	struct ieee80211_snap_hdr	*psnap;

	sint ret=_SUCCESS;
	_adapter	*adapter =precvframe->u.hdr.adapter;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;

	u8* ptr = get_recvframe_data(precvframe) ; // point to frame_ctrl field
	struct rx_pkt_attrib *pattrib = & precvframe->u.hdr.attrib;
	struct _vlan *pvlan = NULL;

_func_enter_;

	psnap=(struct ieee80211_snap_hdr	*)(ptr+pattrib->hdrlen + pattrib->iv_len);
	psnap_type=ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	if (psnap->dsap==0xaa && psnap->ssap==0xaa && psnap->ctrl==0x03)
	{
		if (_rtw_memcmp(psnap->oui, oui_rfc1042, WLAN_IEEE_OUI_LEN))
			bsnaphdr=_TRUE;//wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_RFC1042;	
		else if (_rtw_memcmp(psnap->oui, SNAP_HDR_APPLETALK_DDP, WLAN_IEEE_OUI_LEN) &&
			_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_DDP, 2) )
			bsnaphdr=_TRUE;	//wlan_pkt_format = WLAN_PKT_FORMAT_APPLETALK;
		else if (_rtw_memcmp( psnap->oui, oui_8021h, WLAN_IEEE_OUI_LEN))
			bsnaphdr=_TRUE;	//wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_TUNNEL;
		else {
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("drop pkt due to invalid frame format!\n"));
			ret= _FAIL;
			goto exit;
		}

	} else
		bsnaphdr=_FALSE;//wlan_pkt_format = WLAN_PKT_FORMAT_OTHERS;

	rmv_len = pattrib->hdrlen + pattrib->iv_len +(bsnaphdr?SNAP_SIZE:0);
	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("===pattrib->hdrlen: %x,  pattrib->iv_len:%x ===\n", pattrib->hdrlen,  pattrib->iv_len));

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
	{
		ptr += rmv_len ;
		*ptr = 0x87;
		*(ptr+1) = 0x12;

		//back to original pointer
		ptr -= rmv_len;
	}

	ptr += rmv_len ;

	_rtw_memcpy(&eth_type, ptr, 2);
	eth_type= ntohs((unsigned short )eth_type); //pattrib->ether_type
	ptr +=2;

	if(pattrib->encrypt){
		recvframe_pull_tail(precvframe, pattrib->icv_len);
	}

	if(eth_type == 0x8100) //vlan
	{
		pvlan = (struct _vlan *) ptr;

		//eth_type = get_vlan_encap_proto(pvlan);
		//eth_type = pvlan->h_vlan_encapsulated_proto;//?
		rmv_len += 4;
		ptr+=4;
	}

	if(eth_type==0x0800)//ip
	{
		//struct iphdr*  piphdr = (struct iphdr*) ptr;
		//__u8 tos = (unsigned char)(pattrib->priority & 0xff);

		//piphdr->tos = tos;

		//if (piphdr->protocol == 0x06)
		//{
		//	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("@@@===recv tcp len:%d @@@===\n", precvframe->u.hdr.len));
		//}
	}
	else if(eth_type==0x8712)// append rx status for mp test packets
	{
		//ptr -= 16;
		//_rtw_memcpy(ptr, get_rxmem(precvframe), 16);
	}
	else
	{
#ifdef PLATFORM_OS_XP
		NDIS_PACKET_8021Q_INFO VlanPriInfo;
		UINT32 UserPriority = precvframe->u.hdr.attrib.priority;
		UINT32 VlanID = (pvlan!=NULL ? get_vlan_id(pvlan) : 0 );

		VlanPriInfo.Value =          // Get current value.
				NDIS_PER_PACKET_INFO_FROM_PACKET(precvframe->u.hdr.pkt, Ieee8021QInfo);

		VlanPriInfo.TagHeader.UserPriority = UserPriority;
		VlanPriInfo.TagHeader.VlanId =  VlanID ;

		VlanPriInfo.TagHeader.CanonicalFormatId = 0; // Should be zero.
		VlanPriInfo.TagHeader.Reserved = 0; // Should be zero.
		NDIS_PER_PACKET_INFO_FROM_PACKET(precvframe->u.hdr.pkt, Ieee8021QInfo) = VlanPriInfo.Value;
#endif
	}

	if(eth_type==0x8712)// append rx status for mp test packets
	{
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2)-24);
		_rtw_memcpy(ptr, get_rxmem(precvframe), 24);
		ptr+=24;
	}
	else
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2));

	_rtw_memcpy(ptr, pattrib->dst, ETH_ALEN);
	_rtw_memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

	eth_type = htons((unsigned short)eth_type) ;
	_rtw_memcpy(ptr+12, &eth_type, 2);

exit:

_func_exit_;

	return ret;
}
#endif


#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifdef PLATFORM_LINUX
static void recvframe_expand_pkt(
	PADAPTER padapter,
	union recv_frame *prframe)
{
	struct recv_frame_hdr *pfhdr;
	_pkt *ppkt;
	u8 shift_sz;
	u32 alloc_sz;
	u8 *ptr;


	pfhdr = &prframe->u.hdr;

	//	6 is for IP header 8 bytes alignment in QoS packet case.
	if (pfhdr->attrib.qos)
		shift_sz = 6;
	else
		shift_sz = 0;

	// for first fragment packet, need to allocate
	// (1536 + RXDESC_SIZE + drvinfo_sz) to reassemble packet
	//	8 is for skb->data 8 bytes alignment.
//	alloc_sz = _RND(1536 + RXDESC_SIZE + pfhdr->attrib.drvinfosize + shift_sz + 8, 128);
	alloc_sz = 1664; // round (1536 + 24 + 32 + shift_sz + 8) to 128 bytes alignment

	//3 1. alloc new skb
	// prepare extra space for 4 bytes alignment
	ppkt = rtw_skb_alloc(alloc_sz);

	if (!ppkt) return; // no way to expand

	//3 2. Prepare new skb to replace & release old skb
	// force ppkt->data at 8-byte alignment address
	skb_reserve(ppkt, 8 - ((SIZE_PTR)ppkt->data & 7));
	// force ip_hdr at 8-byte alignment address according to shift_sz
	skb_reserve(ppkt, shift_sz);

	// copy data to new pkt
	ptr = skb_put(ppkt, pfhdr->len);
	if (ptr)
		_rtw_memcpy(ptr, pfhdr->rx_data, pfhdr->len);

	rtw_skb_free(pfhdr->pkt);

	// attach new pkt to recvframe
	pfhdr->pkt = ppkt;
	pfhdr->rx_head = ppkt->head;
	pfhdr->rx_data = ppkt->data;
	pfhdr->rx_tail = skb_tail_pointer(ppkt);
	pfhdr->rx_end = skb_end_pointer(ppkt);
}
#else
#warning "recvframe_expand_pkt not implement, defrag may crash system"
#endif
#endif

//perform defrag
union recv_frame * recvframe_defrag(_adapter *adapter,_queue *defrag_q);
union recv_frame * recvframe_defrag(_adapter *adapter,_queue *defrag_q)
{
	_list	 *plist, *phead;
	u8	*data,wlanhdr_offset;
	u8	curfragnum;
	struct recv_frame_hdr *pfhdr,*pnfhdr;
	union recv_frame* prframe, *pnextrframe;
	_queue	*pfree_recv_queue;

_func_enter_;

	curfragnum=0;
	pfree_recv_queue=&adapter->recvpriv.free_recv_queue;

	phead = get_list_head(defrag_q);
	plist = get_next(phead);
	prframe = LIST_CONTAINOR(plist, union recv_frame, u);
	pfhdr=&prframe->u.hdr;
	rtw_list_delete(&(prframe->u.list));

	if(curfragnum!=pfhdr->attrib.frag_num)
	{
		//the first fragment number must be 0
		//free the whole queue
		rtw_free_recvframe(prframe, pfree_recv_queue);
		rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);

		return NULL;
	}

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_RX_COPY
	recvframe_expand_pkt(adapter, prframe);
#endif
#endif

	curfragnum++;

	plist= get_list_head(defrag_q);

	plist = get_next(plist);

	data=get_recvframe_data(prframe);

	while(rtw_end_of_queue_search(phead, plist) == _FALSE)
	{
		pnextrframe = LIST_CONTAINOR(plist, union recv_frame , u);
		pnfhdr=&pnextrframe->u.hdr;


		//check the fragment sequence  (2nd ~n fragment frame)

		if(curfragnum!=pnfhdr->attrib.frag_num)
		{
			//the fragment number must be increasing  (after decache)
			//release the defrag_q & prframe
			rtw_free_recvframe(prframe, pfree_recv_queue);
			rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);
			return NULL;
		}

		curfragnum++;

		//copy the 2nd~n fragment frame's payload to the first fragment
		//get the 2nd~last fragment frame's payload

		wlanhdr_offset = pnfhdr->attrib.hdrlen + pnfhdr->attrib.iv_len;

		recvframe_pull(pnextrframe, wlanhdr_offset);

		//append  to first fragment frame's tail (if privacy frame, pull the ICV)
		recvframe_pull_tail(prframe, pfhdr->attrib.icv_len);

		//memcpy
		_rtw_memcpy(pfhdr->rx_tail, pnfhdr->rx_data, pnfhdr->len);

		recvframe_put(prframe, pnfhdr->len);

		pfhdr->attrib.icv_len=pnfhdr->attrib.icv_len;
		plist = get_next(plist);

	};

	//free the defrag_q queue and return the prframe
	rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("Performance defrag!!!!!\n"));

_func_exit_;

	return prframe;
}

//check if need to defrag, if needed queue the frame to defrag_q
union recv_frame* recvframe_chk_defrag(PADAPTER padapter, union recv_frame *precv_frame)
{
	u8	ismfrag;
	u8	fragnum;
	u8	*psta_addr;
	struct recv_frame_hdr *pfhdr;
	struct sta_info *psta;
	struct sta_priv *pstapriv;
	_list *phead;
	union recv_frame *prtnframe = NULL;
	_queue *pfree_recv_queue, *pdefrag_q;

_func_enter_;

	pstapriv = &padapter->stapriv;

	pfhdr = &precv_frame->u.hdr;

	pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

	//need to define struct of wlan header frame ctrl
	ismfrag = pfhdr->attrib.mfrag;
	fragnum = pfhdr->attrib.frag_num;

	psta_addr = pfhdr->attrib.ta;
	psta = rtw_get_stainfo(pstapriv, psta_addr);
	if (psta == NULL)
	{
		u8 type = GetFrameType(pfhdr->rx_data);
		if (type != WIFI_DATA_TYPE) {
			psta = rtw_get_bcmc_stainfo(padapter);
			pdefrag_q = &psta->sta_recvpriv.defrag_q;
		} else
			pdefrag_q = NULL;
	}
	else
		pdefrag_q = &psta->sta_recvpriv.defrag_q;

	if ((ismfrag==0) && (fragnum==0))
	{
		prtnframe = precv_frame;//isn't a fragment frame
	}

	if (ismfrag==1)
	{
		//0~(n-1) fragment frame
		//enqueue to defraf_g
		if(pdefrag_q != NULL)
		{
			if(fragnum==0)
			{
				//the first fragment
				if(_rtw_queue_empty(pdefrag_q) == _FALSE)
				{
					//free current defrag_q
					rtw_free_recvframe_queue(pdefrag_q, pfree_recv_queue);
				}
			}


			//Then enqueue the 0~(n-1) fragment into the defrag_q

			//_rtw_spinlock(&pdefrag_q->lock);
			phead = get_list_head(pdefrag_q);
			rtw_list_insert_tail(&pfhdr->list, phead);
			//_rtw_spinunlock(&pdefrag_q->lock);

			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("Enqueuq: ismfrag = %d, fragnum= %d\n", ismfrag,fragnum));

			prtnframe=NULL;

		}
		else
		{
			//can't find this ta's defrag_queue, so free this recv_frame
			rtw_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe=NULL;
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("Free because pdefrag_q ==NULL: ismfrag = %d, fragnum= %d\n", ismfrag, fragnum));
		}

	}

	if((ismfrag==0)&&(fragnum!=0))
	{
		//the last fragment frame
		//enqueue the last fragment
		if(pdefrag_q != NULL)
		{
			//_rtw_spinlock(&pdefrag_q->lock);
			phead = get_list_head(pdefrag_q);
			rtw_list_insert_tail(&pfhdr->list,phead);
			//_rtw_spinunlock(&pdefrag_q->lock);

			//call recvframe_defrag to defrag
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("defrag: ismfrag = %d, fragnum= %d\n", ismfrag, fragnum));
			precv_frame = recvframe_defrag(padapter, pdefrag_q);
			prtnframe=precv_frame;

		}
		else
		{
			//can't find this ta's defrag_queue, so free this recv_frame
			rtw_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe=NULL;
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("Free because pdefrag_q ==NULL: ismfrag = %d, fragnum= %d\n", ismfrag,fragnum));
		}

	}


	if((prtnframe!=NULL)&&(prtnframe->u.hdr.attrib.privacy))
	{
		//after defrag we must check tkip mic code
		if(recvframe_chkmic(padapter,  prtnframe)==_FAIL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvframe_chkmic(padapter,  prtnframe)==_FAIL\n"));
			rtw_free_recvframe(prtnframe,pfree_recv_queue);
			prtnframe=NULL;
		}
	}

_func_exit_;

	return prtnframe;

}

int amsdu_to_msdu(_adapter *padapter, union recv_frame *prframe)
{
	int	a_len, padding_len;
	u16	nSubframe_Length;	
	u8	nr_subframes, i;
	u8	*pdata;
	_pkt *sub_pkt,*subframes[MAX_SUBFRAME_COUNT];
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &(precvpriv->free_recv_queue);
	int	ret = _SUCCESS;

	nr_subframes = 0;

	recvframe_pull(prframe, prframe->u.hdr.attrib.hdrlen);
	
	if(prframe->u.hdr.attrib.iv_len >0)
	{
		recvframe_pull(prframe, prframe->u.hdr.attrib.iv_len);
	}

	a_len = prframe->u.hdr.len;

	pdata = prframe->u.hdr.rx_data;

	while(a_len > ETH_HLEN) {

		/* Offset 12 denote 2 mac address */
		nSubframe_Length = RTW_GET_BE16(pdata + 12);

		if( a_len < (ETHERNET_HEADER_SIZE + nSubframe_Length) ) {
			DBG_871X("nRemain_Length is %d and nSubframe_Length is : %d\n",a_len,nSubframe_Length);
			break;
		}

		sub_pkt = rtw_os_alloc_msdu_pkt(prframe, nSubframe_Length, pdata);
		if (sub_pkt == NULL) {
			DBG_871X("%s(): allocate sub packet fail !!!\n",__FUNCTION__);
			break;
		}

		/* move the data point to data content */
		pdata += ETH_HLEN;
		a_len -= ETH_HLEN;

		subframes[nr_subframes++] = sub_pkt;

		if(nr_subframes >= MAX_SUBFRAME_COUNT) {
			DBG_871X("ParseSubframe(): Too many Subframes! Packets dropped!\n");
			break;
		}

		pdata += nSubframe_Length;
		a_len -= nSubframe_Length;
		if(a_len != 0) {
			padding_len = 4 - ((nSubframe_Length + ETH_HLEN) & (4-1));
			if(padding_len == 4) {
				padding_len = 0;
			}

			if(a_len < padding_len) {
				DBG_871X("ParseSubframe(): a_len < padding_len !\n");
				break;
			}
			pdata += padding_len;
			a_len -= padding_len;
		}
	}

	for(i=0; i<nr_subframes; i++){
		sub_pkt = subframes[i];

		/* Indicat the packets to upper layer */
		if (sub_pkt) {
			rtw_os_recv_indicate_pkt(padapter, sub_pkt, &prframe->u.hdr.attrib);
		}
	}

	prframe->u.hdr.len = 0;
	rtw_free_recvframe(prframe, pfree_recv_queue);//free this recv_frame
	
	return ret;
}

int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, u16 seq_num);
int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, u16 seq_num)
{
	PADAPTER padapter = preorder_ctrl->padapter;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	u8	wsize = preorder_ctrl->wsize_b;
	u16	wend = (preorder_ctrl->indicate_seq + wsize -1) & 0xFFF;//% 4096;

	// Rx Reorder initialize condition.
	if (preorder_ctrl->indicate_seq == 0xFFFF)
	{
		preorder_ctrl->indicate_seq = seq_num;
		#ifdef DBG_RX_SEQ
		DBG_871X("DBG_RX_SEQ %s:%d init IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
			preorder_ctrl->indicate_seq, seq_num);
		#endif

		//DbgPrint("check_indicate_seq, 1st->indicate_seq=%d\n", precvpriv->indicate_seq);
	}

	//DbgPrint("enter->check_indicate_seq(): IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);

	// Drop out the packet which SeqNum is smaller than WinStart
	if( SN_LESS(seq_num, preorder_ctrl->indicate_seq) )
	{
		//RT_TRACE(COMP_RX_REORDER, DBG_LOUD, ("CheckRxTsIndicateSeq(): Packet Drop! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, NewSeqNum));
		//DbgPrint("CheckRxTsIndicateSeq(): Packet Drop! IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);

		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("%s IndicateSeq: %d > NewSeq: %d\n", __FUNCTION__, 
			preorder_ctrl->indicate_seq, seq_num);
		#endif


		return _FALSE;
	}

	//
	// Sliding window manipulation. Conditions includes:
	// 1. Incoming SeqNum is equal to WinStart =>Window shift 1
	// 2. Incoming SeqNum is larger than the WinEnd => Window shift N
	//
	if( SN_EQUAL(seq_num, preorder_ctrl->indicate_seq) )
	{
		preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) & 0xFFF;

		#ifdef DBG_RX_SEQ
		DBG_871X("DBG_RX_SEQ %s:%d SN_EQUAL IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
			preorder_ctrl->indicate_seq, seq_num);
		#endif
	}
	else if(SN_LESS(wend, seq_num))
	{
		//RT_TRACE(COMP_RX_REORDER, DBG_LOUD, ("CheckRxTsIndicateSeq(): Window Shift! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, NewSeqNum));
		//DbgPrint("CheckRxTsIndicateSeq(): Window Shift! IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);

		// boundary situation, when seq_num cross 0xFFF
		if(seq_num >= (wsize - 1))
			preorder_ctrl->indicate_seq = seq_num + 1 -wsize;
		else
			preorder_ctrl->indicate_seq = 0xFFF - (wsize - (seq_num + 1)) + 1;
		pdbgpriv->dbg_rx_ampdu_window_shift_cnt++;
		#ifdef DBG_RX_SEQ
		DBG_871X("DBG_RX_SEQ %s:%d SN_LESS(wend, seq_num) IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
			preorder_ctrl->indicate_seq, seq_num);
		#endif
	}

	//DbgPrint("exit->check_indicate_seq(): IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);

	return _TRUE;
}

int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl, union recv_frame *prframe);
int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl, union recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	_list	*phead, *plist;
	union recv_frame *pnextrframe;
	struct rx_pkt_attrib *pnextattrib;

	//DbgPrint("+enqueue_reorder_recvframe()\n");

	//_enter_critical_ex(&ppending_recvframe_queue->lock, &irql);
	//_rtw_spinlock_ex(&ppending_recvframe_queue->lock);


	phead = get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

	while(rtw_end_of_queue_search(phead, plist) == _FALSE)
	{
		pnextrframe = LIST_CONTAINOR(plist, union recv_frame, u);
		pnextattrib = &pnextrframe->u.hdr.attrib;

		if(SN_LESS(pnextattrib->seq_num, pattrib->seq_num))
		{
			plist = get_next(plist);
		}
		else if( SN_EQUAL(pnextattrib->seq_num, pattrib->seq_num))
		{
			//Duplicate entry is found!! Do not insert current entry.
			//RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("InsertRxReorderList(): Duplicate packet is dropped!! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, SeqNum));

			//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);

			return _FALSE;
		}
		else
		{
			break;
		}

		//DbgPrint("enqueue_reorder_recvframe():while\n");

	}


	//_enter_critical_ex(&ppending_recvframe_queue->lock, &irql);
	//_rtw_spinlock_ex(&ppending_recvframe_queue->lock);

	rtw_list_delete(&(prframe->u.hdr.list));

	rtw_list_insert_tail(&(prframe->u.hdr.list), plist);

	//_rtw_spinunlock_ex(&ppending_recvframe_queue->lock);
	//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);


	//RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("InsertRxReorderList(): Pkt insert into buffer!! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, SeqNum));
	return _TRUE;

}

void recv_indicatepkts_pkt_loss_cnt(struct debug_priv *pdbgpriv, u64 prev_seq, u64 current_seq);
void recv_indicatepkts_pkt_loss_cnt(struct debug_priv *pdbgpriv, u64 prev_seq, u64 current_seq)
{
	if(current_seq < prev_seq)
	{
		pdbgpriv->dbg_rx_ampdu_loss_count+= (4096 + current_seq - prev_seq);

	}
	else
	{
		pdbgpriv->dbg_rx_ampdu_loss_count+= (current_seq - prev_seq);
	}
}
int recv_indicatepkts_in_order(_adapter *padapter, struct recv_reorder_ctrl *preorder_ctrl, int bforced);
int recv_indicatepkts_in_order(_adapter *padapter, struct recv_reorder_ctrl *preorder_ctrl, int bforced)
{
	//_irqL irql;
	//u8 bcancelled;
	_list	*phead, *plist;
	union recv_frame *prframe;
	struct rx_pkt_attrib *pattrib;
	//u8 index = 0;
	int bPktInBuf = _FALSE;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	DBG_COUNTER(padapter->rx_logs.core_rx_post_indicate_in_oder);

	//DbgPrint("+recv_indicatepkts_in_order\n");

	//_enter_critical_ex(&ppending_recvframe_queue->lock, &irql);
	//_rtw_spinlock_ex(&ppending_recvframe_queue->lock);

	phead = 	get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

#if 0
	// Check if there is any other indication thread running.
	if(pTS->RxIndicateState == RXTS_INDICATE_PROCESSING)
		return;
#endif

	// Handling some condition for forced indicate case.
	if(bforced==_TRUE)
	{
		pdbgpriv->dbg_rx_ampdu_forced_indicate_count++;
		if(rtw_is_list_empty(phead))
		{
			// _exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
			//_rtw_spinunlock_ex(&ppending_recvframe_queue->lock);
			return _TRUE;
		}
	
		prframe = LIST_CONTAINOR(plist, union recv_frame, u);
		pattrib = &prframe->u.hdr.attrib;	

		#ifdef DBG_RX_SEQ
		DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
			preorder_ctrl->indicate_seq, pattrib->seq_num);
		#endif
		recv_indicatepkts_pkt_loss_cnt(pdbgpriv,preorder_ctrl->indicate_seq,pattrib->seq_num);
		preorder_ctrl->indicate_seq = pattrib->seq_num;		
		
	}

	// Prepare indication list and indication.
	// Check if there is any packet need indicate.
	while(!rtw_is_list_empty(phead))
	{
	
		prframe = LIST_CONTAINOR(plist, union recv_frame, u);
		pattrib = &prframe->u.hdr.attrib;

		if(!SN_LESS(preorder_ctrl->indicate_seq, pattrib->seq_num))
		{
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
				 ("recv_indicatepkts_in_order: indicate=%d seq=%d amsdu=%d\n",
				  preorder_ctrl->indicate_seq, pattrib->seq_num, pattrib->amsdu));

#if 0
			// This protect buffer from overflow.
			if(index >= REORDER_WIN_SIZE)
			{
				RT_ASSERT(FALSE, ("IndicateRxReorderList(): Buffer overflow!! \n"));
				bPktInBuf = TRUE;
				break;
			}
#endif

			plist = get_next(plist);
			rtw_list_delete(&(prframe->u.hdr.list));

			if(SN_EQUAL(preorder_ctrl->indicate_seq, pattrib->seq_num))
			{
				preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) & 0xFFF;
				#ifdef DBG_RX_SEQ
				DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
					preorder_ctrl->indicate_seq, pattrib->seq_num);
				#endif
			}

#if 0
			index++;
			if(index==1)
			{
				//Cancel previous pending timer.
				//PlatformCancelTimer(Adapter, &pTS->RxPktPendingTimer);
				if(bforced!=_TRUE)
				{
					//DBG_871X("_cancel_timer(&preorder_ctrl->reordering_ctrl_timer, &bcancelled);\n");
					_cancel_timer(&preorder_ctrl->reordering_ctrl_timer, &bcancelled);
				}
			}
#endif

			//Set this as a lock to make sure that only one thread is indicating packet.
			//pTS->RxIndicateState = RXTS_INDICATE_PROCESSING;

			// Indicate packets
			//RT_ASSERT((index<=REORDER_WIN_SIZE), ("RxReorderIndicatePacket(): Rx Reorder buffer full!! \n"));


			//indicate this recv_frame
			//DbgPrint("recv_indicatepkts_in_order, indicate_seq=%d, seq_num=%d\n", precvpriv->indicate_seq, pattrib->seq_num);
			if(!pattrib->amsdu)
			{
				//DBG_871X("recv_indicatepkts_in_order, amsdu!=1, indicate_seq=%d, seq_num=%d\n", preorder_ctrl->indicate_seq, pattrib->seq_num);

				if (!RTW_CANNOT_RUN(padapter))
					rtw_recv_indicatepkt(padapter, prframe);/*indicate this recv_frame*/

			}
			else if(pattrib->amsdu==1)
			{
				if(amsdu_to_msdu(padapter, prframe)!=_SUCCESS)
				{
					rtw_free_recvframe(prframe, &precvpriv->free_recv_queue);
				}
			}
			else
			{
				//error condition;
			}


			//Update local variables.
			bPktInBuf = _FALSE;

		}
		else
		{
			bPktInBuf = _TRUE;
			break;
		}

		//DbgPrint("recv_indicatepkts_in_order():while\n");

	}

	//_rtw_spinunlock_ex(&ppending_recvframe_queue->lock);
	//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);

/*
	//Release the indication lock and set to new indication step.
	if(bPktInBuf)
	{
		// Set new pending timer.
		//pTS->RxIndicateState = RXTS_INDICATE_REORDER;
		//PlatformSetTimer(Adapter, &pTS->RxPktPendingTimer, pHTInfo->RxReorderPendingTime);
		//DBG_871X("_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME)\n");
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
	}
	else
	{
		//pTS->RxIndicateState = RXTS_INDICATE_IDLE;
	}
*/
	//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);

	//return _TRUE;
	return bPktInBuf;

}

int recv_indicatepkt_reorder(_adapter *padapter, union recv_frame *prframe);
int recv_indicatepkt_reorder(_adapter *padapter, union recv_frame *prframe)
{
	_irqL irql;
	int retval = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct recv_reorder_ctrl *preorder_ctrl = prframe->u.hdr.preorder_ctrl;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	DBG_COUNTER(padapter->rx_logs.core_rx_post_indicate_reoder);

	if(!pattrib->amsdu)
	{
		//s1.
                retval = wlanhdr_to_ethhdr(prframe);
                if (retval != _SUCCESS)
                {
                        RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("wlanhdr_to_ethhdr: drop pkt \n"));
                        #ifdef DBG_RX_DROP_FRAME
                        DBG_871X("DBG_RX_DROP_FRAME %s wlanhdr_to_ethhdr error!\n", __FUNCTION__);
                        #endif
                        return retval;
                }

		//if ((pattrib->qos!=1) /*|| pattrib->priority!=0 || IS_MCAST(pattrib->ra)*/
		//	|| (pattrib->eth_type==0x0806) || (pattrib->ack_policy!=0))
		if (pattrib->qos!=1)
		{
			if (!RTW_CANNOT_RUN(padapter)) {
				RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("@@@@  recv_indicatepkt_reorder -recv_func recv_indicatepkt\n" ));

				rtw_recv_indicatepkt(padapter, prframe);
				return _SUCCESS;

			}
			
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s pattrib->qos !=1\n", __FUNCTION__);
			#endif
			
			return _FAIL;
		
		}

		if (preorder_ctrl->enable == _FALSE)
		{
			//indicate this recv_frame			
			preorder_ctrl->indicate_seq = pattrib->seq_num;
			#ifdef DBG_RX_SEQ
			DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
				preorder_ctrl->indicate_seq, pattrib->seq_num);
			#endif
			
			rtw_recv_indicatepkt(padapter, prframe);		
			
			preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;
			#ifdef DBG_RX_SEQ
			DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
				preorder_ctrl->indicate_seq, pattrib->seq_num);
			#endif
			
			return _SUCCESS;	
		}			

#ifndef CONFIG_RECV_REORDERING_CTRL
		//indicate this recv_frame
		rtw_recv_indicatepkt(padapter, prframe);
		return _SUCCESS;
#endif

	}
	else if(pattrib->amsdu==1) //temp filter -> means didn't support A-MSDUs in a A-MPDU
	{
		if (preorder_ctrl->enable == _FALSE)
		{
			preorder_ctrl->indicate_seq = pattrib->seq_num;
			#ifdef DBG_RX_SEQ
			DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
				preorder_ctrl->indicate_seq, pattrib->seq_num);
			#endif

			retval = amsdu_to_msdu(padapter, prframe);

			preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;
			#ifdef DBG_RX_SEQ
			DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
				preorder_ctrl->indicate_seq, pattrib->seq_num);
			#endif

			if(retval != _SUCCESS){
				#ifdef DBG_RX_DROP_FRAME
				DBG_871X("DBG_RX_DROP_FRAME %s amsdu_to_msdu fail\n", __FUNCTION__);
				#endif
			}

			return retval;
		}
	}
	else
	{

	}

	_enter_critical_bh(&ppending_recvframe_queue->lock, &irql);

	RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
		 ("recv_indicatepkt_reorder: indicate=%d seq=%d\n",
		  preorder_ctrl->indicate_seq, pattrib->seq_num));

	//s2. check if winstart_b(indicate_seq) needs to been updated
	if(!check_indicate_seq(preorder_ctrl, pattrib->seq_num))
	{
		pdbgpriv->dbg_rx_ampdu_drop_count++;
		//pHTInfo->RxReorderDropCounter++;
		//ReturnRFDList(Adapter, pRfd);
		//RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("RxReorderIndicatePacket() ==> Packet Drop!!\n"));
		//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
		//return _FAIL;

		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s check_indicate_seq fail\n", __FUNCTION__);
		#endif
#if 0		
		rtw_recv_indicatepkt(padapter, prframe);

		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);
		
		goto _success_exit;
#else
		goto _err_exit;
#endif
	}


	//s3. Insert all packet into Reorder Queue to maintain its ordering.
	if(!enqueue_reorder_recvframe(preorder_ctrl, prframe))
	{
		//DbgPrint("recv_indicatepkt_reorder, enqueue_reorder_recvframe fail!\n");
		//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
		//return _FAIL;
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s enqueue_reorder_recvframe fail\n", __FUNCTION__);
		#endif
		goto _err_exit;
	}


	//s4.
	// Indication process.
	// After Packet dropping and Sliding Window shifting as above, we can now just indicate the packets
	// with the SeqNum smaller than latest WinStart and buffer other packets.
	//
	// For Rx Reorder condition:
	// 1. All packets with SeqNum smaller than WinStart => Indicate
	// 2. All packets with SeqNum larger than or equal to WinStart => Buffer it.
	//

	//recv_indicatepkts_in_order(padapter, preorder_ctrl, _TRUE);
	if(recv_indicatepkts_in_order(padapter, preorder_ctrl, _FALSE)==_TRUE)
	{
		if (!preorder_ctrl->bReorderWaiting) {
			preorder_ctrl->bReorderWaiting = _TRUE;
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
		}
		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);
	}
	else
	{
		preorder_ctrl->bReorderWaiting = _FALSE;
		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);
		_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);
	}


_success_exit:

	return _SUCCESS;

_err_exit:

        _exit_critical_bh(&ppending_recvframe_queue->lock, &irql);

	return _FAIL;
}


void rtw_reordering_ctrl_timeout_handler(void *pcontext)
{
	_irqL irql;
	struct recv_reorder_ctrl *preorder_ctrl = (struct recv_reorder_ctrl *)pcontext;
	_adapter *padapter = preorder_ctrl->padapter;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;


	if (RTW_CANNOT_RUN(padapter))
		return;

	//DBG_871X("+rtw_reordering_ctrl_timeout_handler()=>\n");

	_enter_critical_bh(&ppending_recvframe_queue->lock, &irql);

	if (preorder_ctrl) {
		preorder_ctrl->bReorderWaiting = _FALSE;
	}

	if(recv_indicatepkts_in_order(padapter, preorder_ctrl, _TRUE)==_TRUE)
	{
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);		
	}

	_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);

}

int process_recv_indicatepkts(_adapter *padapter, union recv_frame *prframe);
int process_recv_indicatepkts(_adapter *padapter, union recv_frame *prframe)
{
	int retval = _SUCCESS;
	//struct recv_priv *precvpriv = &padapter->recvpriv;
	//struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_TDLS
	struct sta_info *psta = prframe->u.hdr.psta;
#endif //CONFIG_TDLS

#ifdef CONFIG_80211N_HT

	struct ht_priv	*phtpriv = &pmlmepriv->htpriv;
	
	DBG_COUNTER(padapter->rx_logs.core_rx_post_indicate);

#ifdef CONFIG_TDLS
	if( (phtpriv->ht_option==_TRUE) ||
		((psta->tdls_sta_state & TDLS_LINKED_STATE) && 
		 (psta->htpriv.ht_option==_TRUE) &&
		 (psta->htpriv.ampdu_enable==_TRUE))) //B/G/N Mode
#else
	if(phtpriv->ht_option==_TRUE)  //B/G/N Mode
#endif //CONFIG_TDLS
	{
		//prframe->u.hdr.preorder_ctrl = &precvpriv->recvreorder_ctrl[pattrib->priority];

		if(recv_indicatepkt_reorder(padapter, prframe)!=_SUCCESS)// including perform A-MPDU Rx Ordering Buffer Control
		{
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s recv_indicatepkt_reorder error!\n", __FUNCTION__);
			#endif
		
			if (!RTW_CANNOT_RUN(padapter))	{
				retval = _FAIL;
				return retval;
			}
		}
	}
	else //B/G mode
#endif
	{
		retval=wlanhdr_to_ethhdr (prframe);
		if(retval != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("wlanhdr_to_ethhdr: drop pkt \n"));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s wlanhdr_to_ethhdr error!\n", __FUNCTION__);
			#endif
			return retval;
		}

		if (!RTW_CANNOT_RUN(padapter)) {
			//indicate this recv_frame
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("@@@@ process_recv_indicatepkts- recv_func recv_indicatepkt\n" ));
			rtw_recv_indicatepkt(padapter, prframe);
		}
		else
		{
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("@@@@ process_recv_indicatepkts- recv_func free_indicatepkt\n" ));

			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("recv_func:bDriverStopped(%s) OR bSurpriseRemoved(%s)"
				, rtw_is_drv_stopped(padapter)?"True":"False"
				, rtw_is_surprise_removed(padapter)?"True":"False"));
			retval = _FAIL;
			return retval;
		}

	}

	return retval;

}

#ifdef CONFIG_MP_INCLUDED
int validate_mp_recv_frame(_adapter *adapter, union recv_frame *precv_frame)
{
	int ret = _SUCCESS;
	u8 *ptr = precv_frame->u.hdr.rx_data;	
	u8 type,subtype;

	if(!adapter->mppriv.bmac_filter)	
		return ret;
#if 0	
	if (1){
		u8 bDumpRxPkt;
		type =  GetFrameType(ptr);
		subtype = GetFrameSubType(ptr); //bit(7)~bit(2)	
		
		rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_RXPKT, &(bDumpRxPkt));
		if(bDumpRxPkt ==1){//dump all rx packets
			int i;
			DBG_871X("############ type:0x%02x subtype:0x%02x ################# \n",type,subtype);
			
			for(i=0; i<64;i=i+8)
				DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(ptr+i),
				*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));
			DBG_871X("############################# \n");
		}
	}
#endif		

	if(_rtw_memcmp( GetAddr2Ptr(ptr), adapter->mppriv.mac_filter, ETH_ALEN) == _FALSE )
		ret = _FAIL;

	return ret;
}
#endif

static sint MPwlanhdr_to_ethhdr ( union recv_frame *precvframe)
{
	sint	rmv_len;
	u16 eth_type, len;
	u8	bsnaphdr;
	u8	*psnap_type;
	u8 mcastheadermac[]={0x01,0x00,0x5e};
	
	struct ieee80211_snap_hdr	*psnap;
	
	sint ret=_SUCCESS;
	_adapter			*adapter =precvframe->u.hdr.adapter;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;

	u8	*ptr = get_recvframe_data(precvframe) ; // point to frame_ctrl field
	struct rx_pkt_attrib *pattrib = & precvframe->u.hdr.attrib;

_func_enter_;

	if(pattrib->encrypt){
		recvframe_pull_tail(precvframe, pattrib->icv_len);	
	}

	psnap=(struct ieee80211_snap_hdr	*)(ptr+pattrib->hdrlen + pattrib->iv_len);
	psnap_type=ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	/* convert hdr + possible LLC headers into Ethernet header */
	//eth_type = (psnap_type[0] << 8) | psnap_type[1];
	if((_rtw_memcmp(psnap, rtw_rfc1042_header, SNAP_SIZE) &&
		(_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_IPX, 2) == _FALSE) && 
		(_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_AARP, 2)==_FALSE) )||
		//eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
		 _rtw_memcmp(psnap, rtw_bridge_tunnel_header, SNAP_SIZE)){
		/* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
		bsnaphdr = _TRUE;
	}
	else {
		/* Leave Ethernet header part of hdr and full payload */
		bsnaphdr = _FALSE;
	}

	rmv_len = pattrib->hdrlen + pattrib->iv_len +(bsnaphdr?SNAP_SIZE:0);
	len = precvframe->u.hdr.len - rmv_len;

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n===pattrib->hdrlen: %x,  pattrib->iv_len:%x ===\n\n", pattrib->hdrlen,	pattrib->iv_len));

	_rtw_memcpy(&eth_type, ptr+rmv_len, 2);
	eth_type= ntohs((unsigned short )eth_type); //pattrib->ether_type
	pattrib->eth_type = eth_type;

	{
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+ (bsnaphdr?2:0)));
	}

	_rtw_memcpy(ptr, pattrib->dst, ETH_ALEN);
	_rtw_memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

	if(!bsnaphdr) {
		len = htons(len);
		_rtw_memcpy(ptr+12, &len, 2);
	}
	

		len = htons(pattrib->seq_num);
		//DBG_871X("wlan seq = %d ,seq_num =%x\n",len,pattrib->seq_num);
		_rtw_memcpy(ptr+12,&len, 2);
	if(adapter->mppriv.bRTWSmbCfg==_TRUE)
	{
//		if(_rtw_memcmp(mcastheadermac, pattrib->dst, 3) == _TRUE)//SimpleConfig Dest.
//			_rtw_memcpy(ptr+ETH_ALEN, pattrib->bssid, ETH_ALEN);

		if(_rtw_memcmp(mcastheadermac, pattrib->bssid, 3) == _TRUE) //SimpleConfig Dest.
			_rtw_memcpy(ptr, pattrib->bssid, ETH_ALEN);

	}

	
_func_exit_;	
	return ret;

}


int mp_recv_frame(_adapter *padapter, union recv_frame *rframe)
{
	int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &rframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
#ifdef CONFIG_MP_INCLUDED
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mp_priv *pmppriv = &padapter->mppriv;
#endif //CONFIG_MP_INCLUDED
	u8 type;
	u8 *ptr = rframe->u.hdr.rx_data;
	u8 *psa, *pda, *pbssid;
	struct sta_info *psta = NULL;
    	DBG_COUNTER(padapter->rx_logs.core_rx_pre);
    	
	if ( (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) )//&&(padapter->mppriv.check_mp_pkt == 0))
	{
		if (pattrib->crc_err == 1){
			padapter->mppriv.rx_crcerrpktcount++;
		}
		else{
			if(_SUCCESS == validate_mp_recv_frame(padapter, rframe))
				padapter->mppriv.rx_pktcount++;
			else
				padapter->mppriv.rx_pktcount_filter_out++;
		}

		if(pmppriv->rx_bindicatePkt == _FALSE)
		{
			//RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("MP - Not in loopback mode , drop pkt \n"));
			ret = _FAIL;
			rtw_free_recvframe(rframe, pfree_recv_queue);//free this recv_frame
			goto exit;
		}
		else 
		{				
			type =	GetFrameType(ptr);
			pattrib->to_fr_ds = get_tofr_ds(ptr);
			pattrib->frag_num = GetFragNum(ptr);
			pattrib->seq_num = GetSequence(ptr);
			pattrib->pw_save = GetPwrMgt(ptr);
			pattrib->mfrag = GetMFrag(ptr);
			pattrib->mdata = GetMData(ptr);
			pattrib->privacy = GetPrivacy(ptr);
			pattrib->order = GetOrder(ptr);
	
			if(type ==WIFI_DATA_TYPE)
			{
				pda = get_da(ptr);
				psa = get_sa(ptr);
				pbssid = get_hdr_bssid(ptr);
				
				_rtw_memcpy(pattrib->dst, pda, ETH_ALEN);
				_rtw_memcpy(pattrib->src, psa, ETH_ALEN);
				_rtw_memcpy(pattrib->bssid, pbssid, ETH_ALEN);
			
			switch(pattrib->to_fr_ds)
			{
			case 0:
				_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
				_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
				ret = sta2sta_data_frame(padapter, rframe, &psta);
				break;

			case 1:
		
				_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
				_rtw_memcpy(pattrib->ta, pbssid, ETH_ALEN);
				ret = ap2sta_data_frame(padapter, rframe, &psta);
		
				break;

			case 2:
				_rtw_memcpy(pattrib->ra, pbssid, ETH_ALEN);
				_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
				ret = sta2ap_data_frame(padapter, rframe, &psta);
				break;

			case 3:
				_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
				_rtw_memcpy(pattrib->ta, GetAddr2Ptr(ptr), ETH_ALEN);
				ret =_FAIL;
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" case 3\n"));
				break;

			default:
				ret =_FAIL;
				break;
			}
		
			ret = MPwlanhdr_to_ethhdr (rframe);
				
			if (ret != _SUCCESS)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("wlanhdr_to_ethhdr: drop pkt \n"));
				#ifdef DBG_RX_DROP_FRAME
					DBG_871X("DBG_RX_DROP_FRAME %s wlanhdr_to_ethhdr: drop pkt\n", __FUNCTION__);
				#endif
					rtw_free_recvframe(rframe, pfree_recv_queue);//free this recv_frame
					ret = _FAIL;
					goto exit;
				}
				if (!RTW_CANNOT_RUN(padapter)) {
					RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@ recv_func: recv_func rtw_recv_indicatepkt\n" ));
							//indicate this recv_frame
					ret = rtw_recv_indicatepkt(padapter, rframe);
					if (ret != _SUCCESS)
					{	
					#ifdef DBG_RX_DROP_FRAME
								DBG_871X("DBG_RX_DROP_FRAME %s rtw_recv_indicatepkt fail!\n", __FUNCTION__);
					#endif
								rtw_free_recvframe(rframe, pfree_recv_queue);//free this recv_frame
								ret = _FAIL;

								goto exit;
					}
				}
				else
				{
					RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@  recv_func: rtw_free_recvframe\n" ));
					RT_TRACE(_module_rtl871x_recv_c_, _drv_debug_, ("recv_func:bDriverStopped(%s) OR bSurpriseRemoved(%s)"
						, rtw_is_drv_stopped(padapter)?"True":"False"
						, rtw_is_surprise_removed(padapter)?"True":"False"));
					#ifdef DBG_RX_DROP_FRAME
						DBG_871X("DBG_RX_DROP_FRAME %s ecv_func:bDriverStopped(%s) OR bSurpriseRemoved(%s)\n", __func__,
									rtw_is_drv_stopped(padapter)?"True":"False",
									rtw_is_surprise_removed(padapter)?"True":"False");
					#endif
					ret = _FAIL;
					rtw_free_recvframe(rframe, pfree_recv_queue);//free this recv_frame
						goto exit;
				}

			}
		}
				
	}

	RT_TRACE(_module_rtl871x_recv_c_, _drv_info_, ("recv_func: validate_recv_frame fail! drop pkt\n"));
	rtw_free_recvframe(rframe, pfree_recv_queue);//free this recv_frame
	ret = _FAIL;

exit:
		return ret;

}

static sint fill_radiotap_hdr(_adapter *padapter, union recv_frame *precvframe, u8 *buf)
{
#define CHAN2FREQ(a) ((a < 14)?(2407+5*a):(5000+5*a))

#if 0
#define RTW_RX_RADIOTAP_PRESENT (                 \
		(1 << IEEE80211_RADIOTAP_TSFT)              | \
		(1 << IEEE80211_RADIOTAP_FLAGS)             | \
		(1 << IEEE80211_RADIOTAP_RATE)              | \
		(1 << IEEE80211_RADIOTAP_CHANNEL)           | \
		(0 << IEEE80211_RADIOTAP_FHSS)              | \
		(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL)     | \
		(1 << IEEE80211_RADIOTAP_DBM_ANTNOISE)      | \
		(0 << IEEE80211_RADIOTAP_LOCK_QUALITY)      | \
		(0 << IEEE80211_RADIOTAP_TX_ATTENUATION)    | \
		(0 << IEEE80211_RADIOTAP_DB_TX_ATTENUATION) | \
		(0 << IEEE80211_RADIOTAP_DBM_TX_POWER)      | \
		(1 << IEEE80211_RADIOTAP_ANTENNA)           | \
		(1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL)      | \
		(0 << IEEE80211_RADIOTAP_DB_ANTNOISE)       | \
		(0 << IEEE80211_RADIOTAP_RX_FLAGS)          | \
		(0 << IEEE80211_RADIOTAP_TX_FLAGS)          | \
		(0 << IEEE80211_RADIOTAP_RTS_RETRIES)       | \
		(0 << IEEE80211_RADIOTAP_DATA_RETRIES)      | \
		(0 << IEEE80211_RADIOTAP_MCS)               | \
		(0 << IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE)| \
		(0 << IEEE80211_RADIOTAP_VENDOR_NAMESPACE)  | \
		(0 << IEEE80211_RADIOTAP_EXT)               | \
		0)

	/* (0 << IEEE80211_RADIOTAP_AMPDU_STATUS)      | \ */
	/* (0 << IEEE80211_RADIOTAP_VHT)               | \ */
#endif
#ifndef IEEE80211_RADIOTAP_MCS
#define IEEE80211_RADIOTAP_MCS 19
#endif
#ifndef IEEE80211_RADIOTAP_VHT
#define IEEE80211_RADIOTAP_VHT 21
#endif

#ifndef IEEE80211_RADIOTAP_F_BADFCS
#define IEEE80211_RADIOTAP_F_BADFCS 0x40 /* bad FCS */
#endif

	sint ret = _SUCCESS;
	_adapter			*adapter = precvframe->u.hdr.adapter;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;

	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);

	u16 tmp_16bit = 0;

	u8 data_rate[] = {
		2, 4, 11, 22, /* CCK */
		12, 18, 24, 36, 48, 72, 93, 108, /* OFDM */
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, /* HT MCS index */
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, /* VHT Nss 1 */
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, /* VHT Nss 2 */
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, /* VHT Nss 3 */
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, /* VHT Nss 4 */
	};

	_pkt *pskb = NULL;

	struct ieee80211_radiotap_header *rtap_hdr = NULL;
	u8 *ptr = NULL;

	u8 hdr_buf[64] = {0};
	u16 rt_len = 8;

	/* create header */
	rtap_hdr = (struct ieee80211_radiotap_header *)&hdr_buf[0];
	rtap_hdr->it_version = PKTHDR_RADIOTAP_VERSION;

	/* tsft */
	if (pattrib->tsfl) {
		u64 tmp_64bit;

		rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_TSFT);
		tmp_64bit = cpu_to_le64(pattrib->tsfl);
		memcpy(&hdr_buf[rt_len], &tmp_64bit, 8);
		rt_len += 8;
	}

	/* flags */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_FLAGS);
	if (0)
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_CFP;

	if (0)
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_SHORTPRE;

	if ((pattrib->encrypt == 1) || (pattrib->encrypt == 5))
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_WEP;

	if (pattrib->mfrag)
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_FRAG;

	/* always append FCS */
	hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_FCS;

	if (0)
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_DATAPAD;

	if (pattrib->crc_err)
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_BADFCS;

	if (pattrib->sgi) {
		/* Currently unspecified but used */
		hdr_buf[rt_len] |= 0x80;
	}
	rt_len += 1;

	/* rate */
	if (pattrib->data_rate < 12) {
		rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_RATE);
		if (pattrib->data_rate < 4) {
			/* CCK */
			hdr_buf[rt_len] = data_rate[pattrib->data_rate];
		} else {
			/* OFDM */
			hdr_buf[rt_len] = data_rate[pattrib->data_rate];
		}
	}
	rt_len += 1; /* force padding 1 byte for aligned */

	/* channel */
	tmp_16bit = 0;
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_CHANNEL);
	tmp_16bit = CHAN2FREQ(rtw_get_oper_ch(padapter));
	/*tmp_16bit = CHAN2FREQ(pHalData->CurrentChannel);*/
	memcpy(&hdr_buf[rt_len], &tmp_16bit, 2);
	rt_len += 2;

	/* channel flags */
	tmp_16bit = 0;
	if (pHalData->CurrentBandType == 0) 
		tmp_16bit |= cpu_to_le16(IEEE80211_CHAN_2GHZ);
	else 
		tmp_16bit |= cpu_to_le16(IEEE80211_CHAN_5GHZ);

	if (pattrib->data_rate < 12) {
		if (pattrib->data_rate < 4) {
			/* CCK */
			tmp_16bit |= cpu_to_le16(IEEE80211_CHAN_CCK);
		} else {
			/* OFDM */
			tmp_16bit |= cpu_to_le16(IEEE80211_CHAN_OFDM);
		}
	} else {
		tmp_16bit |= cpu_to_le16(IEEE80211_CHAN_DYN);
	}
	memcpy(&hdr_buf[rt_len], &tmp_16bit, 2);
	rt_len += 2;

	/* dBm Antenna Signal */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
	hdr_buf[rt_len] = pattrib->phy_info.RecvSignalPower;
	rt_len += 1;

#if 0
	/* dBm Antenna Noise */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE);
	hdr_buf[rt_len] = 0;
	rt_len += 1;

	/* Signal Quality */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_LOCK_QUALITY);
	hdr_buf[rt_len] = pattrib->phy_info.SignalQuality;
	rt_len += 1;
#endif

	/* Antenna */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_ANTENNA);
	hdr_buf[rt_len] = 0; /* pHalData->rf_type; */
	rt_len += 1;

	/* RX flags */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_RX_FLAGS);
#if 0
	tmp_16bit = cpu_to_le16(0);
	memcpy(ptr, &tmp_16bit, 1);
#endif
	rt_len += 2;

	/* MCS information */
	if (pattrib->data_rate >= 12 && pattrib->data_rate < 44) {
		rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_MCS);
		/* known, flag */
		hdr_buf[rt_len] |= BIT1; /* MCS index known */

		/* bandwidth */
		hdr_buf[rt_len] |= BIT0;
		hdr_buf[rt_len+1] |= (pattrib->bw & 0x03);

		/* guard interval */
		hdr_buf[rt_len] |= BIT2;
		hdr_buf[rt_len+1] |= (pattrib->sgi & 0x01) << 2;

		/* STBC */
		hdr_buf[rt_len] |= BIT5;
		hdr_buf[rt_len+1] |= (pattrib->stbc & 0x03) << 5;

		rt_len += 2;

		/* MCS rate index */
		hdr_buf[rt_len] = data_rate[pattrib->data_rate];
		rt_len += 1;
	}

	/* VHT */
	if (pattrib->data_rate >= 44 && pattrib->data_rate < 84) {
		rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_VHT);

		/* known 16 bit, flag 8 bit */
		tmp_16bit = 0;

		/* Bandwidth */
		tmp_16bit |= BIT6;

		/* Group ID */
		tmp_16bit |= BIT7;

		/* Partial AID */
		tmp_16bit |= BIT8;

		/* STBC */
		tmp_16bit |= BIT0;
		hdr_buf[rt_len+2] |= (pattrib->stbc & 0x01);

		/* Guard interval */
		tmp_16bit |= BIT2;
		hdr_buf[rt_len+2] |= (pattrib->sgi & 0x01) << 2;

		/* LDPC extra OFDM symbol */
		tmp_16bit |= BIT4;
		hdr_buf[rt_len+2] |= (pattrib->ldpc & 0x01) << 4;

		memcpy(&hdr_buf[rt_len], &tmp_16bit, 2);
		rt_len += 3;

		/* bandwidth */
		if (pattrib->bw == 0) 
			hdr_buf[rt_len] |= 0;
		else if (pattrib->bw == 1) 
			hdr_buf[rt_len] |= 1;
		else if (pattrib->bw == 2) 
			hdr_buf[rt_len] |= 4;
		else if (pattrib->bw == 3) 
			hdr_buf[rt_len] |= 11;
		rt_len += 1;

		/* mcs_nss */
		if (pattrib->data_rate >= 44 && pattrib->data_rate < 54) {
			hdr_buf[rt_len] |= 1;
			hdr_buf[rt_len] |= data_rate[pattrib->data_rate] << 4;
		} else if (pattrib->data_rate >= 54 && pattrib->data_rate < 64) {
			hdr_buf[rt_len + 1] |= 2;
			hdr_buf[rt_len + 1] |= data_rate[pattrib->data_rate] << 4;
		} else if (pattrib->data_rate >= 64 && pattrib->data_rate < 74) {
			hdr_buf[rt_len + 2] |= 3;
			hdr_buf[rt_len + 2] |= data_rate[pattrib->data_rate] << 4;
		} else if (pattrib->data_rate >= 74 && pattrib->data_rate < 84) {
			hdr_buf[rt_len + 3] |= 4;
			hdr_buf[rt_len + 3] |= data_rate[pattrib->data_rate] << 4;
		}
		rt_len += 4;

		/* coding */
		hdr_buf[rt_len] = 0;
		rt_len += 1;

		/* group_id */
		hdr_buf[rt_len] = 0;
		rt_len += 1;

		/* partial_aid */
		tmp_16bit = 0;
		memcpy(&hdr_buf[rt_len], &tmp_16bit, 2);
		rt_len += 2;
	}

	/* push to skb */
	pskb = (_pkt *)buf;
	if (skb_headroom(pskb) < rt_len) {
		DBG_871X("%s:%d %s headroom is too small.\n", __FILE__, __LINE__, __func__);
		ret = _FAIL;
		return ret;
	}

	ptr = skb_push(pskb, rt_len);
	if (ptr) {
		rtap_hdr->it_len = cpu_to_le16(rt_len);
		memcpy(ptr, rtap_hdr, rt_len);
	} else {
		ret = _FAIL;
	}

	return ret;

}

int recv_frame_monitor(_adapter *padapter, union recv_frame *rframe)
{
	int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &rframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
	_pkt *pskb = NULL;

	/* read skb information from recv frame */
	pskb = rframe->u.hdr.pkt;
	pskb->len = rframe->u.hdr.len;
	pskb->data = rframe->u.hdr.rx_data;
	skb_set_tail_pointer(pskb, rframe->u.hdr.len);

	/* fill radiotap header */
	if (fill_radiotap_hdr(padapter, rframe, (u8 *)pskb) == _FAIL) {
		ret = _FAIL;
		rtw_free_recvframe(rframe, pfree_recv_queue); /* free this recv_frame */
		goto exit;
	}

	/* write skb information to recv frame */
	skb_reset_mac_header(pskb);
	rframe->u.hdr.len = pskb->len;
	rframe->u.hdr.rx_data = pskb->data;
	rframe->u.hdr.rx_head = pskb->head;
	rframe->u.hdr.rx_tail = skb_tail_pointer(pskb);
	rframe->u.hdr.rx_end = skb_end_pointer(pskb);

	if (!RTW_CANNOT_RUN(padapter)) {
		/* indicate this recv_frame */
		ret = rtw_recv_monitor(padapter, rframe);
		if (ret != _SUCCESS) {
			ret = _FAIL;
			rtw_free_recvframe(rframe, pfree_recv_queue); /* free this recv_frame */
			goto exit;
		}
	} else {
		ret = _FAIL;
		rtw_free_recvframe(rframe, pfree_recv_queue); /* free this recv_frame */
		goto exit;
	}

exit:
	return ret;
}

int recv_func_prehandle(_adapter *padapter, union recv_frame *rframe)
{
	int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &rframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

#ifdef DBG_RX_COUNTER_DUMP
	if( padapter->dump_rx_cnt_mode & DUMP_DRV_RX_COUNTER )
	{
		if (pattrib->crc_err == 1)
			padapter->drv_rx_cnt_crcerror++;
		else
			padapter->drv_rx_cnt_ok++;
	}
#endif

#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1 || padapter->mppriv.bRTWSmbCfg ==_TRUE)
	{
		mp_recv_frame(padapter,rframe);
		ret = _FAIL;
		goto exit;
        }
	else
#endif
	{
	//check the frame crtl field and decache
	ret = validate_recv_frame(padapter, rframe);
	if (ret != _SUCCESS)
	{
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_, ("recv_func: validate_recv_frame fail! drop pkt\n"));
		rtw_free_recvframe(rframe, pfree_recv_queue);//free this recv_frame
		goto exit;
	}
	}
exit:
	return ret;
}

int recv_func_posthandle(_adapter *padapter, union recv_frame *prframe)
{
	int ret = _SUCCESS;
	union recv_frame *orig_prframe = prframe;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
#ifdef CONFIG_TDLS
	u8 *psnap_type, *pcategory;
#endif //CONFIG_TDLS

	DBG_COUNTER(padapter->rx_logs.core_rx_post);

	// DATA FRAME
	rtw_led_control(padapter, LED_CTL_RX);

	prframe = decryptor(padapter, prframe);
	if (prframe == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("decryptor: drop pkt\n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s decryptor: drop pkt\n", __FUNCTION__);
		#endif
		ret = _FAIL;
		DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_err);
		goto _recv_data_drop;
	}

#if 0
	if ( padapter->adapter_type == PRIMARY_ADAPTER )
	{
		DBG_871X("+++\n");
		{
			int i;
			u8	*ptr = get_recvframe_data(prframe);
			for(i=0; i<140;i=i+8)
				DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:", *(ptr+i),
				*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));

		}
		DBG_871X("---\n");
	}
#endif

#ifdef CONFIG_TDLS
	//check TDLS frame
	psnap_type = get_recvframe_data(orig_prframe) + pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	pcategory = psnap_type + ETH_TYPE_LEN + PAYLOAD_TYPE_LEN;

	if((_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_TDLS, ETH_TYPE_LEN)) &&
		((*pcategory==RTW_WLAN_CATEGORY_TDLS) || (*pcategory==RTW_WLAN_CATEGORY_P2P))){
		ret = OnTDLS(padapter, prframe);
		if(ret == _FAIL)
			goto _exit_recv_func;
	}
#endif //CONFIG_TDLS

	prframe = recvframe_chk_defrag(padapter, prframe);
	if(prframe==NULL)	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvframe_chk_defrag: drop pkt\n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s recvframe_chk_defrag: drop pkt\n", __FUNCTION__);
		#endif
		DBG_COUNTER(padapter->rx_logs.core_rx_post_defrag_err);
		goto _recv_data_drop;		
	}

	prframe=portctrl(padapter, prframe);
	if (prframe == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("portctrl: drop pkt \n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s portctrl: drop pkt\n", __FUNCTION__);
		#endif
		ret = _FAIL;
		DBG_COUNTER(padapter->rx_logs.core_rx_post_portctrl_err);
		goto _recv_data_drop;
	}

	count_rx_stats(padapter, prframe, NULL);

#ifdef CONFIG_WAPI_SUPPORT
	rtw_wapi_update_info(padapter, prframe);
#endif

#ifdef CONFIG_80211N_HT
	ret = process_recv_indicatepkts(padapter, prframe);
	if (ret != _SUCCESS)
	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recv_func: process_recv_indicatepkts fail! \n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s process_recv_indicatepkts fail!\n", __FUNCTION__);
		#endif
		rtw_free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
		DBG_COUNTER(padapter->rx_logs.core_rx_post_indicate_err);
		goto _recv_data_drop;
	}
#else // CONFIG_80211N_HT
	if (!pattrib->amsdu)
	{
		ret = wlanhdr_to_ethhdr (prframe);
		if (ret != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("wlanhdr_to_ethhdr: drop pkt \n"));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s wlanhdr_to_ethhdr: drop pkt\n", __FUNCTION__);
			#endif
			rtw_free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
			goto _recv_data_drop;
		}

		if (!RTW_CANNOT_RUN(padapter)) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@ recv_func: recv_func rtw_recv_indicatepkt\n" ));
			//indicate this recv_frame
			ret = rtw_recv_indicatepkt(padapter, prframe);
			if (ret != _SUCCESS)
			{	
				#ifdef DBG_RX_DROP_FRAME
				DBG_871X("DBG_RX_DROP_FRAME %s rtw_recv_indicatepkt fail!\n", __FUNCTION__);
				#endif
				goto _recv_data_drop;
			}
		}
		else
		{
			RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@  recv_func: rtw_free_recvframe\n" ));
			RT_TRACE(_module_rtl871x_recv_c_, _drv_debug_, ("recv_func:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s ecv_func:bDriverStopped(%s) OR bSurpriseRemoved(%s)\n", __func__
				, rtw_is_drv_stopped(padapter)?"True":"False"
				, rtw_is_surprise_removed(padapter)?"True":"False");
			#endif
			ret = _FAIL;
			rtw_free_recvframe(orig_prframe, pfree_recv_queue); //free this recv_frame
		}

	}
	else if(pattrib->amsdu==1)
	{

		ret = amsdu_to_msdu(padapter, prframe);
		if(ret != _SUCCESS)
		{
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s amsdu_to_msdu fail\n", __FUNCTION__);
			#endif
			rtw_free_recvframe(orig_prframe, pfree_recv_queue);
			goto _recv_data_drop;
		}
	}
	else
	{
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s what is this condition??\n", __FUNCTION__);
		#endif
		goto _recv_data_drop;
	}
#endif // CONFIG_80211N_HT

_exit_recv_func:
	return ret;

_recv_data_drop:
	precvpriv->rx_drop++;
	return ret;
}


int recv_func(_adapter *padapter, union recv_frame *rframe);
int recv_func(_adapter *padapter, union recv_frame *rframe)
{
	int ret;
	struct rx_pkt_attrib *prxattrib = &rframe->u.hdr.attrib;
	struct recv_priv *recvpriv = &padapter->recvpriv;
	struct security_priv *psecuritypriv=&padapter->securitypriv;
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;

	if (check_fwstate(mlmepriv, WIFI_MONITOR_STATE)) {
		/* monitor mode */
		recv_frame_monitor(padapter, rframe);
		ret = _SUCCESS;
		goto exit;
	} else

	/* check if need to handle uc_swdec_pending_queue*/
	if (check_fwstate(mlmepriv, WIFI_STATION_STATE) && psecuritypriv->busetkipkey)
	{
		union recv_frame *pending_frame;
		int cnt = 0;

		while((pending_frame=rtw_alloc_recvframe(&padapter->recvpriv.uc_swdec_pending_queue))) {
			cnt++;
			DBG_COUNTER(padapter->rx_logs.core_rx_dequeue);
			recv_func_posthandle(padapter, pending_frame);
		}

		if (cnt)
			DBG_871X(FUNC_ADPT_FMT" dequeue %d from uc_swdec_pending_queue\n",
				FUNC_ADPT_ARG(padapter), cnt);
	}

	DBG_COUNTER(padapter->rx_logs.core_rx);
	ret = recv_func_prehandle(padapter, rframe);

	if(ret == _SUCCESS) {
		
		/* check if need to enqueue into uc_swdec_pending_queue*/
		if (check_fwstate(mlmepriv, WIFI_STATION_STATE) &&
			!IS_MCAST(prxattrib->ra) && prxattrib->encrypt>0 &&
			(prxattrib->bdecrypted == 0 ||psecuritypriv->sw_decrypt == _TRUE) &&
			psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPAPSK &&
			!psecuritypriv->busetkipkey)
		{
			DBG_COUNTER(padapter->rx_logs.core_rx_enqueue);
			rtw_enqueue_recvframe(rframe, &padapter->recvpriv.uc_swdec_pending_queue);
			//DBG_871X("%s: no key, enqueue uc_swdec_pending_queue\n", __func__);

			if (recvpriv->free_recvframe_cnt < NR_RECVFRAME/4) {
				/* to prevent from recvframe starvation, get recvframe from uc_swdec_pending_queue to free_recvframe_cnt  */
				rframe = rtw_alloc_recvframe(&padapter->recvpriv.uc_swdec_pending_queue);
				if (rframe)
					goto do_posthandle;
			}
			goto exit;
		}

do_posthandle:
		ret = recv_func_posthandle(padapter, rframe);
	}

exit:
	return ret;
}


s32 rtw_recv_entry(union recv_frame *precvframe)
{
	_adapter *padapter;
	struct recv_priv *precvpriv;
	s32 ret=_SUCCESS;

_func_enter_;

//	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("+rtw_recv_entry\n"));

	padapter = precvframe->u.hdr.adapter;

	precvpriv = &padapter->recvpriv;


	if ((ret = recv_func(padapter, precvframe)) == _FAIL)
	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("rtw_recv_entry: recv_func return fail!!!\n"));
		goto _recv_entry_drop;
	}


	precvpriv->rx_pkts++;

_func_exit_;

	return ret;

_recv_entry_drop:

#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1)
		padapter->mppriv.rx_pktloss = precvpriv->rx_drop;
#endif

	//RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("_recv_entry_drop\n"));

_func_exit_;

	return ret;
}

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
void rtw_signal_stat_timer_hdl(RTW_TIMER_HDL_ARGS){
	_adapter *adapter = (_adapter *)FunctionContext;
	struct recv_priv *recvpriv = &adapter->recvpriv;
	
	u32 tmp_s, tmp_q;
	u8 avg_signal_strength = 0;
	u8 avg_signal_qual = 0;
	u32 num_signal_strength = 0;
	u32 num_signal_qual = 0;
	u8 ratio_pre_stat = 0, ratio_curr_stat = 0, ratio_total = 0, ratio_profile = SIGNAL_STAT_CALC_PROFILE_0;

	if(adapter->recvpriv.is_signal_dbg) {
		//update the user specific value, signal_strength_dbg, to signal_strength, rssi
		adapter->recvpriv.signal_strength= adapter->recvpriv.signal_strength_dbg;
		adapter->recvpriv.rssi=(s8)translate_percentage_to_dbm((u8)adapter->recvpriv.signal_strength_dbg);
	} else {

		if(recvpriv->signal_strength_data.update_req == 0) {// update_req is clear, means we got rx
			avg_signal_strength = recvpriv->signal_strength_data.avg_val;
			num_signal_strength = recvpriv->signal_strength_data.total_num;
			// after avg_vals are accquired, we can re-stat the signal values
			recvpriv->signal_strength_data.update_req = 1;
		}
		
		if(recvpriv->signal_qual_data.update_req == 0) {// update_req is clear, means we got rx
			avg_signal_qual = recvpriv->signal_qual_data.avg_val;
			num_signal_qual = recvpriv->signal_qual_data.total_num;
			// after avg_vals are accquired, we can re-stat the signal values
			recvpriv->signal_qual_data.update_req = 1;
		}

		if (num_signal_strength == 0) {
			if (rtw_get_on_cur_ch_time(adapter) == 0
				|| rtw_get_passing_time_ms(rtw_get_on_cur_ch_time(adapter)) < 2 * adapter->mlmeextpriv.mlmext_info.bcn_interval
			) {
				goto set_timer;
			}
		}

		if(check_fwstate(&adapter->mlmepriv, _FW_UNDER_SURVEY) == _TRUE
			|| check_fwstate(&adapter->mlmepriv, _FW_LINKED) == _FALSE
		) { 
			goto set_timer;
		}

		#ifdef CONFIG_CONCURRENT_MODE
		if (check_buddy_fwstate(adapter, _FW_UNDER_SURVEY) == _TRUE)
			goto set_timer;
		#endif

		if (RTW_SIGNAL_STATE_CALC_PROFILE < SIGNAL_STAT_CALC_PROFILE_MAX)
			ratio_profile = RTW_SIGNAL_STATE_CALC_PROFILE;

		ratio_pre_stat = signal_stat_calc_profile[ratio_profile][0];
		ratio_curr_stat = signal_stat_calc_profile[ratio_profile][1];
		ratio_total = ratio_pre_stat + ratio_curr_stat;

		//update value of signal_strength, rssi, signal_qual
		tmp_s = (ratio_curr_stat * avg_signal_strength + ratio_pre_stat * recvpriv->signal_strength);
		if (tmp_s % ratio_total)
			tmp_s = tmp_s / ratio_total + 1;
		else
			tmp_s = tmp_s / ratio_total;
		if (tmp_s > 100)
			tmp_s = 100;

		tmp_q = (ratio_curr_stat * avg_signal_qual + ratio_pre_stat * recvpriv->signal_qual);
		if (tmp_q % ratio_total)
			tmp_q = tmp_q / ratio_total + 1;
		else
			tmp_q = tmp_q / ratio_total;
		if (tmp_q > 100)
			tmp_q = 100;

		recvpriv->signal_strength = tmp_s;
		recvpriv->rssi = (s8)translate_percentage_to_dbm(tmp_s);
		recvpriv->signal_qual = tmp_q;

		#if defined(DBG_RX_SIGNAL_DISPLAY_PROCESSING) && 1
		DBG_871X(FUNC_ADPT_FMT" signal_strength:%3u, rssi:%3d, signal_qual:%3u"
			", num_signal_strength:%u, num_signal_qual:%u"
			", on_cur_ch_ms:%d"
			"\n"
			, FUNC_ADPT_ARG(adapter)
			, recvpriv->signal_strength
			, recvpriv->rssi
			, recvpriv->signal_qual
			, num_signal_strength, num_signal_qual
			, rtw_get_on_cur_ch_time(adapter) ? rtw_get_passing_time_ms(rtw_get_on_cur_ch_time(adapter)) : 0
		);
		#endif
	}

set_timer:
	rtw_set_signal_stat_timer(recvpriv);
	
}
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

static void rx_process_rssi(_adapter *padapter,union recv_frame *prframe)
{
	u32	last_rssi, tmp_val;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	struct signal_stat * signal_stat = &padapter->recvpriv.signal_strength_data;
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

	//DBG_8192C("process_rssi=> pattrib->rssil(%d) signal_strength(%d)\n ",pattrib->RecvSignalPower,pattrib->signal_strength);
	//if(pRfd->Status.bPacketToSelf || pRfd->Status.bPacketBeacon)
	{
	#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
		if(signal_stat->update_req) {
			signal_stat->total_num = 0;
			signal_stat->total_val = 0;
			signal_stat->update_req = 0;
		}

		signal_stat->total_num++;
		signal_stat->total_val  += pattrib->phy_info.SignalStrength;
		signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;		
	#else //CONFIG_NEW_SIGNAL_STAT_PROCESS
	
		//Adapter->RxStats.RssiCalculateCnt++;	//For antenna Test
		if(padapter->recvpriv.signal_strength_data.total_num++ >= PHY_RSSI_SLID_WIN_MAX)
		{
			padapter->recvpriv.signal_strength_data.total_num = PHY_RSSI_SLID_WIN_MAX;
			last_rssi = padapter->recvpriv.signal_strength_data.elements[padapter->recvpriv.signal_strength_data.index];
			padapter->recvpriv.signal_strength_data.total_val -= last_rssi;
		}
		padapter->recvpriv.signal_strength_data.total_val  +=pattrib->phy_info.SignalStrength;

		padapter->recvpriv.signal_strength_data.elements[padapter->recvpriv.signal_strength_data.index++] = pattrib->phy_info.SignalStrength;
		if(padapter->recvpriv.signal_strength_data.index >= PHY_RSSI_SLID_WIN_MAX)
			padapter->recvpriv.signal_strength_data.index = 0;


		tmp_val = padapter->recvpriv.signal_strength_data.total_val/padapter->recvpriv.signal_strength_data.total_num;
		
		if(padapter->recvpriv.is_signal_dbg) {
			padapter->recvpriv.signal_strength= padapter->recvpriv.signal_strength_dbg;
			padapter->recvpriv.rssi=(s8)translate_percentage_to_dbm(padapter->recvpriv.signal_strength_dbg);
		} else {
			padapter->recvpriv.signal_strength= tmp_val;
			padapter->recvpriv.rssi=(s8)translate_percentage_to_dbm(tmp_val);
		}

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("UI RSSI = %d, ui_rssi.TotalVal = %d, ui_rssi.TotalNum = %d\n", tmp_val, padapter->recvpriv.signal_strength_data.total_val,padapter->recvpriv.signal_strength_data.total_num));
	#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS
	}
}

static void rx_process_link_qual(_adapter *padapter,union recv_frame *prframe)
{
	u32	last_evm=0, tmpVal;
 	struct rx_pkt_attrib *pattrib;
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	struct signal_stat * signal_stat;
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

	if(prframe == NULL || padapter==NULL){
		return;
	}

	pattrib = &prframe->u.hdr.attrib;
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	signal_stat = &padapter->recvpriv.signal_qual_data;
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

	//DBG_8192C("process_link_qual=> pattrib->signal_qual(%d)\n ",pattrib->signal_qual);

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	if(signal_stat->update_req) {
		signal_stat->total_num = 0;
		signal_stat->total_val = 0;
		signal_stat->update_req = 0;
	}

	signal_stat->total_num++;
	signal_stat->total_val  += pattrib->phy_info.SignalQuality;
	signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;
	
#else //CONFIG_NEW_SIGNAL_STAT_PROCESS
	if(pattrib->phy_info.SignalQuality != 0)
	{
			//
			// 1. Record the general EVM to the sliding window.
			//
			if(padapter->recvpriv.signal_qual_data.total_num++ >= PHY_LINKQUALITY_SLID_WIN_MAX)
			{
				padapter->recvpriv.signal_qual_data.total_num = PHY_LINKQUALITY_SLID_WIN_MAX;
				last_evm = padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index];
				padapter->recvpriv.signal_qual_data.total_val -= last_evm;
			}
			padapter->recvpriv.signal_qual_data.total_val += pattrib->phy_info.SignalQuality;

			padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index++] = pattrib->phy_info.SignalQuality;
			if(padapter->recvpriv.signal_qual_data.index >= PHY_LINKQUALITY_SLID_WIN_MAX)
				padapter->recvpriv.signal_qual_data.index = 0;

			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("Total SQ=%d  pattrib->signal_qual= %d\n", padapter->recvpriv.signal_qual_data.total_val, pattrib->phy_info.SignalQuality));

			// <1> Showed on UI for user, in percentage.
			tmpVal = padapter->recvpriv.signal_qual_data.total_val/padapter->recvpriv.signal_qual_data.total_num;
			padapter->recvpriv.signal_qual=(u8)tmpVal;

	}
	else
	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" pattrib->signal_qual =%d\n", pattrib->phy_info.SignalQuality));
	}
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS
}

void rx_process_phy_info(_adapter *padapter, union recv_frame *rframe)
{
	/* Check RSSI */
	rx_process_rssi(padapter, rframe);

	/* Check PWDB */
	//process_PWDB(padapter, rframe); 

	//UpdateRxSignalStatistics8192C(Adapter, pRfd);
	
	/* Check EVM */
	rx_process_link_qual(padapter, rframe);
	#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
	rtw_store_phy_info( padapter, rframe);
	#endif
}

void rx_query_phy_status(
	union recv_frame	*precvframe,
	u8 *pphy_status)
{
	PADAPTER 			padapter = precvframe->u.hdr.adapter;
	struct rx_pkt_attrib	*pattrib = &precvframe->u.hdr.attrib;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);	
	PODM_PHY_INFO_T 	pPHYInfo  = (PODM_PHY_INFO_T)(&pattrib->phy_info);
	u8					*wlanhdr;
	ODM_PACKET_INFO_T	pkt_info;
	u8 *sa;
	struct sta_priv *pstapriv;
	struct sta_info *psta = NULL;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	//_irqL		irqL;
	
	pkt_info.bPacketMatchBSSID =_FALSE;
	pkt_info.bPacketToSelf = _FALSE;
	pkt_info.bPacketBeacon = _FALSE;

	wlanhdr = get_recvframe_data(precvframe);

	pkt_info.bPacketMatchBSSID = (!IsFrameTypeCtrl(wlanhdr))
		&& (!pattrib->icv_err) && (!pattrib->crc_err)
		&& _rtw_memcmp(get_hdr_bssid(wlanhdr), get_bssid(&padapter->mlmepriv), ETH_ALEN);

	pkt_info.bToSelf = (!pattrib->icv_err) && (!pattrib->crc_err)
		&& _rtw_memcmp(get_ra(wlanhdr), adapter_mac_addr(padapter), ETH_ALEN);

	pkt_info.bPacketToSelf = pkt_info.bPacketMatchBSSID
		&& _rtw_memcmp(get_ra(wlanhdr), adapter_mac_addr(padapter), ETH_ALEN);

	pkt_info.bPacketBeacon = pkt_info.bPacketMatchBSSID
		&& (GetFrameSubType(wlanhdr) == WIFI_BEACON);

	sa = get_ta(wlanhdr);

	pkt_info.StationID = 0xFF;

	if (_rtw_memcmp(adapter_mac_addr(padapter), sa, ETH_ALEN) == _TRUE) {
		static u32 start_time = 0;

#if 0 /*For debug */
		if (IsFrameTypeCtrl(wlanhdr)) {
				DBG_871X("-->Control frame: Y\n");
				DBG_871X("-->pkt_len: %d\n", pattrib->pkt_len);
				DBG_871X("-->Sub Type = 0x%X\n", GetFrameSubType(wlanhdr));
		}

		/* Dump first 40 bytes of header */
		int i = 0;

		for (i = 0; i < 40; i++)
			DBG_871X("%d: %X\n", i, *((u8 *)wlanhdr + i));

		DBG_871X("\n");
#endif

		if ((start_time == 0) || (rtw_get_passing_time_ms(start_time) > 5000)) {
			DBG_871X_LEVEL(_drv_always_, "Warning!!! %s: Confilc mac addr!!\n", __func__);
			start_time = rtw_get_current_time();
		}
		pdbgpriv->dbg_rx_conflic_mac_addr_cnt++;
	} else{
		pstapriv = &padapter->stapriv;
		psta = rtw_get_stainfo(pstapriv, sa);
		if (psta)
			pkt_info.StationID = psta->mac_id;
	}

	pkt_info.DataRate = pattrib->data_rate;

	//_enter_critical_bh(&pHalData->odm_stainfo_lock, &irqL);
	ODM_PhyStatusQuery(&pHalData->odmpriv, pPHYInfo, pphy_status, &pkt_info);
	if (psta)
		psta->rssi = pattrib->phy_info.RecvSignalPower;
	//_exit_critical_bh(&pHalData->odm_stainfo_lock, &irqL);

	{
		precvframe->u.hdr.psta = NULL;
		if (pkt_info.bPacketMatchBSSID
			&& (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
		) {
			if (psta) {
				precvframe->u.hdr.psta = psta;
				rx_process_phy_info(padapter, precvframe);
			}
		} else if (pkt_info.bPacketToSelf || pkt_info.bPacketBeacon) {
			if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE) {
				if (psta)
					precvframe->u.hdr.psta = psta;
			}
			rx_process_phy_info(padapter, precvframe);
		}
	}
}
/*
* Increase and check if the continual_no_rx_packet of this @param pmlmepriv is larger than MAX_CONTINUAL_NORXPACKET_COUNT
* @return _TRUE:
* @return _FALSE:
*/
int rtw_inc_and_chk_continual_no_rx_packet(struct sta_info *sta, int tid_index)
{
	
	int ret = _FALSE;
	int value = ATOMIC_INC_RETURN(&sta->continual_no_rx_packet[tid_index]);

	if (value >= MAX_CONTINUAL_NORXPACKET_COUNT) 
		ret = _TRUE;
	
	return ret;
}

/*
* Set the continual_no_rx_packet of this @param pmlmepriv to 0
*/
void rtw_reset_continual_no_rx_packet(struct sta_info *sta, int tid_index)
{	
	ATOMIC_SET(&sta->continual_no_rx_packet[tid_index], 0);	
}
