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
#define _RTL871X_RECV_C_
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


/*
pfree_recv_queue accesser: returnPkt(dispatch level), rx_done(ISR?),  =>  using crtitical sec ??

caller of alloc_recvframe have to call =====> init_recvframe(precvframe, _pkt *pkt);

*/


void	_init_sta_recv_priv(struct sta_recv_priv *psta_recvpriv)
{	
	
	
_func_enter_;	

	_memset((u8 *)psta_recvpriv, 0, sizeof (struct sta_recv_priv));

	_spinlock_init(&psta_recvpriv->lock);

	//for(i=0; i<MAX_RX_NUMBLKS; i++)
	//	_init_queue(&psta_recvpriv->blk_strms[i]);

	_init_queue(&psta_recvpriv->defrag_q);
	
_func_exit_;

}

sint	_init_recv_priv(struct recv_priv *precvpriv, _adapter *padapter)
{
	sint i;

	union recv_frame *precvframe;

	sint	res=_SUCCESS;
	
_func_enter_;		

	 _memset((unsigned char *)precvpriv, 0, sizeof (struct  recv_priv));

	_spinlock_init(&precvpriv->lock);

	_init_queue(&precvpriv->free_recv_queue);
	_init_queue(&precvpriv->recv_pending_queue);
	
	precvpriv->adapter = padapter;
	
	precvpriv->free_recvframe_cnt = NR_RECVFRAME;

	os_recv_resource_init(precvpriv, padapter);

	precvpriv->pallocated_frame_buf = _vmalloc(NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);
	if(precvpriv->pallocated_frame_buf==NULL){
		res= _FAIL;
		goto exit;
	}	
	_memset(precvpriv->pallocated_frame_buf, 0, NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);

	precvpriv->precv_frame_buf = precvpriv->pallocated_frame_buf + RXFRAME_ALIGN_SZ -
							((uint) (precvpriv->pallocated_frame_buf) &(RXFRAME_ALIGN_SZ-1));

	precvframe = (union recv_frame*) precvpriv->precv_frame_buf;


	for(i=0; i < NR_RECVFRAME ; i++)
	{
		_init_listhead(&(precvframe->u.list));

		list_insert_tail(&(precvframe->u.list), &(precvpriv->free_recv_queue.queue));

		res = os_recv_resource_alloc(padapter, precvframe);
	
            	precvframe->u.hdr.adapter =padapter;
		precvframe++;
		
	}
	
#ifdef CONFIG_USB_HCI

	precvpriv->rx_pending_cnt=1;

	_init_sema(&precvpriv->allrxreturnevt, 0);		
	
#endif
	
	
	res = init_recv_priv(precvpriv, padapter);

	
exit:

_func_exit_;

	return res;
	
}

void mfree_recv_priv_lock(struct recv_priv *precvpriv)
{	
       _spinlock_free(&precvpriv->lock);
	_free_sema(&precvpriv->recv_sema);
	_free_sema(&precvpriv->terminate_recvthread_sema);

	_spinlock_free(&precvpriv->free_recv_queue.lock);
	_spinlock_free(&precvpriv->recv_pending_queue.lock);
}

void _free_recv_priv (struct recv_priv *precvpriv)
{
_func_enter_;		

	mfree_recv_priv_lock(precvpriv);

	os_recv_resource_free(precvpriv);

	if(precvpriv->pallocated_frame_buf)
		_vmfree(precvpriv->pallocated_frame_buf, NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);

	free_recv_priv(precvpriv);
	
_func_exit_;

}

union recv_frame *alloc_recvframe (_queue *pfree_recv_queue)
{
	_irqL irqL;
	union recv_frame  *precvframe;
	_list	*plist, *phead;
	_adapter *padapter;
	struct recv_priv *precvpriv;
_func_enter_;

	_enter_critical(&pfree_recv_queue->lock, &irqL);
	
	if(_queue_empty(pfree_recv_queue) == _TRUE)
	{
		precvframe = NULL;
	}
	else
	{
		phead = get_list_head(pfree_recv_queue);

		plist = get_next(phead);

		precvframe = LIST_CONTAINOR(plist, union recv_frame, u);

		list_delete(&precvframe->u.hdr.list);
		padapter=precvframe->u.hdr.adapter;
		if(padapter !=NULL){
			precvpriv=&padapter->recvpriv;
			if(pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt--;
		}
	}
	        
	_exit_critical(&pfree_recv_queue->lock, &irqL);
	
_func_exit_;

	return precvframe;
	
}

union recv_frame *dequeue_recvframe (_queue *queue)
{
	return alloc_recvframe(queue);	
}


sint	enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{	
       _irqL irqL;
	_adapter *padapter=precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	
_func_enter_;


	//_spinlock(&pfree_recv_queue->lock);
	 _enter_critical(&queue->lock, &irqL);

	//_init_listhead(&(precvframe->u.hdr.list));
	list_delete(&(precvframe->u.hdr.list));
	
	
	list_insert_tail(&(precvframe->u.hdr.list), get_list_head(queue));

	if(padapter !=NULL){			
			if(queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt++;
	}

	//_spinunlock(&pfree_recv_queue->lock);
	 _exit_critical(&queue->lock, &irqL);
		

_func_exit_;	

	return _SUCCESS;
}

/*
sint	enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{
	return free_recvframe(precvframe, queue);
}
*/




/*
caller : defrag ; recvframe_chk_defrag in recv_thread  (passive)
pframequeue: defrag_queue : will be accessed in recv_thread  (passive)

using spinlock to protect

*/

void free_recvframe_queue(_queue *pframequeue,  _queue *pfree_recv_queue)
{
	union	recv_frame 	*precvframe;
	_list	*plist, *phead;

_func_enter_;

    {
        unsigned long   flags;

        spin_lock_irqsave(&pframequeue->lock, flags);

    	phead = get_list_head(pframequeue);
    	plist = get_next(phead);
    
    	while(end_of_queue_search(phead, plist) == _FALSE)
    	{
    		precvframe = LIST_CONTAINOR(plist, union recv_frame, u);
    
    		plist = get_next(plist);
    		
    		//list_delete(&precvframe->u.hdr.list); // will do this in free_recvframe()
    		
    		free_recvframe(precvframe, pfree_recv_queue);
    	}
        spin_unlock_irqrestore(&pframequeue->lock, flags);
    }
	
_func_exit_;

}



sint recvframe_chkmic(_adapter *adapter,  union recv_frame *precvframe){

	sint 			i,res=_SUCCESS;
	u32	datalen;
	u8 miccode[8];
	u8	bmic_err=_FALSE;
	u8	*pframe, *payload,*pframemic;
	u8   *mickey,idx,*iv;
	struct	sta_info		*stainfo;

	struct	rx_pkt_attrib	*prxattrib=&precvframe->u.hdr.attrib;
	struct 	security_priv	*psecuritypriv=&adapter->securitypriv;
	//struct 	recv_stat *prxstat=(struct recv_stat *)precvframe->u.hdr.rx_head;
	
_func_enter_;	
	
	stainfo=get_stainfo(&adapter->stapriv ,&prxattrib->ta[0] );

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
                        	iv=precvframe->u.hdr.rx_data+prxattrib->hdrlen;
				idx=iv[3];
				mickey=&psecuritypriv->dot118021XGrprxmickey[(( (idx>>6)&0x3 ))-1].skey[0];

				RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n recvframe_chkmic: bcmc key \n"));
				if(psecuritypriv->binstallGrpkey==_FALSE)
                                {
					res=_FAIL;
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n recvframe_chkmic:didn't install group key!!!!!!!!!!\n"));
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
			//seccalctkipmic(&stainfo->dot11tkiprxmickey.skey[0],pframe,payload, datalen ,&miccode[0],(unsigned char)prxattrib->priority); //care the length of the data
			seccalctkipmic(mickey,pframe,payload, datalen ,&miccode[0],(unsigned char)prxattrib->priority); //care the length of the data
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
				if(prxattrib->bdecrypted ==_TRUE)
                                {
					handle_tkip_mic_err(adapter,(u8)IS_MCAST(prxattrib->ra));
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" mic error :prxattrib->bdecrypted=%d ",prxattrib->bdecrypted));
                                }
				else
                                {
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" mic error :prxattrib->bdecrypted=%d ",prxattrib->bdecrypted));
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" mic error :prxattrib->bdecrypted=%d ",prxattrib->bdecrypted));
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
			recvframe_pull_tail(precvframe, 8);	
		}
		else{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvframe_chkmic: get_stainfo==NULL!!!\n"));
		}
	}
exit:	
_func_exit_;
	return res;
}

//decrypt and set the ivlen,icvlen of the recv_frame
union recv_frame * decryptor(_adapter *padapter,union recv_frame *precv_frame)
{

	struct rx_pkt_attrib *prxattrib = &precv_frame->u.hdr.attrib;
	struct security_priv *psecuritypriv=&padapter->securitypriv;
	union recv_frame *return_packet=precv_frame;
	
_func_enter_;		

	RT_TRACE(_module_rtl871x_recv_c_,_drv_notice_,("prxstat->decrypted=%x prxattrib->encrypt = 0x%03x \n",prxattrib->bdecrypted,prxattrib->encrypt));

       if((prxattrib->encrypt>0) && ((prxattrib->bdecrypted==0) ||(psecuritypriv->sw_decrypt==_TRUE)))
	{
		psecuritypriv->hw_decrypted=_FALSE;
		
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("prxstat->decrypted==0 psecuritypriv->hw_decrypted=_FALSE"));
		
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("perfrom software decryption! \n"));
		
		//printk("perfrom software decryption!\n");
		RT_TRACE(_module_rtl871x_recv_c_,_drv_notice_,("###  software decryption!\n"));
		switch(prxattrib->encrypt){
		case _WEP40_:
		case _WEP104_:
			wep_decrypt(padapter, (u8 *)precv_frame);
			break;
		case _TKIP_:
			tkip_decrypt(padapter, (u8 *)precv_frame);
			break;
		case _AES_:
			aes_decrypt(padapter, (u8 * )precv_frame);
			break;
		default:
				break;
		}
	}	
	else if(prxattrib->bdecrypted==1)
	{
#if 0	
		if((prxstat->icv==1)&&(prxattrib->encrypt!=_AES_))
		{
			psecuritypriv->hw_decrypted=_FALSE;
			
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("psecuritypriv->hw_decrypted=_FALSE"));
			
			free_recvframe(precv_frame, &padapter->recvpriv.free_recv_queue);
			
			return_packet=NULL;
			
		}
		else
#endif			
		{
			psecuritypriv->hw_decrypted=_TRUE;
			RT_TRACE(_module_rtl871x_recv_c_,_drv_notice_,("### psecuritypriv->hw_decrypted=_TRUE"));

		}
	}
	
	//recvframe_chkmic(adapter, precv_frame);   //move to recvframme_defrag function
	
_func_exit_;		
	
	return return_packet;
	
}
//###set the security information in the recv_frame
union recv_frame * portctrl(_adapter *adapter,union recv_frame * precv_frame)
{
	u8   *psta_addr,*ptr;
	uint  auth_alg;	
	struct recv_frame_hdr *pfhdr;
	struct sta_info * psta;
	struct	sta_priv *pstapriv ;			
	union recv_frame * prtnframe;
	u16	ether_type=0;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	
_func_enter_;			

	pstapriv = &adapter->stapriv;
	ptr=get_recvframe_data(precv_frame);
	pfhdr=&precv_frame->u.hdr;
	psta_addr=pfhdr->attrib.ta;
	psta=get_stainfo(pstapriv, psta_addr);
	
	auth_alg=adapter->securitypriv.dot11AuthAlgrthm;
	
	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:adapter->securitypriv.dot11AuthAlgrthm= 0x%d\n",adapter->securitypriv.dot11AuthAlgrthm));
	
	if(auth_alg==2)
	{
		
	  if((psta!=NULL)&&( psta->ieee8021x_blocked)){
	  	
		//blocked
		//only accept EAPOL frame
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:psta->ieee8021x_blocked==1\n"));
		
		prtnframe=precv_frame;
		
				//get ether_type
		ptr=ptr+pfhdr->attrib.hdrlen+pfhdr->attrib.iv_len+LLC_HEADER_SIZE;
		 _memcpy(&ether_type,ptr, 2);
		 ether_type= ntohs((unsigned short )ether_type);
		 
		if(ether_type == 0x888e){
			prtnframe=precv_frame;
		}
		else{ 

			//free this frame
			free_recvframe(precv_frame, &adapter->recvpriv.free_recv_queue);
			prtnframe=NULL;
		}			
	  }
	  else
	  { 
	  	//allowed
	  	//check decryption status, and decrypt the frame if needed	
	  	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:psta->ieee8021x_blocked==0\n"));
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("portctrl:precv_frame->hdr.attrib.privacy=%x\n",precv_frame->u.hdr.attrib.privacy));

		//prxstat=(struct recv_stat *)(precv_frame->u.hdr.rx_head);	
		if(pattrib->bdecrypted==0)
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("portctrl:prxstat->decrypted=%x\n", pattrib->bdecrypted));

	  	prtnframe=precv_frame;
  		//check is the EAPOL frame or not (Rekey)
		if(ether_type == 0x888e){

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
	struct	security_priv	*psecuritypriv = &adapter->securitypriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = myid(&adapter->eeprompriv);
	u8 * sta_addr = NULL;

	sint bmcast = IS_MCAST(pattrib->dst);
	
_func_enter_;		

 	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)) 
	{

           // filter packets that SA is myself or multicast or broadcast
	    if (_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
		  RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" SA==myself \n"));
		  ret= _FAIL;
		  goto exit;
	    }		
        
	    if( (!_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))	&& (!bmcast) ){
		  ret= _FAIL;
		  goto exit;
	    	}  
		
	    if( _memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		   _memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		   (!_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) ) {
			ret= _FAIL;
			goto exit;
	    	}
	    	
	    sta_addr = pattrib->src;			
		
	}
	else if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
	{	
	      // For Station mode, sa and bssid should always be BSSID, and DA is my mac-address
		if(!_memcmp(pattrib->bssid, pattrib->src, ETH_ALEN) )
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
			if(!_memcmp(pattrib->bssid, pattrib->dst, ETH_ALEN)) {
				ret= _FAIL;
				goto exit;
			}

	       	sta_addr = pattrib->src;			
		
		}
		
	  }
	 else if(check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
	 {
              _memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
	       _memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
	       _memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
  	       _memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

              sta_addr = mybssid;	 
	  }
	 else
	 {
	 	ret  = _FAIL;
	 }


	 
	if(bmcast)
		*psta = get_bcmc_stainfo(adapter);
	else
		*psta = get_stainfo(pstapriv, sta_addr); // get ap_info

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


sint ap2sta_data_frame(
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
#ifndef CONFIG_DRVEXT_MODULE			
		&& (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) 
#endif			
		)	
	{
	
	       // if NULL-frame, drop packet
	      if ((GetFrameSubType(ptr)) == WIFI_DATA_NULL)
	      {
		   	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,(" NULL frame \n"));	
			ret= _FAIL;
			goto exit;
	           
	       }
		   
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
	       if (_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
		     RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" SA==myself \n"));
			ret= _FAIL;
			goto exit;
		}	

		// da should be for me  
              if((!_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))&& (!bmcast))
              {
                  RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,(" ap2sta_data_frame:  compare DA fail; DA= %x:%x:%x:%x:%x:%x \n",
					pattrib->dst[0],
					pattrib->dst[1],
					pattrib->dst[2],
					pattrib->dst[3],
					pattrib->dst[4],
					pattrib->dst[5]));
				   
				ret= _FAIL;
				goto exit;
              }
			  
		
		// check BSSID
		if( _memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		     _memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		     (!_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) )
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
                   
			ret= _FAIL;
			goto exit;
			}	

		if(bmcast)
			*psta = get_bcmc_stainfo(adapter);
		else
		       *psta = get_stainfo(pstapriv, pattrib->bssid); // get ap_info

		if (*psta == NULL) {
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("ap2sta: can't get psta under STATION_MODE ; drop pkt\n"));
			ret= _FAIL;
			goto exit;
		}

	}
       else if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) && 
		     (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) )
	{      
	       _memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
	       _memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
	       _memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
  	       _memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

		//
		_memcpy(pattrib->bssid,  mybssid, ETH_ALEN);
		  
		  
		   *psta = get_stainfo(pstapriv, pattrib->bssid); // get sta_info
		if (*psta == NULL) {
		       RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("can't get psta under MP_MODE ; drop pkt\n"));
			ret= _FAIL;
			goto exit;
		}

	
	}
	else
	{
		ret =  _FAIL;
	}
	
exit:	
	
_func_exit_;	

	return ret;

}




sint sta2ap_data_frame(
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
	unsigned char *myhwaddr = myid(&adapter->eeprompriv);
	sint ret=_SUCCESS;
	//sint bmcast = IS_MCAST(pattrib->dst);

_func_enter_;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{ 

#if 0
		if (bmcast)
		{
			// For AP mode, if DA == MCAST, then BSSID should be also MCAST
			//bssid = mc-addr => psta=NULL

			//???
			//if (!IS_MCAST(pattrib->bssid)){
			//	ret= _FAIL;
			//	goto exit;
			//}
			
			*psta = get_bcmc_stainfo(adapter);
			
		}
		else // not mc-frame
#endif			
		{
		
			//??? For AP mode, if DA is non-MCAST, then it must be BSSID, and bssid == BSSID
			//???if( (!_memcmp(mybssid, pattrib->dst, ETH_ALEN)) || 
			//For AP mode, RA=BSSID, TX=STA(SRC_ADDR), A3=DST_ADDR
			if(!_memcmp(pattrib->bssid, mybssid, ETH_ALEN))
			{
					ret= _FAIL;
					goto exit;
				}

			*psta = get_stainfo(pstapriv, pattrib->src);

			if (*psta == NULL)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("can't get psta under AP_MODE; drop pkt\n"));
				ret= _FAIL;
				goto exit;
			}
			
		}

	}
	
exit:
	
_func_exit_;	

	return ret;

}

sint validate_recv_ctrl_frame(_adapter *adapter, union recv_frame *precv_frame)
{
	RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("+validate_recv_ctrl_frame\n"));
	
	return _FAIL;
}

sint validate_recv_mgnt_frame(_adapter *adapter, union recv_frame *precv_frame)
{

#ifdef CONFIG_MLME_EXT

	RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("+validate_recv_mgnt_frame\n"));
	
	mgt_dispatcher(adapter, precv_frame->u.hdr.rx_data, precv_frame->u.hdr.len);

#endif
	
	return _FAIL;

}


sint validate_recv_data_frame(_adapter *adapter, union recv_frame *precv_frame)
{
	int res;	
	u8 bretry;
	u8	*psa, *pda, *pbssid;
	struct	sta_info	*psta = NULL;
	
	u8 *ptr = precv_frame->u.hdr.rx_data;
	uint	frtype = GetFrameType(ptr);
	u8 *myhwaddr = myid(&adapter->eeprompriv);

	struct	rx_pkt_attrib	*pattrib = & precv_frame->u.hdr.attrib;
	struct	sta_priv 		*pstapriv = &adapter->stapriv;	
	struct	security_priv	*psecuritypriv = &adapter->securitypriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	NDIS_WLAN_BSSID_EX  *pcur_bss = &pmlmepriv->cur_network.network;
	sint	ret=_SUCCESS;

_func_enter_;	

	bretry = GetRetry(ptr);
	pda = get_da(ptr);
	psa = get_sa(ptr);
	pbssid = get_hdr_bssid(ptr);

#if 0
{
	u8 psaddr[6] = {0x00, 0xe0, 0x4c, 0x87, 0x12, 0x22};
	_memcpy(psa, psaddr, ETH_ALEN);
}
#endif



	if(pbssid == NULL){
		ret= _FAIL;
		goto exit;
	}

	_memcpy(pattrib->dst, pda, ETH_ALEN);
	_memcpy(pattrib->src, psa, ETH_ALEN);
	
	_memcpy(pattrib->bssid, pbssid, ETH_ALEN);

	switch(pattrib->to_fr_ds)
	{
		case 0:
			_memcpy(pattrib->ra, pda, ETH_ALEN);
			_memcpy(pattrib->ta, psa, ETH_ALEN);
			res= sta2sta_data_frame(adapter, precv_frame, &psta);
			break;

		case 1:				
			_memcpy(pattrib->ra, pda, ETH_ALEN);
			_memcpy(pattrib->ta, pbssid, ETH_ALEN);
			res= ap2sta_data_frame(adapter, precv_frame, &psta);
			break;

		case 2:
			_memcpy(pattrib->ra, pbssid, ETH_ALEN);
			_memcpy(pattrib->ta, psa, ETH_ALEN);
			res= sta2ap_data_frame(adapter, precv_frame, &psta);
			break;
		
		case 3:	
			_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
			_memcpy(pattrib->ta, GetAddr2Ptr(ptr), ETH_ALEN);
                     res=_FAIL;
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" case 3\n"));
			break;		
			
		default:
			res=_FAIL;			
			break;			

	}

	if(res==_FAIL){
		//RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,(" after to_fr_ds_chk; res = fail \n"));
		ret= res;
		goto exit;
	}	

#if 0
	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE))
	{
		psta = get_stainfo(pstapriv, pattrib->bssid); // get sta_info
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n pattrib->bssid=0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
		pattrib->bssid[0],pattrib->bssid[1],pattrib->bssid[2],pattrib->bssid[3],pattrib->bssid[4],pattrib->bssid[5]));
		
	}
#endif
	
	if(psta==NULL){
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" after to_fr_ds_chk; psta==NULL \n"));
		ret= _FAIL;
		goto exit;
	}
	else
	{	
		//psta->rssi = prxcmd->rssi;
		//psta->signal_quality= prxcmd->sq;
		precv_frame->u.hdr.psta = psta;
	}	
	
	if(pcur_bss!=NULL){
		//pcur_bss->Rssi =  	prxcmd->rssi;
	}

	pattrib->amsdu=0;
	//parsing QC field
	if(pattrib->qos == 1)
	{
		pattrib->priority = GetPriority((ptr + 24));
		pattrib->ack_policy =GetAckpolicy((ptr + 24));
		pattrib->amsdu = GetAMsdu((ptr + 24));		
		pattrib->hdrlen = pattrib->to_fr_ds==3 ? 32 : 26;
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
		ret= _FAIL;
		goto exit;
	}

	if(pattrib->privacy){
		
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("validate_recv_data_frame:pattrib->privacy=%x\n", pattrib->privacy));		
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n ^^^^^^^^^^^IS_MCAST(pattrib->ra(0x%02x))=%d^^^^^^^^^^^^^^^6\n", pattrib->ra[0],IS_MCAST(pattrib->ra)));
		
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

_func_enter_;

	
#if 0
MSG_8712("\n");
{
	int i;
	for(i=0; i<64;i=i+8)
		MSG_8712("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:", *(ptr+i),
		*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));

}	
MSG_8712("\n");	
#endif	

	//add version chk
	if(ver!=0){
		 RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n  validate_recv_data_frame fail! (ver!=0)\n"));
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
	pattrib->privacy =  GetPrivacy(ptr);
	pattrib->order = GetOrder(ptr);



	switch(type)
	{
		case WIFI_MGT_TYPE: //mgnt
			  retval=validate_recv_mgnt_frame(adapter, precv_frame);
			  if (retval==_FAIL)
                          {
			  RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n  validate_recv_mgnt_frame fail\n"));
                          }
			  break;
		case WIFI_CTRL_TYPE://ctrl
			  retval=validate_recv_ctrl_frame(adapter, precv_frame);
			   if (retval==_FAIL)
			  {
			  RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n  validate_recv_ctrl_frame fail\n"));
                          }
			  break;
		case WIFI_DATA_TYPE: //data 
			pattrib->qos = (subtype & BIT(7))? 1:0;
			retval=validate_recv_data_frame(adapter, precv_frame);
			if (retval==_FAIL)
                        {
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("  validate_recv_data_frame fail\n"));
                        }
			break;
		default:
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("  validate_recv_data_frame fail! type=0x%x\n", type));
			  retval=_FAIL;
			  break;
			
	}
	
exit:
	
_func_exit_;	

	return retval;

}

#if 1
sint wlanhdr_to_ethhdr ( union recv_frame *precvframe)
{
	//remove the wlanhdr and add the eth_hdr
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
	if((_memcmp(psnap, rtw_rfc1042_header, SNAP_SIZE) &&
		(_memcmp(psnap_type, SNAP_ETH_TYPE_IPX, 2) == _FALSE) && 
		(_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_AARP, 2)==_FALSE) )||
		//eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
		 _memcmp(psnap, rtw_bridge_tunnel_header, SNAP_SIZE)){
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
		_memcpy(ptr, get_rxmem(precvframe), 24);
		ptr+=24;
	}
	else {
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+ (bsnaphdr?2:0)));
	}

	_memcpy(ptr, pattrib->dst, ETH_ALEN);
	_memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

	if(!bsnaphdr) {
		len = htons(len);
		_memcpy(ptr+12, &len, 2);
	}

exit:	
_func_exit_;	
	return ret;

}
#else
sint wlanhdr_to_ethhdr ( union recv_frame *precvframe)
{
	//remove the wlanhdr and add the eth_hdr

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
	 if (psnap->dsap==0xaa && psnap->ssap==0xaa && psnap->ctrl==0x03) {
		 if ( _memcmp(psnap->oui, oui_rfc1042, WLAN_IEEE_OUI_LEN)) {
   			bsnaphdr=_TRUE;//wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_RFC1042;
  		}
  		else if (_memcmp(psnap->oui, SNAP_HDR_APPLETALK_DDP, WLAN_IEEE_OUI_LEN) &&
     			_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_DDP, 2) )
    			bsnaphdr=_TRUE;	//wlan_pkt_format = WLAN_PKT_FORMAT_APPLETALK;
  		else if (_memcmp( psnap->oui, oui_8021h, WLAN_IEEE_OUI_LEN))
   			bsnaphdr=_TRUE;	//wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_TUNNEL;
  		else {
  			 RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("drop pkt due to invalid frame format!\n"));
   			ret= _FAIL;
			goto exit;
 		 }
 	}
	 else
  		bsnaphdr=_FALSE;//wlan_pkt_format = WLAN_PKT_FORMAT_OTHERS;

	rmv_len = pattrib->hdrlen + pattrib->iv_len +(bsnaphdr?SNAP_SIZE:0);
	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n===pattrib->hdrlen: %x,  pattrib->iv_len:%x ===\n\n", pattrib->hdrlen,  pattrib->iv_len));
	
	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE))	   	
       {
	   ptr += rmv_len ;	
          *ptr = 0x87;
	   *(ptr+1) = 0x12;
	   
	    //back to original pointer	 
	    ptr -= rmv_len;          
        } 
	
	ptr += rmv_len ;

	_memcpy(&eth_type, ptr, 2);
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
		struct iphdr*  piphdr = (struct iphdr*) ptr;
		//__u8 tos = (unsigned char)(pattrib->priority & 0xff);

		//piphdr->tos = tos;

		if (piphdr->protocol == 0x06)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("@@@===recv tcp len:%d @@@===\n", precvframe->u.hdr.len));
		}
	}
	else if(eth_type==0x8712)// append rx status for mp test packets
	{
	       //ptr -= 16;
	       //_memcpy(ptr, get_rxmem(precvframe), 16);		 
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
	       _memcpy(ptr, get_rxmem(precvframe), 24);		
             ptr+=24;			  
      	}
       else
	ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2));

	_memcpy(ptr, pattrib->dst, ETH_ALEN);
	_memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

	eth_type = htons((unsigned short)eth_type) ;
	_memcpy(ptr+12, &eth_type, 2);
exit:	
_func_exit_;	
	return ret;

}
#endif

s32 recv_entry(union recv_frame *precvframe)
{
	_adapter *padapter;
	struct recv_priv *precvpriv;
	struct	mlme_priv	*pmlmepriv ;
	 struct dvobj_priv *pdev;		
	struct recv_stat *prxstat; 
	 u8 *phead, *pdata, *ptail,*pend;    

	_queue *pfree_recv_queue, *ppending_recv_queue;
	u8 blk_mode = _FALSE;
	s32 ret=_SUCCESS;	
	struct intf_hdl * pintfhdl;

_func_enter_;

	//RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("+recv_entry\n"));

	padapter=precvframe->u.hdr.adapter;
	pintfhdl=&padapter->pio_queue->intf;

	pmlmepriv = &padapter->mlmepriv;
	precvpriv = &(padapter->recvpriv);
	pdev=&padapter->dvobjpriv;	
	pfree_recv_queue = &(precvpriv->free_recv_queue);
	ppending_recv_queue = &(precvpriv->recv_pending_queue);

	phead=precvframe->u.hdr.rx_head;
	pdata=precvframe->u.hdr.rx_data;
	ptail=precvframe->u.hdr.rx_tail;
	pend=precvframe->u.hdr.rx_end;  
	prxstat=(struct recv_stat *)phead;	

	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_RX);

#ifdef CONFIG_SDIO_HCI
	if(precvpriv->free_recvframe_cnt >1)
#endif		
	{
#ifdef CONFIG_RECV_THREAD_MODE
 		if(_queue_empty(ppending_recv_queue) == _TRUE)
		{		
			//enqueue_recvframe_usb(precvframe, ppending_recv_queue);//enqueue to recv_pending_queue
		 	enqueue_recvframe(precvframe, ppending_recv_queue);
			_up_sema(&precvpriv->recv_sema);
		}
		else 	
		{	
			//enqueue_recvframe_usb(precvframe, ppending_recv_queue);//enqueue to recv_pending_queue
			 enqueue_recvframe(precvframe, ppending_recv_queue);
		}	
#else

#ifdef CONFIG_RECV_BH
		#ifdef PLATFORM_LINUX
		
		if(_queue_empty(ppending_recv_queue) == _TRUE)
		{
			enqueue_recvframe(precvframe, ppending_recv_queue);
			tasklet_hi_schedule(&precvpriv->recv_tasklet);
		}
		else
		{
			enqueue_recvframe(precvframe, ppending_recv_queue);
		}
		
		#endif
#else
		if((ret = recv_func(padapter, precvframe)) == _FAIL)
		{	
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recv_entry: recv_func return fail!!!\n"));
			goto _recv_entry_drop;
		}	
#endif		

#endif
	   	precvpriv->rx_pkts++;
		
	}
#ifdef CONFIG_SDIO_HCI	
	else
		goto _recv_entry_drop;
#endif	


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

