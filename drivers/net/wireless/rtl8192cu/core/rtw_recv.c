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
#define _RTW_RECV_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <mlme_osdep.h>
#include <ip.h>
#include <if_ether.h>
#include <ethernet.h>

#ifdef CONFIG_USB_HCI
#include <usb_ops.h>
#endif

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

#include <wifi.h>
#include <circ_buf.h>

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
void rtw_signal_stat_timer_hdl(RTW_TIMER_HDL_ARGS);
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

	precvpriv->adapter = padapter;

	precvpriv->free_recvframe_cnt = NR_RECVFRAME;

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

		precvframe->u.hdr.adapter =padapter;
		precvframe++;

	}

#ifdef CONFIG_USB_HCI

	precvpriv->rx_pending_cnt=1;

	_rtw_init_sema(&precvpriv->allrxreturnevt, 0);

#endif

	res = padapter->HalFunc.init_recv_priv(padapter);

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	#ifdef PLATFORM_LINUX
	_init_timer(&precvpriv->signal_stat_timer, padapter->pnetdev, RTW_TIMER_HDL_NAME(signal_stat), padapter);
	#elif defined(PLATFORM_OS_CE) || defined(PLATFORM_WINDOWS)
	_init_timer(&precvpriv->signal_stat_timer, padapter->hndis_adapter, RTW_TIMER_HDL_NAME(signal_stat), padapter);
	#endif

	precvpriv->signal_stat_sampling_interval = 1000; //ms
	//precvpriv->signal_stat_converging_constant = 5000; //ms

	rtw_set_signal_stat_timer(precvpriv);
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS

exit:

_func_exit_;

	return res;

}

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

	rtw_mfree_recv_priv_lock(precvpriv);

	rtw_os_recv_resource_free(precvpriv);

	if(precvpriv->pallocated_frame_buf) {
		rtw_vmfree(precvpriv->pallocated_frame_buf, NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);
	}

	padapter->HalFunc.free_recv_priv(padapter);

_func_exit_;

}

union recv_frame *rtw_alloc_recvframe (_queue *pfree_recv_queue)
{
	_irqL irqL;
	union recv_frame  *precvframe;
	_list	*plist, *phead;
	_adapter *padapter;
	struct recv_priv *precvpriv;
_func_enter_;

	_enter_critical_bh(&pfree_recv_queue->lock, &irqL);

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

	_exit_critical_bh(&pfree_recv_queue->lock, &irqL);

_func_exit_;

	return precvframe;

}


void rtw_init_recvframe(union recv_frame *precvframe, struct recv_priv *precvpriv)
{
	struct recv_buf *precvbuf = precvframe->u.hdr.precvbuf;

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


#ifdef PLATFORM_WINDOWS
	rtw_os_read_port(padapter, precvframe->u.hdr.precvbuf);
#endif

#ifdef PLATFORM_LINUX

	if(precvframe->u.hdr.pkt)
	{
		dev_kfree_skb_any(precvframe->u.hdr.pkt);//free skb by driver
		precvframe->u.hdr.pkt = NULL;
	}

#ifdef CONFIG_SDIO_HCI
{
	_irqL irql;
	struct recv_buf *precvbuf=precvframe->u.hdr.precvbuf;
	if(precvbuf !=NULL){
		_enter_critical_bh(&precvbuf->recvbuf_lock, &irql);

		precvbuf->ref_cnt--;
		if(precvbuf->ref_cnt == 0 ){
			_enter_critical_bh(&precvpriv->free_recv_buf_queue.lock, &irqL);
			rtw_list_delete(&(precvbuf->list));
			rtw_list_insert_tail(&(precvbuf->list), get_list_head(&precvpriv->free_recv_buf_queue));
			precvpriv->free_recv_buf_queue_cnt++;
			_exit_critical_bh(&precvpriv->free_recv_buf_queue.lock, &irqL);
			RT_TRACE(_module_rtl871x_recv_c_,_drv_notice_,("rtw_os_read_port: precvbuf=0x%p enqueue:precvpriv->free_recv_buf_queue_cnt=%d\n",precvbuf,precvpriv->free_recv_buf_queue_cnt));
		}
		RT_TRACE(_module_rtl871x_recv_c_,_drv_notice_,("rtw_os_read_port: precvbuf=0x%p enqueue:precvpriv->free_recv_buf_queue_cnt=%d\n",precvbuf,precvpriv->free_recv_buf_queue_cnt));
		_exit_critical_bh(&precvbuf->recvbuf_lock, &irql);
	}
}
#endif
#endif

	_enter_critical_bh(&pfree_recv_queue->lock, &irqL);

	rtw_list_delete(&(precvframe->u.hdr.list));

	rtw_list_insert_tail(&(precvframe->u.hdr.list), get_list_head(pfree_recv_queue));

	if(padapter !=NULL){
		if(pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt++;
	}

      _exit_critical_bh(&pfree_recv_queue->lock, &irqL);

_func_exit_;

	return _SUCCESS;

}


union recv_frame *rtw_dequeue_recvframe (_queue *queue)
{
	return rtw_alloc_recvframe(queue);
}


sint rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{
	_irqL irqL;
	_adapter *padapter=precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

_func_enter_;


	//_spinlock(&pfree_recv_queue->lock);
	_enter_critical_bh(&queue->lock, &irqL);

	//_rtw_init_listhead(&(precvframe->u.hdr.list));
	rtw_list_delete(&(precvframe->u.hdr.list));


	rtw_list_insert_tail(&(precvframe->u.hdr.list), get_list_head(queue));

	if (padapter != NULL) {
		if (queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;
	}

	//_rtw_spinunlock(&pfree_recv_queue->lock);
	_exit_critical_bh(&queue->lock, &irqL);


_func_exit_;

	return _SUCCESS;
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

sint rtw_enqueue_recvbuf(struct recv_buf *precvbuf, _queue *queue)
{
	_irqL irqL;	

	_enter_critical(&queue->lock, &irqL);

	rtw_list_delete(&precvbuf->list);

	rtw_list_insert_tail(&precvbuf->list, get_list_head(queue));
	
	_exit_critical(&queue->lock, &irqL);


	return _SUCCESS;
	
}

struct recv_buf *rtw_dequeue_recvbuf (_queue *queue)
{
	_irqL irqL;
	struct recv_buf *precvbuf;
	_list	*plist, *phead;	

	_enter_critical(&queue->lock, &irqL);

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

	_exit_critical(&queue->lock, &irqL);


	return precvbuf;

}

static sint recvframe_chkmic(_adapter *adapter,  union recv_frame *precvframe){

	sint	i,res=_SUCCESS;
	u32	datalen;
	u8	miccode[8];
	u8	bmic_err=_FALSE,brpt_micerror = _TRUE;
	u8	*pframe, *payload,*pframemic;
	u8	*mickey,*iv,rxdata_key_idx;
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
				//DBG_8192C("\n recvframe_chkmic: bcmc key psecuritypriv->dot118021XGrpKeyid(%d),pmlmeinfo->key_index(%d) ,recv key_id(%d)\n",
				//								psecuritypriv->dot118021XGrpKeyid,pmlmeinfo->key_index,rxdata_key_idx);
				
				if(psecuritypriv->binstallGrpkey==_FALSE)
				{
					res=_FAIL;
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n recvframe_chkmic:didn't install group key!!!!!!!!!!\n"));
					DBG_8192C("\n recvframe_chkmic:didn't install group key!!!!!!!!!!\n");
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
					rtw_handle_tkip_mic_err(adapter,(u8)IS_MCAST(prxattrib->ra));
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" mic error :prxattrib->bdecrypted=%d ",prxattrib->bdecrypted));
					DBG_8192C(" mic error :prxattrib->bdecrypted=%d\n",prxattrib->bdecrypted);
				}
				else
				{
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" mic error :prxattrib->bdecrypted=%d ",prxattrib->bdecrypted));
					DBG_8192C(" mic error :prxattrib->bdecrypted=%d\n",prxattrib->bdecrypted);
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
static union recv_frame * decryptor(_adapter *padapter,union recv_frame *precv_frame)
{

	struct rx_pkt_attrib *prxattrib = &precv_frame->u.hdr.attrib;
	struct security_priv *psecuritypriv=&padapter->securitypriv;
	union recv_frame *return_packet=precv_frame;

_func_enter_;

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
		psecuritypriv->hw_decrypted=_FALSE;

		#ifdef DBG_RX_DECRYPTOR
		DBG_871X("prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n"
			, prxattrib->bdecrypted ,prxattrib->encrypt, psecuritypriv->hw_decrypted);
		#endif

		switch(prxattrib->encrypt){
		case _WEP40_:
		case _WEP104_:
			rtw_wep_decrypt(padapter, (u8 *)precv_frame);
			break;
		case _TKIP_:
			rtw_tkip_decrypt(padapter, (u8 *)precv_frame);
			break;
		case _AES_:
			rtw_aes_decrypt(padapter, (u8 * )precv_frame);
			break;
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
			psecuritypriv->hw_decrypted=_TRUE;
			#ifdef DBG_RX_DECRYPTOR
			DBG_871X("prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n"
			, prxattrib->bdecrypted ,prxattrib->encrypt, psecuritypriv->hw_decrypted);
			#endif

		}
	}
	else {
		#ifdef DBG_RX_DECRYPTOR
		DBG_871X("prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  psecuritypriv->hw_decrypted:%d\n"
		, prxattrib->bdecrypted ,prxattrib->encrypt, psecuritypriv->hw_decrypted);
		#endif
	}

	//recvframe_chkmic(adapter, precv_frame);   //move to recvframme_defrag function

_func_exit_;

	return return_packet;

}
//###set the security information in the recv_frame
static union recv_frame * portctrl(_adapter *adapter,union recv_frame * precv_frame)
{
	u8   *psta_addr,*ptr;
	uint  auth_alg;
	struct recv_frame_hdr *pfhdr;
	struct sta_info * psta;
	struct sta_priv *pstapriv ;
	union recv_frame * prtnframe;
	u16	ether_type=0;
	u16  eapol_type = 0x888e;//for Funia BD's WPA issue  
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;

_func_enter_;

	pstapriv = &adapter->stapriv;
	ptr = get_recvframe_data(precv_frame);
	pfhdr = &precv_frame->u.hdr;
	psta_addr = pfhdr->attrib.ta;
	psta = rtw_get_stainfo(pstapriv, psta_addr);

	auth_alg = adapter->securitypriv.dot11AuthAlgrthm;

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:adapter->securitypriv.dot11AuthAlgrthm= 0x%d\n",adapter->securitypriv.dot11AuthAlgrthm));

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

			if(pattrib->bdecrypted==0)
				RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("portctrl:prxstat->decrypted=%x\n", pattrib->bdecrypted));

			prtnframe=precv_frame;
			//check is the EAPOL frame or not (Rekey)
			if(ether_type == eapol_type){

				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("########portctrl:ether_type == 0x888e\n"));
				//check Rekey

				prtnframe=precv_frame;
			}
			else{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("########portctrl:ether_type = 0x%.4x\n",ether_type));
			}
		}
	}
	else
	{
		prtnframe=precv_frame;
	}

_func_exit_;

		return prtnframe;

}

static sint recv_decache(union recv_frame *precv_frame, u8 bretry, struct stainfo_rxcache *prxcache)
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

static void process_pwrbit_data(_adapter *padapter, union recv_frame *precv_frame)
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
			psta->state |= WIFI_SLEEP_STATE;
			pstapriv->sta_dz_bitmap |= BIT(psta->aid);
			//DBG_871X("to sleep, sta_dz_bitmap=%x\n", pstapriv->sta_dz_bitmap);
		}
		else
		{
			if(psta->state & WIFI_SLEEP_STATE)
			{
				psta->state ^= WIFI_SLEEP_STATE;

				pstapriv->sta_dz_bitmap &= ~BIT(psta->aid);
				
				//DBG_871X("to wakeup, sta_dz_bitmap=%x\n", pstapriv->sta_dz_bitmap);
				wakeup_sta_to_xmit(padapter, psta);

			}
		}

	}

#endif
}

static void process_wmmps_data(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE		
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta=NULL;

	psta = rtw_get_stainfo(pstapriv, pattrib->src);
	
	if(!psta) return;

	if(!psta->qos_option)
		return;

	if(!(psta->qos_info&0xf))
		return;
		

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
				issue_qos_nulldata(padapter, psta->hwaddr, (u16)pattrib->priority);
			}
		}
				
	}

	
#endif	

}

#ifdef CONFIG_TDLS
sint On_TDLS_Setup_Req(_adapter *adapter, union recv_frame *precv_frame)
{
	u8 *psa, *pmyid;
	struct sta_info *ptdls_sta= NULL;
   	struct sta_priv *pstapriv = &adapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);	
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	_irqL irqL;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *pdialog , *prsnie, *ppairwise_cipher;
	u8 i, k, pairwise_count;
	u8 ccmp_have=0, rsnie_have=0;
	u16 j;
	u8 SNonce[32];
	u32 *timeout_interval;
	sint parsing_length;	//frame body length, without icv_len
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE = 5;

	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(pstapriv, psa);
	
	pmyid=myid(&(adapter->eeprompriv));
	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE+1;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-TYPE_LENGTH_FIELD_SIZE
			-1
			-FIXED_IE;

	if(ptdls_sta==NULL ||(ptdls_sta->state&TDLS_LINKED_STATE)==TDLS_LINKED_STATE)
	{
		if(ptdls_sta==NULL){
			ptdls_sta = rtw_alloc_stainfo(pstapriv, psa);
		}else{
			//If the direct link is already set up
			//Process as re-setup after tear down		
			DBG_8192C("re-setup a direct link\n");
		}
		
		if(ptdls_sta) 
		{
			//copy dialog token
			pdialog=ptr+2;
			//rx_pkt_pattrib->frag_num is used to fill dialog token
			_rtw_memcpy(&(prx_pkt_attrib->frag_num), pdialog, 1);

			//parsing information element
			for(j=FIXED_IE; j<parsing_length;){
	
				pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

				switch (pIE->ElementID)
				{
					case _SUPPORTEDRATES_IE_:
						break;
					case _COUNTRY_IE_:
						break;
					case _EXT_SUPPORTEDRATES_IE_:
						break;
					case _SUPPORTED_CH_IE_:
						break;
					case _RSN_IE_2_:
						rsnie_have=1;
						if(prx_pkt_attrib->encrypt){
							prsnie=(u8*)pIE;
							//check whether initiator STA has CCMP pairwise_cipher.
							ppairwise_cipher=prsnie+10;
							_rtw_memcpy(&pairwise_count, (u16*)(ppairwise_cipher-2), 1);
							for(k=0;k<pairwise_count;k++){
								if(_rtw_memcmp( ppairwise_cipher+4*k, RSN_CIPHER_SUITE_CCMP, 4)==_TRUE)
									ccmp_have=1;
							}
							if(ccmp_have==0){
								//invalid contents of RSNIE
								ptdls_sta->stat_code=72;
							}
						}
						break;
					case _EXT_CAP_IE_:
						break;
					case _VENDOR_SPECIFIC_IE_:
						break;
					case _FTIE_:
						if(prx_pkt_attrib->encrypt)
							_rtw_memcpy(SNonce, (ptr+j+52), 32);
						break;
					case _TIMEOUT_ITVL_IE_:
						if(prx_pkt_attrib->encrypt)
							timeout_interval = (u32 *)(ptr+j+3);
						break;
					case _RIC_Descriptor_IE_:
						break;
					case _HT_CAPABILITY_IE_:
						break;
					case EID_BSSCoexistence:
						break;
					case _LINK_ID_IE_:
						break;
					default:
						break;
				}
	
				j += (pIE->Length + 2);
				
			}

			//check status code
			//if responder STA has/hasn't security on AP, but request hasn't/has RSNIE, it should reject
			if((rsnie_have && (prx_pkt_attrib->encrypt))||
			   (rsnie_have==0 && (prx_pkt_attrib->encrypt==0))){
				ptdls_sta->stat_code=0;
			}else if(rsnie_have && (prx_pkt_attrib->encrypt==0)){
				//security disabled
				ptdls_sta->stat_code=5;
			}else if(rsnie_have==0 && (prx_pkt_attrib->encrypt)){
				//request haven't RSNIE
				ptdls_sta->stat_code=38;
			}
			
			ptdls_sta->state|= TDLS_INITIATOR_STATE;
			ptdls_sta->aid=*(pdialog);
			if(prx_pkt_attrib->encrypt){
				_rtw_memcpy(ptdls_sta->SNonce, SNonce, 32);
				_rtw_memcpy(&(ptdls_sta->TDLS_PeerKey_Lifetime), timeout_interval, 4);
			}
			_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
			pmlmeinfo->tdls_sta_cnt++;
			_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
		}
		else
		{
			goto _exit_recv_tdls_frame;
		}
	}
	//already receiving TDLS setup request
	else if(ptdls_sta->state==TDLS_INITIATOR_STATE){
		DBG_8192C("receive duplicated TDLS setup request frame in handshaking\n");
		goto _exit_recv_tdls_frame;
	}
	//When receiving and sending setup_req to the same link at the same time, STA with higher MAC_addr would be initiator
	//following is to check out MAC_addr
	else if(ptdls_sta->state==TDLS_RESPONDER_STATE){
		DBG_8192C("receive setup_req after sending setUP_req\n");
		for (i=0;i<6;i++){
			if(*(pmyid+i)==*(psa+i)){
			}
			else if(*(pmyid+i)>*(psa+i)){
				goto _exit_recv_tdls_frame;
			}else if(*(pmyid+i)<*(psa+i)){
				ptdls_sta->state=TDLS_INITIATOR_STATE;
				ptdls_sta->aid=*(pdialog);
				break;
			}
		}
	}

	issue_tdls_setup_rsp(adapter, precv_frame);

_exit_recv_tdls_frame:
	
	return _FAIL;
}


sint On_TDLS_Setup_Rsp(_adapter *adapter, union recv_frame *precv_frame)
{
	struct sta_info *ptdls_sta= NULL;
   	struct sta_priv *pstapriv = &adapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	_irqL irqL;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *psa, *pdialog;
	u16 stat_code;
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	sint parsing_length;	//frame body length, without icv_len
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE =7;
	u8  *pftie, *ptimeout_ie, *plinkid_ie, *prsnie, *pftie_mic, *ppairwise_cipher;
	u16 pairwise_count, j, k;
	u8 verify_ccmp=0;
	
	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(pstapriv, psa);

	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE+1;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-TYPE_LENGTH_FIELD_SIZE
			-1
			-FIXED_IE;
	_rtw_memcpy(&stat_code, ptr+2, 2);

	pdialog=ptr+4;
	//rx_pkt_pattrib->frag_num is used to fill dialog token
	_rtw_memcpy(&(prx_pkt_attrib->frag_num), pdialog, 1);

	if(stat_code!=0){
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
		pmlmeinfo->tdls_sta_cnt--;
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		rtw_free_stainfo(adapter,  ptdls_sta);
		if(pmlmeinfo->tdls_sta_cnt==0)
			pmlmeinfo->tdls_setup_state=UN_TDLS_STATE;
		return _FAIL;
	}

	//parsing information element
	for(j=FIXED_IE; j<parsing_length;){

		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

		switch (pIE->ElementID)
		{
			case _SUPPORTEDRATES_IE_:
				break;
			case _COUNTRY_IE_:
				break;
			case _EXT_SUPPORTEDRATES_IE_:
				break;
			case _SUPPORTED_CH_IE_:
				break;
			case _RSN_IE_2_:
				prsnie=(u8*)pIE;
				//check whether responder STA has CCMP pairwise_cipher.
				ppairwise_cipher=prsnie+10;
				_rtw_memcpy(&pairwise_count, (u16*)(ppairwise_cipher-2), 2);
				for(k=0;k<pairwise_count;k++){
					if(_rtw_memcmp( ppairwise_cipher+4*k, RSN_CIPHER_SUITE_CCMP, 4)==_TRUE)
						verify_ccmp=1;
				}
			case _EXT_CAP_IE_:
				break;
			case _VENDOR_SPECIFIC_IE_:
				break;
			case _FTIE_:
				pftie=(u8*)pIE;
				_rtw_memcpy(ptdls_sta->ANonce, (ptr+j+20), 32);
				break;
			case _TIMEOUT_ITVL_IE_:
				ptimeout_ie=(u8*)pIE;
				break;
			case _RIC_Descriptor_IE_:
				break;
			case _HT_CAPABILITY_IE_:
				break;
			case EID_BSSCoexistence:
				break;
			case _LINK_ID_IE_:
				plinkid_ie=(u8*)pIE;
				break;
			default:
				break;
		}

		j += (pIE->Length + 2);
		
	}

	if(prx_pkt_attrib->encrypt){
		if(verify_ccmp==1){
			wpa_tdls_generate_tpk(adapter, ptdls_sta);
			ptdls_sta->stat_code=0;
		}
		else{
			ptdls_sta->stat_code=72;	//invalide contents of RSNIE
		}
	}else{
		ptdls_sta->stat_code=0;
	}

	if(prx_pkt_attrib->encrypt){
		if(tdls_verify_mic(ptdls_sta->tpk.kck, 2, plinkid_ie, prsnie, ptimeout_ie, pftie)==0){	//0: Invalid, 1: valid
			_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
			pmlmeinfo->tdls_sta_cnt--;
			rtw_free_stainfo(adapter,  ptdls_sta);
			_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
			if(pmlmeinfo->tdls_sta_cnt==0)
				pmlmeinfo->tdls_setup_state=UN_TDLS_STATE;
			return _FAIL;
		}
	}
	DBG_8192C("issue_tdls_setup_cfm\n");
	issue_tdls_setup_cfm(adapter, precv_frame);

	if((ptdls_sta->state&TDLS_RESPONDER_STATE)==TDLS_RESPONDER_STATE)
		ptdls_sta->state |= TDLS_LINKED_STATE;

	if(prx_pkt_attrib->encrypt){
		if(ptdls_sta->cam_entry==0){
			ptdls_sta->dot118021XPrivacy=_AES_;
			ptdls_sta->cam_entry=pmlmeinfo->tdls_cam_entry_to_write;
			if(++pmlmeinfo->tdls_cam_entry_to_write>31)
				pmlmeinfo->tdls_cam_entry_to_write=6;
		}
		rtw_setstakey_cmd(adapter, (u8*)ptdls_sta, _TRUE);
	}
	
	return _FAIL;
}

sint On_TDLS_Setup_Cfm(_adapter *adapter, union recv_frame *precv_frame)
{
	struct sta_info *ptdls_sta= NULL;
   	struct sta_priv *pstapriv = &adapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	_irqL irqL;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *psa; 
	u16 stat_code;
	sint parsing_length;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE =5;
	u8  *pftie, *ptimeout_ie, *plinkid_ie, *prsnie, *pftie_mic, *ppairwise_cipher;
	u16 j, pairwise_count;

	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(pstapriv, psa);

	//[+1]: payload type
	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE+1;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-TYPE_LENGTH_FIELD_SIZE
			-1
			-FIXED_IE;
	_rtw_memcpy(&stat_code, ptr+2, 2);

	if(stat_code!=0){
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
		pmlmeinfo->tdls_sta_cnt--;
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		rtw_free_stainfo(adapter,  ptdls_sta);
		if(pmlmeinfo->tdls_sta_cnt==0)
			pmlmeinfo->tdls_setup_state=UN_TDLS_STATE;
		return _FAIL;
	}

	if(prx_pkt_attrib->encrypt){
		//parsing information element
		for(j=FIXED_IE; j<parsing_length;){

			pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

			switch (pIE->ElementID)
			{
				case _RSN_IE_2_:
					prsnie=(u8*)pIE;
					break;
				case _VENDOR_SPECIFIC_IE_:
					break;
	 			case _FTIE_:
					pftie=(u8*)pIE;
					break;
				case _TIMEOUT_ITVL_IE_:
					ptimeout_ie=(u8*)pIE;
					break;
				case _HT_EXTRA_INFO_IE_:
					break;
	 			case _LINK_ID_IE_:
					plinkid_ie=(u8*)pIE;
					break;
				default:
					break;
			}

			j += (pIE->Length + 2);
			
		}

		//verify mic in FTIE MIC field
		if(tdls_verify_mic(ptdls_sta->tpk.kck, 3, plinkid_ie, prsnie, ptimeout_ie, pftie)==0){	//0: Invalid, 1: Valid
			_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
			pmlmeinfo->tdls_sta_cnt--;
			rtw_free_stainfo(adapter,  ptdls_sta);
			_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
			if(pmlmeinfo->tdls_sta_cnt==0)
				pmlmeinfo->tdls_setup_state=UN_TDLS_STATE;
			return _FAIL;
		}

	}

	pmlmeinfo->tdls_setup_state=TDLS_LINKED_STATE;
	if((ptdls_sta->state & TDLS_INITIATOR_STATE)==TDLS_INITIATOR_STATE)
		ptdls_sta->state|=TDLS_LINKED_STATE;

	ptdls_sta->option=1;	//write RCR DATA BIT
	_set_workitem(&ptdls_sta->option_workitem);

	//Write cam
	//TDLS encryption(if needed) will always be CCMP
	if(prx_pkt_attrib->encrypt){
		if(ptdls_sta->cam_entry==0){
			ptdls_sta->dot118021XPrivacy=_AES_;
			ptdls_sta->cam_entry=pmlmeinfo->tdls_cam_entry_to_write;
			if(++pmlmeinfo->tdls_cam_entry_to_write>31)
				pmlmeinfo->tdls_cam_entry_to_write=6;
		}
		rtw_setstakey_cmd(adapter, (u8*)ptdls_sta, _TRUE);
	}


	return _FAIL;
}

sint On_TDLS_Dis_Req(_adapter *adapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	u8 *pdialog = NULL;

	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+8+1;
	pdialog=ptr+2;

	//check frame contents

	//rx_pkt_pattrib->frag_num is used to fill dialog token
	_rtw_memcpy(&(prx_pkt_attrib->frag_num), pdialog, 1);

	issue_tdls_dis_rsp(adapter, precv_frame);

	return _FAIL;
	
}

sint On_TDLS_Teardown(_adapter *adapter, union recv_frame *precv_frame)
{
	u8 *psa;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);	
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv 	*pstapriv = &adapter->stapriv;
	struct sta_info *ptdls_sta= NULL;
	_irqL irqL;

	psa = get_sa(ptr);

	ptdls_sta = rtw_get_stainfo(pstapriv, psa);
	if(ptdls_sta!=NULL){

		if(ptdls_sta->state & TDLS_CH_SWITCH_ON_STATE){
			ptdls_sta->option =3;
			_set_workitem(&ptdls_sta->option_workitem);
		}
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
		pmlmeinfo->tdls_sta_cnt--;
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		//ready to clear cam
		if(ptdls_sta->cam_entry!=0){
			pmlmeinfo->tdls_cam_entry_to_clear=ptdls_sta->cam_entry;
			//it will clear cam response to ptdls_sta->cam_entry
			rtw_setstakey_cmd(adapter, (u8 *)ptdls_sta, _TRUE);
		}
		_set_workitem(&pmlmeext->TDLS_restore_workitem);
		rtw_free_stainfo(adapter,  ptdls_sta);
	}
	if(pmlmeinfo->tdls_sta_cnt==0)
		pmlmeinfo->tdls_setup_state=UN_TDLS_STATE;
		
	return _FAIL;
	
}

u8 TDLS_check_ch_state(uint state){
	if(	(state & TDLS_CH_SWITCH_ON_STATE) &&
		(state & TDLS_AT_OFF_CH_STATE) &&
		(state & TDLS_PEER_AT_OFF_STATE) ){

		if(state & TDLS_PEER_SLEEP_STATE)
			return 2;	//U-APSD + ch. switch
		else
			return 1;	//ch. switch
	}else
		return 0;
}

//we process buffered data for 1. U-APSD, 2. ch. switch, 3. U-APSD + ch. switch here
sint On_TDLS_Peer_Traffic_Rsp(_adapter *adapter, union recv_frame *precv_frame)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct rx_pkt_attrib	*pattrib = & precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &adapter->stapriv;
	//get peer sta infomation
	struct sta_info *ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->src);
	u8 wmmps_ac=0, state=TDLS_check_ch_state(ptdls_sta->state);
	int i;
	ptdls_sta->sta_stats.rx_pkts++;

	//receive peer traffic response frame, sleeping STA wakes up
	ptdls_sta->state &= ~(TDLS_PEER_SLEEP_STATE);

	// if noticed peer STA wakes up by receiving peer traffic response
	// and we want to do channel swtiching, then we will transmit channel switch request first
	if(ptdls_sta->state & TDLS_APSD_CHSW_STATE){
		issue_tdls_ch_switch_req(adapter, pattrib->src);
		ptdls_sta->state &= ~(TDLS_APSD_CHSW_STATE);
		return  _FAIL;
	}

	//check 4-AC queue bit
	if(ptdls_sta->uapsd_vo || ptdls_sta->uapsd_vi || ptdls_sta->uapsd_be || ptdls_sta->uapsd_bk)
		wmmps_ac=1;

	//if it's a direct link and have buffered frame
	if(ptdls_sta->state & TDLS_LINKED_STATE){
		if(wmmps_ac && state)
		{
			_irqL irqL;	 
			_list	*xmitframe_plist, *xmitframe_phead;
			struct xmit_frame *pxmitframe=NULL;
		
			_enter_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);	

			xmitframe_phead = get_list_head(&ptdls_sta->sleep_q);
			xmitframe_plist = get_next(xmitframe_phead);

			//transmit buffered frames
			while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE)
			{			
				pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);
				xmitframe_plist = get_next(xmitframe_plist);
				rtw_list_delete(&pxmitframe->list);

				ptdls_sta->sleepq_len--;
				if(ptdls_sta->sleepq_len>0){
					pxmitframe->attrib.mdata = 1;
					pxmitframe->attrib.eosp = 0;
				}else{
					pxmitframe->attrib.mdata = 0;
					pxmitframe->attrib.eosp = 1;
				}
				//pxmitframe->attrib.triggered = 1;	//maybe doesn't need in TDLS
				if(adapter->HalFunc.hal_xmit(adapter, pxmitframe) == _TRUE)
				{		
					rtw_os_xmit_complete(adapter, pxmitframe);
				}

			}

			if(ptdls_sta->sleepq_len==0)
			{
				DBG_871X("no buffered packets to xmit\n");
				//on U-APSD + CH. switch state, when there is no buffered date to xmit,
				// we should go back to base channel
				if(state==2){
					ptdls_sta->option = 3;
					_set_workitem(&ptdls_sta->option_workitem);
				}else if(ptdls_sta->state&TDLS_SW_OFF_STATE){
						ptdls_sta->state &= ~(TDLS_SW_OFF_STATE);
						pmlmeinfo->tdls_candidate_ch= pmlmeext->cur_channel;
						issue_tdls_ch_switch_req(adapter, pattrib->src);
						DBG_8192C("issue tdls ch switch req back to base channel\n");
				}
				
			}
			else
			{
				DBG_871X("error!psta->sleepq_len=%d\n", ptdls_sta->sleepq_len);
				ptdls_sta->sleepq_len=0;						
			}

			_exit_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);			
		
		}

	}

	return _FAIL;
}

sint On_TDLS_Ch_Switch_Req(_adapter *adapter, union recv_frame *precv_frame)
{
	struct sta_info *ptdls_sta= NULL;
   	struct sta_priv *pstapriv = &adapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *psa; 
	sint parsing_length;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE =3;
	u16 j;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(pstapriv, psa);
	
	//[+1]: payload type
	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE+1;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-TYPE_LENGTH_FIELD_SIZE
			-1
			-FIXED_IE;

	ptdls_sta->off_ch = *(ptr+2);
	
	//parsing information element
	for(j=FIXED_IE; j<parsing_length;){

		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

		switch (pIE->ElementID)
		{
			case _COUNTRY_IE_:
				break;
			case _CH_SWTICH_ANNOUNCE_:
				break;
 			case _LINK_ID_IE_:
				break;
			case _CH_SWITCH_TIMING_:
				_rtw_memcpy(&ptdls_sta->ch_switch_time, pIE->data, 2);
				_rtw_memcpy(&ptdls_sta->ch_switch_timeout, pIE->data+2, 2);
			default:
				break;
		}

		j += (pIE->Length + 2);
		
	}

	//todo: check status
	ptdls_sta->stat_code=0;
	ptdls_sta->state|=TDLS_CH_SWITCH_ON_STATE;

	issue_nulldata(adapter, 1);

	issue_tdls_ch_switch_rsp(adapter, psa);

	DBG_8192C("issue tdls channel switch response\n");

	if((ptdls_sta->state & TDLS_CH_SWITCH_ON_STATE) && ptdls_sta->off_ch==pmlmeext->cur_channel){
		DBG_8192C("back to base channel\n");
		ptdls_sta->option=7;
		_set_workitem(&ptdls_sta->option_workitem);
		
	}else{		
		ptdls_sta->option=6;
		_set_workitem(&ptdls_sta->option_workitem);
	}
	return _FAIL;
}

sint On_TDLS_Ch_Switch_Rsp(_adapter *adapter, union recv_frame *precv_frame)
{
	struct sta_info *ptdls_sta= NULL;
   	struct sta_priv *pstapriv = &adapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *psa; 
	sint parsing_length;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE =4;
	u16 stat_code, j, switch_time, switch_timeout;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(pstapriv, psa);

	//if channel switch is running and receiving Unsolicited TDLS Channel Switch Response,
	//it will go back to base channel and terminate this channel switch procedure
	if(ptdls_sta->state & TDLS_CH_SWITCH_ON_STATE ){
		if(pmlmeext->cur_channel==ptdls_sta->off_ch){
			DBG_8192C("back to base channel\n");
			ptdls_sta->option=7;
			_set_workitem(&ptdls_sta->option_workitem);
		}else{
			DBG_8192C("receive unsolicited channel switch response \n");
			ptdls_sta->option=3;
			_set_workitem(&ptdls_sta->option_workitem);
		}
		return _FAIL;
	}

	//avoiding duplicated or unconditional ch. switch. rsp
	if((ptdls_sta->state & TDLS_CH_SW_INITIATOR_STATE) != TDLS_CH_SW_INITIATOR_STATE)
		return _FAIL;
	
	//[+1]: payload type
	ptr +=prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len+LLC_HEADER_SIZE+TYPE_LENGTH_FIELD_SIZE+1;
	parsing_length= ((union recv_frame *)precv_frame)->u.hdr.len
			-prx_pkt_attrib->hdrlen
			-prx_pkt_attrib->iv_len
			-prx_pkt_attrib->icv_len
			-LLC_HEADER_SIZE
			-TYPE_LENGTH_FIELD_SIZE
			-1
			-FIXED_IE;

	_rtw_memcpy(&stat_code, ptr+2, 2);

	if(stat_code!=0){
		return _FAIL;
	}
	
	//parsing information element
	for(j=FIXED_IE; j<parsing_length;){

		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr+ j);

		switch (pIE->ElementID)
		{
 			case _LINK_ID_IE_:
				break;
			case _CH_SWITCH_TIMING_:
				_rtw_memcpy(&switch_time, pIE->data, 2);
				if(switch_time > ptdls_sta->ch_switch_time)
					_rtw_memcpy(&ptdls_sta->ch_switch_time, &switch_time, 2);

				_rtw_memcpy(&switch_timeout, pIE->data+2, 2);
				if(switch_timeout > ptdls_sta->ch_switch_timeout)
					_rtw_memcpy(&ptdls_sta->ch_switch_timeout, &switch_timeout, 2);

			default:
				break;
		}

		j += (pIE->Length + 2);
		
	}

	ptdls_sta->state &= ~(TDLS_CH_SW_INITIATOR_STATE);
	ptdls_sta->state |=TDLS_CH_SWITCH_ON_STATE;

	//goto set_channel_workitem_callback()
	ptdls_sta->option=6;
	_set_workitem(&ptdls_sta->option_workitem);

	return _FAIL;	
}

sint OnTDLS(_adapter *adapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib	*pattrib = & precv_frame->u.hdr.attrib;
	sint ret = _SUCCESS;
	u8 *paction = get_recvframe_data(precv_frame);

	//point to action field, [+8]: snap+ether_type, [+1]: payload_type, [+1]: category field
	paction+=pattrib->hdrlen + pattrib->iv_len+8+1+1;	
	
	switch(*paction){
		case TDLS_SETUP_REQUEST:
			DBG_8192C("recv tdls setup request frame\n");
			ret=On_TDLS_Setup_Req(adapter, precv_frame);
			break;
		case TDLS_SETUP_RESPONSE:
			DBG_8192C("recv tdls setup response frame\n");			
			ret=On_TDLS_Setup_Rsp(adapter, precv_frame);
			break;
		case TDLS_SETUP_CONFIRM:
			DBG_8192C("recv tdls setup confirm frame\n");
			ret=On_TDLS_Setup_Cfm(adapter, precv_frame);
			break;
		case TDLS_TEARDOWN:
			DBG_8192C("recv tdls teardown, free sta_info\n");
			ret=On_TDLS_Teardown(adapter, precv_frame);
			break;
		case TDLS_DISCOVERY_REQUEST:
			DBG_8192C("recv tdls discovery request frame\n");
			ret=On_TDLS_Dis_Req(adapter, precv_frame);
			break;
		case TDLS_PEER_TRAFFIC_RESPONSE:
			DBG_8192C("recv tdls discovery response frame\n");
			ret=On_TDLS_Peer_Traffic_Rsp(adapter, precv_frame);
			break;
		case TDLS_CHANNEL_SWITCH_REQUEST:
			DBG_8192C("recv tdls channel switch request frame\n");
			ret=On_TDLS_Ch_Switch_Req(adapter, precv_frame);
			break;
		case TDLS_CHANNEL_SWITCH_RESPONSE:
			DBG_8192C("recv tdls channel switch response frame\n");
			ret=On_TDLS_Ch_Switch_Rsp(adapter, precv_frame);
			break;
		default:
			DBG_8192C("receive TDLS frame but not supported\n");
			ret=_FAIL;
			break;
	}

exit:
	return ret;
	
}
#endif

static sint sta2sta_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta
)
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	sint ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	struct	sta_priv 		*pstapriv = &adapter->stapriv;
	struct	security_priv	*psecuritypriv = &adapter->securitypriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = myid(&adapter->eeprompriv);
	u8 * sta_addr = NULL;
	sint bmcast = IS_MCAST(pattrib->dst);

#ifdef CONFIG_TDLS	
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);	
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info *ptdls_sta=NULL;
	u8 *psnap_type=ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	//frame body located after [+2]: ether-type, [+1]: payload type
	u8 *pframe_body = psnap_type+2+1;
#endif

_func_enter_;

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

		//direct link data transfer
		if(pmlmeinfo->tdls_setup_state==TDLS_LINKED_STATE){
			ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->src);
			if(ptdls_sta==NULL){
				ret=_FAIL;
				goto exit;
			}else if(((ptdls_sta->state&TDLS_LINKED_STATE)!=TDLS_LINKED_STATE)&&(!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))&& (!bmcast)){
				ret=_FAIL;
				goto exit;
			}else if((ptdls_sta->state&TDLS_LINKED_STATE)==TDLS_LINKED_STATE){

				//drop QoS-SubType Data, including QoS NULL, excluding QoS-Data
				if( (GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE )== WIFI_QOS_DATA_TYPE)
				{
					if(GetFrameSubType(ptr)&(BIT(4)|BIT(5)|BIT(6)))
					{
					ret= _FAIL;
					goto exit;
					}
				}
				// filter packets that SA is myself or multicast or broadcast
				if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" SA==myself \n"));
					ret= _FAIL;
					goto exit;
				}
				// da should be for me
				if((!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))&& (!bmcast))
				{
					ret= _FAIL;
					goto exit;
				}
				// check BSSID
				if( _rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
				     _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
				     (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) )
				{
					ret= _FAIL;
					goto exit;
				}
				// if NULL-frame, check pwrbit
				if ((GetFrameSubType(ptr)) == WIFI_DATA_NULL)
				{
					//NULL-frame with pwrbit=1, buffer_STA should buffer frames for sleep_STA
					if(GetPwrMgt(ptr)){
						ptdls_sta->state|=TDLS_PEER_SLEEP_STATE;
					// it would be triggered when we are off channel and receiving NULL DATA
					// we can confirm that peer STA is at off channel
					}else if(ptdls_sta->state&TDLS_CH_SWITCH_ON_STATE){
						if((ptdls_sta->state & TDLS_PEER_AT_OFF_STATE) != TDLS_PEER_AT_OFF_STATE){
							issue_nulldata_to_TDLS_peer_STA(adapter, ptdls_sta, 0);
							ptdls_sta->state |= TDLS_PEER_AT_OFF_STATE;
							On_TDLS_Peer_Traffic_Rsp(adapter, precv_frame);
						}
					}
					RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,(" NULL frame \n"));
					ret= _FAIL;
					goto exit;
				}
				//receive some of all TDLS management frames, process it at ON_TDLS
				if((_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_TDLS, 2))){
					ret= OnTDLS(adapter, precv_frame);
					goto exit;
				}
				
			}
		}		
		else
#endif
		// For Station mode, sa and bssid should always be BSSID, and DA is my mac-address
		if(!_rtw_memcmp(pattrib->bssid, pattrib->src, ETH_ALEN) )
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("bssid != TA under STATION_MODE; drop pkt\n"));
			ret= _FAIL;
			goto exit;
		}

		sta_addr = pattrib->bssid;

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

	if (*psta == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("can't get psta under sta2sta_data_frame ; drop pkt\n"));
#ifdef CONFIG_MP_INCLUDED
		if(check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
		adapter->mppriv.rx_pktloss++;
#endif
		ret= _FAIL;
		goto exit;
	}

exit:
_func_exit_;
	return ret;

}


static sint ap2sta_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta )
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	sint ret = _SUCCESS;
	struct	sta_priv 		*pstapriv = &adapter->stapriv;
	struct	security_priv	*psecuritypriv = &adapter->securitypriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = myid(&adapter->eeprompriv);
	sint bmcast = IS_MCAST(pattrib->dst);

_func_enter_;

	if ((check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
		&& (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE 
			|| check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE	)
		)
	{

		// if NULL-frame, drop packet
		if ((GetFrameSubType(ptr)) == WIFI_DATA_NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,(" NULL frame \n"));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s NULL frame\n", __FUNCTION__);
			#endif
			ret= _FAIL;
			goto exit;
		}

		//drop QoS-SubType Data, including QoS NULL, excluding QoS-Data
		if( (GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE )== WIFI_QOS_DATA_TYPE)
		{
			if(GetFrameSubType(ptr)&(BIT(4)|BIT(5)|BIT(6)))
			{
				#ifdef DBG_RX_DROP_FRAME
				DBG_871X("DBG_RX_DROP_FRAME %s drop QoS-SubType Data, including QoS NULL, excluding QoS-Data\n", __FUNCTION__);
				#endif
				ret= _FAIL;
				goto exit;
			}

		}

		// filter packets that SA is myself or multicast or broadcast
		if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" SA==myself \n"));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s SA=%x:%x:%x:%x:%x:%x, myhwaddr= %x:%x:%x:%x:%x:%x\n", __FUNCTION__,
				pattrib->src[0], pattrib->src[1], pattrib->src[2],
				pattrib->src[3], pattrib->src[4], pattrib->src[5],
				*(myhwaddr), *(myhwaddr+1), *(myhwaddr+2),
				*(myhwaddr+3), *(myhwaddr+4), *(myhwaddr+5));
			#endif			
			ret= _FAIL;
			goto exit;
		}

		// da should be for me
		if((!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))&& (!bmcast))
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,(" ap2sta_data_frame:  compare DA fail; DA= %x:%x:%x:%x:%x:%x \n",
					pattrib->dst[0],
					pattrib->dst[1],
					pattrib->dst[2],
					pattrib->dst[3],
					pattrib->dst[4],
					pattrib->dst[5]));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s compare DA fail; DA= %x:%x:%x:%x:%x:%x \n", __FUNCTION__,
					pattrib->dst[0],pattrib->dst[1],pattrib->dst[2],
					pattrib->dst[3],pattrib->dst[4],pattrib->dst[5]);
			#endif

			ret= _FAIL;
			goto exit;
		}


		// check BSSID
		if( _rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		     _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		     (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) )
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,(" ap2sta_data_frame:  compare BSSID fail ; BSSID=%x:%x:%x:%x:%x:%x\n",
				pattrib->bssid[0],
				pattrib->bssid[1],
				pattrib->bssid[2],
				pattrib->bssid[3],
				pattrib->bssid[4],
				pattrib->bssid[5]));

			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("mybssid= %x:%x:%x:%x:%x:%x\n",
				mybssid[0],
				mybssid[1],
				mybssid[2],
				mybssid[3],
				mybssid[4],
				mybssid[5]));

			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s compare BSSID fail ; BSSID=%x:%x:%x:%x:%x:%x, mybssid= %x:%x:%x:%x:%x:%x\n", __FUNCTION__,
				pattrib->bssid[0], pattrib->bssid[1], pattrib->bssid[2],
				pattrib->bssid[3], pattrib->bssid[4], pattrib->bssid[5],
				mybssid[0], mybssid[1], mybssid[2],
				mybssid[3], mybssid[4], mybssid[5]);
			#endif

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
	else
	{
		ret = _FAIL;
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s fw_state:0x%x\n", __FUNCTION__, get_fwstate(pmlmepriv));
		#endif
	}

exit:

_func_exit_;

	return ret;

}

static sint sta2ap_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta )
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	struct	sta_priv 		*pstapriv = &adapter->stapriv;
	struct	security_priv	*psecuritypriv = &adapter->securitypriv;
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
				ret= _FAIL;
				goto exit;
			}


			process_pwrbit_data(adapter, precv_frame);
			

			// if NULL-frame, drop packet
			if ((GetFrameSubType(ptr)) == WIFI_DATA_NULL)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,(" NULL frame \n"));

				//process_null_data(adapter, precv_frame);
				//process_pwrbit_data(adapter, precv_frame);
				

				ret= _FAIL;
				goto exit;
			}

			//drop QoS-SubType Data, including QoS NULL, excluding QoS-Data
			if( (GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE )== WIFI_QOS_DATA_TYPE)
			{

				if(GetFrameSubType(ptr)==WIFI_QOS_DATA_NULL)
				{
					RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,(" QoS NULL frame \n"));

					//process_null_data(adapter, precv_frame);

					ret= _FAIL;
					goto exit;
				}
			
				process_wmmps_data(adapter, precv_frame);
			
			/*
				if(GetFrameSubType(ptr)&(BIT(4)|BIT(5)|BIT(6)))
				{
					process_null_data(adapter, precv_frame);
					ret= _FAIL;
					goto exit;
				}
			*/
			}

	}

exit:

_func_exit_;

	return ret;

}

static sint validate_recv_ctrl_frame(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	//uint len = precv_frame->u.hdr.len;
		
	//DBG_871X("+validate_recv_ctrl_frame\n");

	if (GetFrameType(pframe) != WIFI_CTRL_TYPE)
	{		
		return _FAIL;
	}

	//receive the frames that ra(a1) is my address
	if (!_rtw_memcmp(GetAddr1Ptr(pframe), myid(&padapter->eeprompriv), ETH_ALEN))
	{
		return _FAIL;
	}

	//only handle ps-poll
	if(GetFrameSubType(pframe) == WIFI_PSPOLL)
	{
		u16 aid;
		u8 wmmps_ac=0;	
		struct sta_info *psta=NULL;
	
		aid = GetAid(pframe);
		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
		
		if((psta==NULL) || (psta->aid!=aid))
		{
			return _FAIL;
		}

		//for rx pkt statistics
		psta->sta_stats.rx_pkts++;

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

		if((psta->state&WIFI_SLEEP_STATE) && (pstapriv->sta_dz_bitmap&BIT(psta->aid)))
		{
			_irqL irqL;	 
			_list	*xmitframe_plist, *xmitframe_phead;
			struct xmit_frame *pxmitframe=NULL;
		
			_enter_critical_bh(&psta->sleep_q.lock, &irqL);	

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

				if(padapter->HalFunc.hal_xmit(padapter, pxmitframe) == _TRUE)
				{		
					rtw_os_xmit_complete(padapter, pxmitframe);
				}

				if(psta->sleepq_len==0)
				{
					pstapriv->tim_bitmap &= ~BIT(psta->aid);

					//DBG_871X("after handling ps-poll, tim=%x\n", pstapriv->tim_bitmap);

					//upate BCN for TIM IE
					//update_BCNTIM(padapter);		
					update_beacon(padapter, _TIM_IE_, NULL, _FALSE);
				}
				
			}
			else
			{
				//DBG_871X("no buffered packets to xmit\n");
				if(pstapriv->tim_bitmap&BIT(psta->aid))
				{
					if(psta->sleepq_len==0)
					{
						DBG_871X("no buffered packets to xmit\n");
					}
					else
					{
						DBG_871X("error!psta->sleepq_len=%d\n", psta->sleepq_len);
						psta->sleepq_len=0;						
					}
				
					pstapriv->tim_bitmap &= ~BIT(psta->aid);					

					//upate BCN for TIM IE
					//update_BCNTIM(padapter);
					update_beacon(padapter, _TIM_IE_, NULL, _FALSE);
				}
				
			}
	
			_exit_critical_bh(&psta->sleep_q.lock, &irqL);			
			
		}
		
	}
	
#endif

	return _FAIL;

}

static sint validate_recv_mgnt_frame(_adapter *adapter, union recv_frame *precv_frame)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

#ifdef CONFIG_TDLS
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if(pmlmeinfo->tdls_ch_sensing==1 && pmlmeinfo->tdls_cur_channel !=0){
		pmlmeinfo->tdls_collect_pkt_num[pmlmeinfo->tdls_cur_channel-1]++;
	}
#endif

	RT_TRACE(_module_rtl871x_recv_c_, _drv_info_, ("+validate_recv_mgnt_frame\n"));

#if 0
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
#ifdef CONFIG_NATIVEAP_MLME		
	        mgt_dispatcher(adapter, precv_frame);
#else
		rtw_hostapd_mlme_rx(adapter, precv_frame);
#endif	
	}
	else
	{
		mgt_dispatcher(adapter, precv_frame);
	}
#endif

#ifdef CONFIG_AP_MODE
	{
		//for rx pkt statistics
		struct sta_info *psta = rtw_get_stainfo(&adapter->stapriv, GetAddr2Ptr(precv_frame->u.hdr.rx_data));
		if(psta)
			psta->sta_stats.rx_pkts++;	
	}
#endif

	mgt_dispatcher(adapter, precv_frame);

	return _SUCCESS;

}


static sint validate_recv_data_frame(_adapter *adapter, union recv_frame *precv_frame)
{
	int res;
	u8 bretry;
	u8 *psa, *pda, *pbssid;
	struct sta_info *psta = NULL;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*pattrib = & precv_frame->u.hdr.attrib;
	struct sta_priv 	*pstapriv = &adapter->stapriv;
	struct security_priv	*psecuritypriv = &adapter->securitypriv;	
	sint ret = _SUCCESS;
#ifdef CONFIG_TDLS
	struct sta_info  *ptdls_sta = NULL;
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);	
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
#endif

_func_enter_;

	bretry = GetRetry(ptr);
	pda = get_da(ptr);
	psa = get_sa(ptr);
	pbssid = get_hdr_bssid(ptr);

	if(pbssid == NULL){
		ret= _FAIL;
		goto exit;
	}

#ifdef CONFIG_TDLS
	if(pmlmeinfo->tdls_ch_sensing==1 && pmlmeinfo->tdls_cur_channel !=0){
		pmlmeinfo->tdls_collect_pkt_num[pmlmeinfo->tdls_cur_channel-1]++;
	}
#endif

	_rtw_memcpy(pattrib->dst, pda, ETH_ALEN);
	_rtw_memcpy(pattrib->src, psa, ETH_ALEN);

	_rtw_memcpy(pattrib->bssid, pbssid, ETH_ALEN);

	switch(pattrib->to_fr_ds)
	{
		case 0:
			_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
			res= sta2sta_data_frame(adapter, precv_frame, &psta);
			break;

		case 1:
			_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, pbssid, ETH_ALEN);
			res= ap2sta_data_frame(adapter, precv_frame, &psta);
			break;

		case 2:
			_rtw_memcpy(pattrib->ra, pbssid, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
			res= sta2ap_data_frame(adapter, precv_frame, &psta);
			break;

		case 3:
			_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
			_rtw_memcpy(pattrib->ta, GetAddr2Ptr(ptr), ETH_ALEN);
			res=_FAIL;
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" case 3\n"));
			break;

		default:
			res=_FAIL;
			break;

	}

	if(res==_FAIL){
		//RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,(" after to_fr_ds_chk; res = fail \n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s case:%d, res:%d\n", __FUNCTION__, pattrib->to_fr_ds, res);
		#endif
		ret= res;
		goto exit;
	}


	if(psta==NULL){
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" after to_fr_ds_chk; psta==NULL \n"));
		ret= _FAIL;
		goto exit;
	}
	
	//psta->rssi = prxcmd->rssi;
	//psta->signal_quality= prxcmd->sq;
	precv_frame->u.hdr.psta = psta;
		

	pattrib->amsdu=0;
	//parsing QC field
	if(pattrib->qos == 1)
	{
		pattrib->priority = GetPriority((ptr + 24));
		pattrib->ack_policy =GetAckpolicy((ptr + 24));
		pattrib->amsdu = GetAMsdu((ptr + 24));
		pattrib->hdrlen = pattrib->to_fr_ds==3 ? 32 : 26;

		if(pattrib->priority!=0 && pattrib->priority!=3)
		{
			adapter->recvpriv.bIsAnyNonBEPkts = _TRUE;
		}
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

#ifdef CONFIG_TDLS
	//checking reordering per direct link
	if((pmlmeinfo->tdls_setup_state==TDLS_LINKED_STATE)&&(pattrib->to_fr_ds==0)){
		ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->src);
		if(ptdls_sta==NULL){
			ret= _FAIL;
			goto exit;
		}else{
			precv_frame->u.hdr.preorder_ctrl = &ptdls_sta->recvreorder_ctrl[pattrib->priority];

			// decache, drop duplicate recv packets
			if(recv_decache(precv_frame, bretry, &ptdls_sta->sta_recvpriv.rxcache) == _FAIL)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("decache : drop pkt\n"));
				ret= _FAIL;
				goto exit;
			}
		}
		if(ptdls_sta->dot118021XPrivacy==_AES_)
			pattrib->encrypt=ptdls_sta->dot118021XPrivacy;
	}else
#endif
	{
		precv_frame->u.hdr.preorder_ctrl = &psta->recvreorder_ctrl[pattrib->priority];

		// decache, drop duplicate recv packets
		if(recv_decache(precv_frame, bretry, &psta->sta_recvpriv.rxcache) == _FAIL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("decache : drop pkt\n"));
			ret= _FAIL;
			goto exit;
		}
	}

	if(pattrib->privacy){

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("validate_recv_data_frame:pattrib->privacy=%x\n", pattrib->privacy));
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n ^^^^^^^^^^^IS_MCAST(pattrib->ra(0x%02x))=%d^^^^^^^^^^^^^^^6\n", pattrib->ra[0],IS_MCAST(pattrib->ra)));

#ifdef CONFIG_TDLS
		if(ptdls_sta==NULL)
#endif
			GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, IS_MCAST(pattrib->ra));

		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n pattrib->encrypt=%d\n",pattrib->encrypt));

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

static sint validate_recv_frame(_adapter *adapter, union recv_frame *precv_frame)
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

_func_enter_;


#ifdef CONFIG_FIND_BEST_CHANNEL
	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		pmlmeext->channel_set[pmlmeext->sitesurvey_res.channel_idx].rx_count++;
	}
#endif

#if 0
DBG_871X("\n");
{
	int i;
	for(i=0; i<64;i=i+8)
		DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:", *(ptr+i),
		*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));

}
DBG_871X("\n");
#endif

	//add version chk
	if(ver!=0){
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_data_frame fail! (ver!=0)\n"));
		retval= _FAIL;
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
#if 0 //for debug

if(pHalData->bDumpRxPkt ==1){
	int i;
	DBG_871X("############################# \n");
	
	for(i=0; i<64;i=i+8)
		DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(ptr+i),
		*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));
	DBG_871X("############################# \n");
}
else if(pHalData->bDumpRxPkt ==2){
	if(type== WIFI_MGT_TYPE){
		int i;
		DBG_871X("############################# \n");

		for(i=0; i<64;i=i+8)
			DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(ptr+i),
			*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));
		DBG_871X("############################# \n");
	}
}
else if(pHalData->bDumpRxPkt ==3){
	if(type== WIFI_DATA_TYPE){
		int i;
		DBG_871X("############################# \n");
		
		for(i=0; i<64;i=i+8)
			DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(ptr+i),
			*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));
		DBG_871X("############################# \n");
	}
}

#endif
	switch (type)
	{
		case WIFI_MGT_TYPE: //mgnt
			retval = validate_recv_mgnt_frame(adapter, precv_frame);
			if (retval == _FAIL)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_mgnt_frame fail\n"));
			}
			retval = _FAIL; // only data frame return _SUCCESS
			break;
		case WIFI_CTRL_TYPE: //ctrl
			retval = validate_recv_ctrl_frame(adapter, precv_frame);
			if (retval == _FAIL)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_ctrl_frame fail\n"));
			}
			retval = _FAIL; // only data frame return _SUCCESS
			break;
		case WIFI_DATA_TYPE: //data
			rtw_led_control(adapter, LED_CTL_RX);
			pattrib->qos = (subtype & BIT(7))? 1:0;
			retval = validate_recv_data_frame(adapter, precv_frame);
			if (retval == _FAIL)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_data_frame fail\n"));
			}
			break;
		default:
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
static sint wlanhdr_to_ethhdr ( union recv_frame *precvframe)
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
	if((_rtw_memcmp(psnap, rfc1042_header, SNAP_SIZE) &&
		(_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_IPX, 2) == _FALSE) && 
		(_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_AARP, 2)==_FALSE) )||
		//eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
		 _rtw_memcmp(psnap, bridge_tunnel_header, SNAP_SIZE)){
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

	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE))	   	
	{
		ptr += rmv_len ;	
		*ptr = 0x87;
		*(ptr+1) = 0x12;

		eth_type = 0x8712;
		// append rx status for mp test packets
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2)-24);
		_rtw_memcpy(ptr, get_rxmem(precvframe), 24);
		ptr+=24;
	}
	else {
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+ (bsnaphdr?2:0)));
	}

	_rtw_memcpy(ptr, pattrib->dst, ETH_ALEN);
	_rtw_memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

	if(!bsnaphdr) {
		len = htons(len);
		_rtw_memcpy(ptr+12, &len, 2);
	}

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

static void count_rx_stats(_adapter *padapter, union recv_frame *prframe)
{
	int	sz;
	struct sta_info		*psta = NULL;
	struct stainfo_stats	*pstats = NULL;
	struct rx_pkt_attrib	*pattrib = & prframe->u.hdr.attrib;
	struct recv_priv		*precvpriv = &padapter->recvpriv;

	sz = get_recvframe_len(prframe);
	precvpriv->rx_bytes += sz;

	padapter->mlmepriv.LinkDetectInfo.NumRxOkInPeriod++;

	if( (!MacAddr_isBcst(pattrib->dst)) && (!IS_MCAST(pattrib->dst))){
		padapter->mlmepriv.LinkDetectInfo.NumRxUnicastOkInPeriod++;
	}

	psta = prframe->u.hdr.psta;

	if(psta)
	{
		pstats = &psta->sta_stats;

		pstats->rx_pkts++;
		pstats->rx_bytes += sz;
	}

}


//perform defrag
static union recv_frame * recvframe_defrag(_adapter *adapter,_queue *defrag_q)
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
static union recv_frame * recvframe_chk_defrag(_adapter *padapter,union recv_frame* precv_frame)
{
	u8	ismfrag;
	u8	fragnum;
	u8	*psta_addr;
	struct recv_frame_hdr *pfhdr;
	struct sta_info * psta;
	struct	sta_priv *pstapriv ;
	_list	 *phead;
	union recv_frame* prtnframe=NULL;
	_queue *pfree_recv_queue, *pdefrag_q;

_func_enter_;

	pstapriv = &padapter->stapriv;

	pfhdr=&precv_frame->u.hdr;

	pfree_recv_queue=&padapter->recvpriv.free_recv_queue;

	//need to define struct of wlan header frame ctrl
	ismfrag= pfhdr->attrib.mfrag;
	fragnum=pfhdr->attrib.frag_num;

	psta_addr=pfhdr->attrib.ta;
	psta=rtw_get_stainfo(pstapriv, psta_addr);
	if (psta==NULL)
		pdefrag_q = NULL;
	else
		pdefrag_q=&psta->sta_recvpriv.defrag_q;

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


static int amsdu_to_msdu(_adapter *padapter, union recv_frame *prframe)
{
#ifdef PLATFORM_LINUX	//for amsdu TP improvement,Creator: Thomas 
	int	a_len, padding_len;
	u16	eth_type, nSubframe_Length;	
	u8	nr_subframes, i;
	unsigned char *data_ptr, *pdata;
	struct rx_pkt_attrib *pattrib;
	_pkt *sub_skb,*subframes[MAX_SUBFRAME_COUNT];
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &(precvpriv->free_recv_queue);
	int	ret = _SUCCESS;

	nr_subframes = 0;

	pattrib = &prframe->u.hdr.attrib;

	recvframe_pull(prframe, prframe->u.hdr.attrib.hdrlen);
	
	if(prframe->u.hdr.attrib.iv_len >0)
	{
		recvframe_pull(prframe, prframe->u.hdr.attrib.iv_len);
	}

	a_len = prframe->u.hdr.len;

	pdata = prframe->u.hdr.rx_data;

	while(a_len > ETH_HLEN) {
		
		/* Offset 12 denote 2 mac address */
		//nSubframe_Length = *((u16*)(pdata + 12));
		//==m==>change the length order
		//nSubframe_Length = (nSubframe_Length>>8) + (nSubframe_Length<<8);
		//nSubframe_Length = ntohs(*((u16*)(pdata + 12)));
		nSubframe_Length = RTW_GET_BE16(pdata + 12);

		//ntohs(nSubframe_Length);

		if( a_len < (ETHERNET_HEADER_SIZE + nSubframe_Length) ) {
			DBG_8192C("nRemain_Length is %d and nSubframe_Length is : %d\n",a_len,nSubframe_Length);
			goto exit;
		}

		/* move the data point to data content */
		pdata += ETH_HLEN;
		a_len -= ETH_HLEN;

		/* Allocate new skb for releasing to upper layer */
#ifdef CONFIG_SKB_COPY
		sub_skb = dev_alloc_skb(nSubframe_Length + 12);
		if(sub_skb)
		{
			skb_reserve(sub_skb, 12);
			data_ptr = (u8 *)skb_put(sub_skb, nSubframe_Length);
			_rtw_memcpy(data_ptr, pdata, nSubframe_Length);
		}
		else
		{
#endif // CONFIG_SKB_COPY
			sub_skb = skb_clone(prframe->u.hdr.pkt, GFP_ATOMIC);
			if(sub_skb)
			{
				sub_skb->data = pdata;
				sub_skb->len = nSubframe_Length;
				sub_skb->tail = sub_skb->data + nSubframe_Length;
			}
			else
			{
				DBG_8192C("skb_clone() Fail!!! , nr_subframes = %d\n",nr_subframes);
				break;
			}
		}

		//sub_skb->dev = padapter->pnetdev;
		subframes[nr_subframes++] = sub_skb;
		if(nr_subframes >= MAX_SUBFRAME_COUNT) {
			DBG_8192C("ParseSubframe(): Too many Subframes! Packets dropped!\n");
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
				goto exit;
			}
			pdata += padding_len;
			a_len -= padding_len;
		}
	}

	for(i=0; i<nr_subframes; i++){
		sub_skb = subframes[i];
		/* convert hdr + possible LLC headers into Ethernet header */
		//eth_type = (sub_skb->data[6] << 8) | sub_skb->data[7];
		//eth_type = ntohs(*(u16*)&sub_skb->data[6]);
		eth_type = RTW_GET_BE16(&sub_skb->data[6]);
		if (sub_skb->len >= 8 &&
			((_rtw_memcmp(sub_skb->data, rfc1042_header, SNAP_SIZE) &&
			  eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
			 _rtw_memcmp(sub_skb->data, bridge_tunnel_header, SNAP_SIZE) )) {
			/* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
			skb_pull(sub_skb, SNAP_SIZE);
			_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->src, ETH_ALEN);
			_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->dst, ETH_ALEN);
		} else {
			u16 len;
			/* Leave Ethernet header part of hdr and full payload */
			len = htons(sub_skb->len);
			_rtw_memcpy(skb_push(sub_skb, 2), &len, 2);
			_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->src, ETH_ALEN);
			_rtw_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->dst, ETH_ALEN);
		}

		/* Indicat the packets to upper layer */
		if (sub_skb) {
			//memset(sub_skb->cb, 0, sizeof(sub_skb->cb));

#ifdef CONFIG_BR_EXT
			// Insert NAT2.5 RX here!
			struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
			void *br_port = NULL;

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
			br_port = padapter->pnetdev->br_port;
#else   // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
			rcu_read_lock();
			br_port = rcu_dereference(padapter->pnetdev->rx_handler_data);
			rcu_read_unlock();
#endif  // (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))


			if( br_port && (check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE) )	 	
			{
				int nat25_handle_frame(_adapter *priv, struct sk_buff *skb);
				if (nat25_handle_frame(padapter, sub_skb) == -1) {
					//priv->ext_stats.rx_data_drops++;
					//DEBUG_ERR("RX DROP: nat25_handle_frame fail!\n");
					//return FAIL;
					
#if 1
					// bypass this frame to upper layer!!
#else
					dev_kfree_skb_any(sub_skb);
					continue;
#endif
				}							
			}
#endif	// CONFIG_BR_EXT

			sub_skb->protocol = eth_type_trans(sub_skb, padapter->pnetdev);
			sub_skb->dev = padapter->pnetdev;

#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
			if ( (pattrib->tcpchk_valid == 1) && (pattrib->tcp_chkrpt == 1) ) {
				sub_skb->ip_summed = CHECKSUM_UNNECESSARY;
			} else {
				sub_skb->ip_summed = CHECKSUM_NONE;
			}
#else /* !CONFIG_TCP_CSUM_OFFLOAD_RX */
			sub_skb->ip_summed = CHECKSUM_NONE;
#endif

			netif_rx(sub_skb);
		}
	}

exit:

	prframe->u.hdr.len=0;
	rtw_free_recvframe(prframe, pfree_recv_queue);//free this recv_frame
	
	return ret;
#else
	_irqL irql;
	unsigned char *ptr, *pdata, *pbuf, *psnap_type;
	union recv_frame *pnrframe, *pnrframe_new;
	int a_len, mv_len, padding_len;
	u16 eth_type, type_len;
	u8 bsnaphdr;
	struct ieee80211_snap_hdr	*psnap;
	struct _vlan *pvlan;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &(precvpriv->free_recv_queue);
	int ret = _SUCCESS;
#ifdef PLATFORM_WINDOWS
	struct recv_buf *precvbuf = prframe->u.hdr.precvbuf;
#endif
	a_len = prframe->u.hdr.len - prframe->u.hdr.attrib.hdrlen;

	recvframe_pull(prframe, prframe->u.hdr.attrib.hdrlen);

	if(prframe->u.hdr.attrib.iv_len >0)
	{
		recvframe_pull(prframe, prframe->u.hdr.attrib.iv_len);
	}

	pdata = prframe->u.hdr.rx_data;

	prframe->u.hdr.len=0;

	pnrframe = prframe;


	do{

		mv_len=0;
		pnrframe->u.hdr.rx_data = pnrframe->u.hdr.rx_tail = pdata;
		ptr = pdata;


		_rtw_memcpy(pnrframe->u.hdr.attrib.dst, ptr, ETH_ALEN);
		ptr+=ETH_ALEN;
		_rtw_memcpy(pnrframe->u.hdr.attrib.src, ptr, ETH_ALEN);
		ptr+=ETH_ALEN;

		_rtw_memcpy(&type_len, ptr, 2);
		type_len= ntohs((unsigned short )type_len);
		ptr +=2;
		mv_len += ETH_HLEN;

		recvframe_put(pnrframe, type_len+ETH_HLEN);//update tail;

		if(pnrframe->u.hdr.rx_data >= pnrframe->u.hdr.rx_tail || type_len<8)
		{
			//panic("pnrframe->u.hdr.rx_data >= pnrframe->u.hdr.rx_tail || type_len<8\n");

			rtw_free_recvframe(pnrframe, pfree_recv_queue);

			goto exit;
		}

		psnap=(struct ieee80211_snap_hdr *)(ptr);
		psnap_type=ptr+SNAP_SIZE;
		if (psnap->dsap==0xaa && psnap->ssap==0xaa && psnap->ctrl==0x03)
		{
			if ( _rtw_memcmp(psnap->oui, oui_rfc1042, WLAN_IEEE_OUI_LEN))
			{
				bsnaphdr=_TRUE;//wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_RFC1042;
			}
			else if (_rtw_memcmp(psnap->oui, SNAP_HDR_APPLETALK_DDP, WLAN_IEEE_OUI_LEN) &&
					_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_DDP, 2) )
			{
				bsnaphdr=_TRUE;	//wlan_pkt_format = WLAN_PKT_FORMAT_APPLETALK;
			}
			else if (_rtw_memcmp( psnap->oui, oui_8021h, WLAN_IEEE_OUI_LEN))
			{
				bsnaphdr=_TRUE;	//wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_TUNNEL;
			}
			else
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("drop pkt due to invalid frame format!\n"));

				//KeBugCheckEx(0x87123333, 0xe0, 0x4c, 0x87, 0xdd);

				//panic("0x87123333, 0xe0, 0x4c, 0x87, 0xdd\n");

				rtw_free_recvframe(pnrframe, pfree_recv_queue);

				goto exit;
			}

		}
		else
		{
			bsnaphdr=_FALSE;//wlan_pkt_format = WLAN_PKT_FORMAT_OTHERS;
		}

		ptr += (bsnaphdr?SNAP_SIZE:0);
		_rtw_memcpy(&eth_type, ptr, 2);
		eth_type= ntohs((unsigned short )eth_type); //pattrib->ether_type

		mv_len+= 2+(bsnaphdr?SNAP_SIZE:0);
		ptr += 2;//now move to iphdr;

		pvlan = NULL;
		if(eth_type == 0x8100) //vlan
		{
			pvlan = (struct _vlan *)ptr;
			ptr+=4;
			mv_len+=4;
		}

		if(eth_type==0x0800)//ip
		{
			struct iphdr*  piphdr = (struct iphdr*)ptr;


			if (piphdr->protocol == 0x06)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("@@@===recv tcp len:%d @@@===\n", pnrframe->u.hdr.len));
			}
		}
#ifdef PLATFORM_OS_XP
		else
		{
			NDIS_PACKET_8021Q_INFO VlanPriInfo;
			UINT32 UserPriority = pnrframe->u.hdr.attrib.priority;
			UINT32 VlanID = (pvlan!=NULL ? get_vlan_id(pvlan) : 0 );

			VlanPriInfo.Value =          // Get current value.
					NDIS_PER_PACKET_INFO_FROM_PACKET(pnrframe->u.hdr.pkt, Ieee8021QInfo);

			VlanPriInfo.TagHeader.UserPriority = UserPriority;
			VlanPriInfo.TagHeader.VlanId =  VlanID;

			VlanPriInfo.TagHeader.CanonicalFormatId = 0; // Should be zero.
			VlanPriInfo.TagHeader.Reserved = 0; // Should be zero.
			NDIS_PER_PACKET_INFO_FROM_PACKET(pnrframe->u.hdr.pkt, Ieee8021QInfo) = VlanPriInfo.Value;

		}
#endif

		pbuf = recvframe_pull(pnrframe, (mv_len-sizeof(struct ethhdr)));

		_rtw_memcpy(pbuf, pnrframe->u.hdr.attrib.dst, ETH_ALEN);
		_rtw_memcpy(pbuf+ETH_ALEN, pnrframe->u.hdr.attrib.src, ETH_ALEN);

		eth_type = htons((unsigned short)eth_type) ;
		_rtw_memcpy(pbuf+12, &eth_type, 2);

		padding_len = (4) - ((type_len + ETH_HLEN)&(4-1));

		a_len -= (type_len + ETH_HLEN + padding_len) ;


#if 0

	if(a_len > ETH_HLEN)
	{
		pnrframe_new = rtw_alloc_recvframe(pfree_recv_queue);
		if(pnrframe_new)
		{
			_pkt *pskb_copy;
			unsigned int copy_len  = pnrframe->u.hdr.len;

			_rtw_init_listhead(&pnrframe_new->u.hdr.list);

	#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
			pskb_copy = dev_alloc_skb(copy_len+64);
	#else
			pskb_copy = netdev_alloc_skb(padapter->pnetdev, copy_len + 64);
	#endif
			if(pskb_copy==NULL)
			{
				DBG_8192C("amsdu_to_msdu:can not all(ocate memory for skb copy\n");
			}

			pnrframe_new->u.hdr.pkt = pskb_copy;

			_rtw_memcpy(pskb_copy->data, pnrframe->u.hdr.rx_data, copy_len);

			pnrframe_new->u.hdr.rx_data = pnrframe->u.hdr.rx_data;
			pnrframe_new->u.hdr.rx_tail = pnrframe->u.hdr.rx_data + copy_len;


			if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE))
			{
				rtw_recv_indicatepkt(padapter, pnrframe_new);//indicate this recv_frame
			}
			else
			{
				rtw_free_recvframe(pnrframe_new, pfree_recv_queue);//free this recv_frame
			}

		}
		else
		{
			DBG_8192C("amsdu_to_msdu:can not allocate memory for pnrframe_new\n");
		}

	}
	else
	{
		if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE))
		{
			rtw_recv_indicatepkt(padapter, pnrframe);//indicate this recv_frame
		}
		else
		{
			rtw_free_recvframe(pnrframe, pfree_recv_queue);//free this recv_frame
		}

		pnrframe = NULL;

	}

#else

		//padding_len = (4) - ((type_len + ETH_HLEN)&(4-1));

		//a_len -= (type_len + ETH_HLEN + padding_len) ;

		pnrframe_new = NULL;


		if(a_len > ETH_HLEN)
		{
			pnrframe_new = rtw_alloc_recvframe(pfree_recv_queue);

			if(pnrframe_new)
			{


				//pnrframe_new->u.hdr.precvbuf = precvbuf;//precvbuf is assigned before call rtw_init_recvframe()
				//rtw_init_recvframe(pnrframe_new, precvpriv);
				{
						_pkt *pskb = pnrframe->u.hdr.pkt;
						_rtw_init_listhead(&pnrframe_new->u.hdr.list);

						pnrframe_new->u.hdr.len=0;

#ifdef PLATFORM_LINUX
						if(pskb)
						{
							pnrframe_new->u.hdr.pkt = skb_clone(pskb, GFP_ATOMIC);
						}
#endif

				}

				pdata += (type_len + ETH_HLEN + padding_len);
				pnrframe_new->u.hdr.rx_head = pnrframe_new->u.hdr.rx_data = pnrframe_new->u.hdr.rx_tail = pdata;
				pnrframe_new->u.hdr.rx_end = pdata + a_len + padding_len;//

#ifdef PLATFORM_WINDOWS
				pnrframe_new->u.hdr.precvbuf=precvbuf;
				_enter_critical_bh(&precvbuf->recvbuf_lock, &irql);
				precvbuf->ref_cnt++;
				_exit_critical_bh(&precvbuf->recvbuf_lock, &irql);
#endif

			}
			else
			{
				//panic("pnrframe_new=%x\n", pnrframe_new);
			}
		}


		if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE) )
		{
			rtw_recv_indicatepkt(padapter, pnrframe);//indicate this recv_frame
		}
		else
		{
			rtw_free_recvframe(pnrframe, pfree_recv_queue);//free this recv_frame
		}


		pnrframe = NULL;
		if(pnrframe_new)
		{
			pnrframe = pnrframe_new;
		}


#endif

	}while(pnrframe);

exit:

	return ret;
#endif
}


static int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, u16 seq_num)
{
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
		DBG_871X("DBG_RX_DROP_FRAME %s IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__,
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

		#ifdef DBG_RX_SEQ
		DBG_871X("DBG_RX_SEQ %s:%d SN_LESS(wend, seq_num) IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
			preorder_ctrl->indicate_seq, seq_num);
		#endif
	}

	//DbgPrint("exit->check_indicate_seq(): IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);

	return _TRUE;
}


static int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl, union recv_frame *prframe)
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


static int recv_indicatepkts_in_order(_adapter *padapter, struct recv_reorder_ctrl *preorder_ctrl, int bforced)
{
	_irqL irql;
	//u8 bcancelled;
	_list	*phead, *plist;
	union recv_frame *prframe;
	struct rx_pkt_attrib *pattrib;
	//u8 index = 0;
	int bPktInBuf = _FALSE;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

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
		if(rtw_is_list_empty(phead))
		{
			// _exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
			//_rtw_spinunlock_ex(&ppending_recvframe_queue->lock);
			return _TRUE;
		}
	
		 prframe = LIST_CONTAINOR(plist, union recv_frame, u);
	        pattrib = &prframe->u.hdr.attrib;	
		preorder_ctrl->indicate_seq = pattrib->seq_num;		
		#ifdef DBG_RX_SEQ
		DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d, NewSeq: %d\n", __FUNCTION__, __LINE__,
			preorder_ctrl->indicate_seq, pattrib->seq_num);
		#endif
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
					//DBG_8192C("_cancel_timer(&preorder_ctrl->reordering_ctrl_timer, &bcancelled);\n");
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
				//DBG_8192C("recv_indicatepkts_in_order, amsdu!=1, indicate_seq=%d, seq_num=%d\n", preorder_ctrl->indicate_seq, pattrib->seq_num);

				if ((padapter->bDriverStopped == _FALSE) &&
				    (padapter->bSurpriseRemoved == _FALSE))
				{				
					
					rtw_recv_indicatepkt(padapter, prframe);		//indicate this recv_frame
					
				}
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
		//DBG_8192C("_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME)\n");
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


static int recv_indicatepkt_reorder(_adapter *padapter, union recv_frame *prframe)
{
	_irqL irql;
	int retval = _SUCCESS;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct recv_reorder_ctrl *preorder_ctrl = prframe->u.hdr.preorder_ctrl;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

	if(!pattrib->amsdu)
	{
		//s1.
		wlanhdr_to_ethhdr(prframe);

		if(pattrib->qos !=1 /*|| pattrib->priority!=0 || IS_MCAST(pattrib->ra)*/)
		{
			if ((padapter->bDriverStopped == _FALSE) &&
			    (padapter->bSurpriseRemoved == _FALSE))
			{
				RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@  recv_indicatepkt_reorder -recv_func recv_indicatepkt\n" ));

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
		//pHTInfo->RxReorderDropCounter++;
		//ReturnRFDList(Adapter, pRfd);
		//RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("RxReorderIndicatePacket() ==> Packet Drop!!\n"));
		//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
		//return _FAIL;

		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s check_indicate_seq fail\n", __FUNCTION__);
		#endif
		goto _err_exit;
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
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);
	}
	else
	{
		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);
		_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);
	}


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


	if(padapter->bDriverStopped ||padapter->bSurpriseRemoved)
	{
		return;
	}

	//DBG_8192C("+rtw_reordering_ctrl_timeout_handler()=>\n");

	_enter_critical_bh(&ppending_recvframe_queue->lock, &irql);

	if(recv_indicatepkts_in_order(padapter, preorder_ctrl, _TRUE)==_TRUE)
	{
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);		
	}

	_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);

}


static int process_recv_indicatepkts(_adapter *padapter, union recv_frame *prframe)
{
	int retval = _SUCCESS;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

#ifdef CONFIG_80211N_HT

	struct ht_priv	*phtpriv = &pmlmepriv->htpriv;

	if(phtpriv->ht_option==_TRUE) //B/G/N Mode
	{
		//prframe->u.hdr.preorder_ctrl = &precvpriv->recvreorder_ctrl[pattrib->priority];

		if(recv_indicatepkt_reorder(padapter, prframe)!=_SUCCESS)// including perform A-MPDU Rx Ordering Buffer Control
		{
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s recv_indicatepkt_reorder error!\n", __FUNCTION__);
			#endif
		
			if ((padapter->bDriverStopped == _FALSE) &&
			    (padapter->bSurpriseRemoved == _FALSE))
			{
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

		if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE))
		{
			//indicate this recv_frame
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("@@@@ process_recv_indicatepkts- recv_func recv_indicatepkt\n" ));
			rtw_recv_indicatepkt(padapter, prframe);


		}
		else
		{
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("@@@@ process_recv_indicatepkts- recv_func free_indicatepkt\n" ));

			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("recv_func:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
			retval = _FAIL;
			return retval;
		}

	}

	return retval;

}
 

static int recv_func(_adapter *padapter, void *pcontext)
{
	struct rx_pkt_attrib *pattrib;
	union recv_frame *prframe, *orig_prframe;
	int retval = _SUCCESS;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_TDLS
	u8 *psnap_type, *pcategory;
#endif

	prframe = (union recv_frame *)pcontext;
	orig_prframe = prframe;

	pattrib = &prframe->u.hdr.attrib;

#ifdef CONFIG_MP_INCLUDED
	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE))//&&(padapter->mppriv.check_mp_pkt == 0))
	{
		if (pattrib->crc_err == 1)
			padapter->mppriv.rx_crcerrpktcount++;
		else
			padapter->mppriv.rx_pktcount++;

		if (check_fwstate(pmlmepriv, WIFI_MP_LPBK_STATE) == _FALSE) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("MP - Not in loopback mode , drop pkt \n"));
			retval = _FAIL;
			rtw_free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
			goto _exit_recv_func;
		}
	}
#endif

	//check the frame crtl field and decache
	retval = validate_recv_frame(padapter, prframe);
	if (retval != _SUCCESS)
	{
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_, ("recv_func: validate_recv_frame fail! drop pkt\n"));
		rtw_free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
		goto _exit_recv_func;
	}
	// DATA FRAME
	rtw_led_control(padapter, LED_CTL_RX);

	prframe = decryptor(padapter, prframe);
	if (prframe == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("decryptor: drop pkt\n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s decryptor: drop pkt\n", __FUNCTION__);
		#endif
		retval = _FAIL;
		goto _exit_recv_func;
	}

#ifdef CONFIG_TDLS
	//check TDLS frame
	psnap_type = get_recvframe_data(orig_prframe);
	psnap_type+=pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	//[+2]: ether_type, [+1]: payload type
	pcategory = psnap_type+2+1;
	if((_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_TDLS, 2))&&((*pcategory==0x0c))){
		retval = OnTDLS(padapter, prframe);	//all of functions will return _FAIL
		goto _exit_recv_func;
	}
#endif

	prframe = recvframe_chk_defrag(padapter, prframe);
	if(prframe==NULL)	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvframe_chk_defrag: drop pkt\n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s recvframe_chk_defrag: drop pkt\n", __FUNCTION__);
		#endif
		goto _exit_recv_func;		
	}

	prframe=portctrl(padapter, prframe);
	if (prframe == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("portctrl: drop pkt \n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s portctrl: drop pkt\n", __FUNCTION__);
		#endif
		retval = _FAIL;
		goto _exit_recv_func;
	}

	count_rx_stats(padapter, prframe);

#ifdef CONFIG_80211N_HT

	retval = process_recv_indicatepkts(padapter, prframe);
	if (retval != _SUCCESS)
	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recv_func: process_recv_indicatepkts fail! \n"));
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s recv_func: process_recv_indicatepkts fail!\n", __FUNCTION__);
		#endif
		rtw_free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
		goto _exit_recv_func;
	}

#else

	if (!pattrib->amsdu)
	{
		retval = wlanhdr_to_ethhdr (prframe);
		if (retval != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("wlanhdr_to_ethhdr: drop pkt \n"));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s wlanhdr_to_ethhdr: drop pkt\n", __FUNCTION__);
			#endif
			rtw_free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
			goto _exit_recv_func;
		}

		if ((padapter->bDriverStopped == _FALSE) && (padapter->bSurpriseRemoved == _FALSE))
		{
			RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@ recv_func: recv_func rtw_recv_indicatepkt\n" ));
			//indicate this recv_frame
			retval = rtw_recv_indicatepkt(padapter, prframe);
			if (retval != _SUCCESS)
			{	
				#ifdef DBG_RX_DROP_FRAME
				DBG_871X("DBG_RX_DROP_FRAME %s rtw_recv_indicatepkt fail!\n", __FUNCTION__);
				#endif
				goto _exit_recv_func;
			}
		}
		else
		{
			RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@  recv_func: rtw_free_recvframe\n" ));
			RT_TRACE(_module_rtl871x_recv_c_, _drv_debug_, ("recv_func:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s ecv_func:bDriverStopped(%d) OR bSurpriseRemoved(%d)\n", __FUNCTION__,
				padapter->bDriverStopped, padapter->bSurpriseRemoved);
			#endif
			retval = _FAIL;
			rtw_free_recvframe(orig_prframe, pfree_recv_queue); //free this recv_frame
		}

	}
	else if(pattrib->amsdu==1)
	{

		retval = amsdu_to_msdu(padapter, prframe);
		if(retval != _SUCCESS)
		{
			#ifdef DBG_RX_DROP_FRAME
			DBG_871X("DBG_RX_DROP_FRAME %s amsdu_to_msdu fail\n", __FUNCTION__);
			#endif
			rtw_free_recvframe(orig_prframe, pfree_recv_queue);
			goto _exit_recv_func;
		}
	}
	else
	{
		#ifdef DBG_RX_DROP_FRAME
		DBG_871X("DBG_RX_DROP_FRAME %s what is this condition??\n", __FUNCTION__);
		#endif
	}
#endif


_exit_recv_func:

	return retval;
}


s32 rtw_recv_entry(union recv_frame *precvframe)
{
	_adapter *padapter;
	struct recv_priv *precvpriv;
	//struct	mlme_priv	*pmlmepriv ;
	//struct dvobj_priv *pdev;
	//u8 *phead, *pdata, *ptail,*pend;

	//_queue *pfree_recv_queue, *ppending_recv_queue;
	//u8 blk_mode = _FALSE;
	s32 ret=_SUCCESS;
	//struct intf_hdl * pintfhdl;

_func_enter_;

//	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("+rtw_recv_entry\n"));

	padapter = precvframe->u.hdr.adapter;
	//pintfhdl = &padapter->iopriv.intf;

	//pdev=&padapter->dvobjpriv;	
	//pmlmepriv = &padapter->mlmepriv;
	precvpriv = &padapter->recvpriv;
	//pfree_recv_queue = &precvpriv->free_recv_queue;
	//ppending_recv_queue = &precvpriv->recv_pending_queue;

	//phead = precvframe->u.hdr.rx_head;
	//pdata = precvframe->u.hdr.rx_data;
	//ptail = precvframe->u.hdr.rx_tail;
	//pend = precvframe->u.hdr.rx_end;

	//rtw_led_control(padapter, LED_CTL_RX);

#ifdef CONFIG_SDIO_HCI
	if (precvpriv->free_recvframe_cnt <= 1)
		goto _recv_entry_drop;
#endif

#ifdef CONFIG_RECV_THREAD_MODE
	if (_rtw_queue_empty(ppending_recv_queue) == _TRUE)
	{
		//enqueue_recvframe_usb(precvframe, ppending_recv_queue);//enqueue to recv_pending_queue
	 	rtw_enqueue_recvframe(precvframe, ppending_recv_queue);
		_rtw_up_sema(&precvpriv->recv_sema);
	}
	else
	{
		//enqueue_recvframe_usb(precvframe, ppending_recv_queue);//enqueue to recv_pending_queue
		rtw_enqueue_recvframe(precvframe, ppending_recv_queue);
	}
#else
	if ((ret = recv_func(padapter, precvframe)) == _FAIL)
	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("rtw_recv_entry: recv_func return fail!!!\n"));
		goto _recv_entry_drop;
	}
#endif

	precvpriv->rx_pkts++;

_func_exit_;

	return ret;

_recv_entry_drop:


	precvpriv->rx_drop++;

#ifdef CONFIG_MP_INCLUDED
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
	u8 _alpha = 3; // this value is based on converging_constant = 5000 and sampling_interval = 1000

	if(adapter->recvpriv.is_signal_dbg) {
		//update the user specific value, signal_strength_dbg, to signal_strength, rssi
		adapter->recvpriv.signal_strength= adapter->recvpriv.signal_strength_dbg;
		adapter->recvpriv.rssi=(s8)translate_percentage_to_dbm((u8)adapter->recvpriv.signal_strength_dbg);
	} else {

		if(recvpriv->signal_strength_data.update_req == 0) {// update_req is clear, means we got rx
			avg_signal_strength = recvpriv->signal_strength_data.avg_val;
			avg_signal_qual = recvpriv->signal_qual_data.avg_val;
		}
		
		if(recvpriv->signal_qual_data.update_req == 0) {// update_req is clear, means we got rx
			num_signal_strength = recvpriv->signal_strength_data.total_num;
			num_signal_qual = recvpriv->signal_qual_data.total_num;
		}
		
		// after avg_vals are accquired, we can re-stat the signal values
		recvpriv->signal_strength_data.update_req = 1;
		recvpriv->signal_qual_data.update_req = 1;

		//update value of signal_strength, rssi, signal_qual
		if(check_fwstate(&adapter->mlmepriv, _FW_UNDER_SURVEY) == _FALSE) {
			tmp_s = (avg_signal_strength+(_alpha-1)*recvpriv->signal_strength);
			if(tmp_s %_alpha)
				tmp_s = tmp_s/_alpha + 1;
			else
				tmp_s = tmp_s/_alpha;
			if(tmp_s>100)
				tmp_s = 100;

			tmp_q = (avg_signal_qual+(_alpha-1)*recvpriv->signal_qual);
			if(tmp_q %_alpha)
				tmp_q = tmp_q/_alpha + 1;
			else
				tmp_q = tmp_q/_alpha;
			if(tmp_q>100)
				tmp_q = 100;

			recvpriv->signal_strength = tmp_s;
			recvpriv->rssi = (s8)translate_percentage_to_dbm(tmp_s);
			recvpriv->signal_qual = tmp_q;

			#if defined(DBG_RX_SIGNAL_DISPLAY_PROCESSING) && 1
			DBG_871X("%s signal_strength:%3u, rssi:%3d, signal_qual:%3u"
				", num_signal_strength:%u, num_signal_qual:%u"
				"\n"
				, __FUNCTION__
				, recvpriv->signal_strength
				, recvpriv->rssi
				, recvpriv->signal_qual
				, num_signal_strength, num_signal_qual
			);
			#endif
		}
	}
	rtw_set_signal_stat_timer(recvpriv);
	
}
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS



