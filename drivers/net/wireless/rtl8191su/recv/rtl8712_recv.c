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
#define _RTL8712_RECV_C_
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

extern u8* g_pallocated_recv_buf;

int	init_recv_priv(struct recv_priv *precvpriv, _adapter *padapter)
{
	int i;
	struct recv_buf *precvbuf;
	//struct recv_reorder_ctrl *preorder_ctrl;
	int	res=_SUCCESS;

	
	_init_sema(&precvpriv->recv_sema, 0);//will be removed
	_init_sema(&precvpriv->terminate_recvthread_sema, 0);//will be removed
	
      //init recv_buf
      _init_queue(&precvpriv->free_recv_buf_queue);	  


      precvpriv->pallocated_recv_buf = g_pallocated_recv_buf;
      if(precvpriv->pallocated_recv_buf==NULL){
		res= _FAIL;
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("alloc recv_buf fail!\n"));
		goto exit;
      }
      _memset(precvpriv->pallocated_recv_buf, 0, NR_RECVBUFF *sizeof(struct recv_buf) + 4);

      precvpriv->precv_buf = precvpriv->pallocated_recv_buf + 4 -
							((uint) (precvpriv->pallocated_recv_buf) &(4-1));


      precvbuf = (struct recv_buf*)precvpriv->precv_buf;

      for(i=0; i < NR_RECVBUFF ; i++)
     {
	    _init_listhead(&precvbuf->list);

	     _spinlock_init(&precvbuf->recvbuf_lock);	     
	     
	     res = os_recvbuf_resource_alloc(padapter, precvbuf);		 
	     if(res==_FAIL)
		 	break;
		 
	     precvbuf->ref_cnt = 0;
	     precvbuf->adapter =padapter;
  

	     list_insert_tail(&precvbuf->list, &(precvpriv->free_recv_buf_queue.queue));	 
  
	     precvbuf++;
		 
	}
#ifdef CONFIG_SDIO_HCI
	
	precvpriv->recvbuf_drop= (struct recv_buf*)_malloc(sizeof(struct recv_buf));
#ifdef PLATFORM_LINUX
	((struct recv_buf *)precvpriv->recvbuf_drop)->pallocated_buf = _malloc(MAX_RECVBUF_SZ+4);
        if(((struct recv_buf *)precvpriv->recvbuf_drop)->pallocated_buf == NULL){
                res = _FAIL;
        }

        ((struct recv_buf *)precvpriv->recvbuf_drop)->pbuf=((struct recv_buf *)precvpriv->recvbuf_drop)->pallocated_buf + 4 -  ((uint) (((struct recv_buf *)precvpriv->recvbuf_drop)->pallocated_buf) &(4-1));

        ((struct recv_buf *)precvpriv->recvbuf_drop)->pdata = ((struct recv_buf *)precvpriv->recvbuf_drop)->phead = ((struct recv_buf *)precvpriv->recvbuf_drop)->ptail =((struct recv_buf *)precvpriv->recvbuf_drop)->pbuf;

        ((struct recv_buf *)precvpriv->recvbuf_drop)->pend = ((struct recv_buf *)precvpriv->recvbuf_drop)->pdata + MAX_RECVBUF_SZ;

        ((struct recv_buf *)precvpriv->recvbuf_drop)->len = 0;

#else
		os_recvbuf_resource_alloc(padapter, precvpriv->recvbuf_drop);	
#endif
#endif	

	precvpriv->free_recv_buf_queue_cnt = NR_RECVBUFF;
	
#if 0 //move to sta_info when alloc sta_info
	//for A-MPDU Rx reordering buffer control
	for(i=0; i < 16 ; i++)
	{
		preorder_ctrl = &precvpriv->recvreorder_ctrl[i];

		preorder_ctrl->padapter = padapter;
		
		preorder_ctrl->indicate_seq = 0xffff;
		preorder_ctrl->wend_b= 0xffff;       
		//preorder_ctrl->wsize_b = (NR_RECVBUFF-2);
		preorder_ctrl->wsize_b = 16;//64;

		_init_queue(&preorder_ctrl->pending_recvframe_queue);

		init_recv_timer(preorder_ctrl);
	}
#endif

#ifdef PLATFORM_LINUX
//#if defined (CONFIG_USB_HCI) && defined(PLATFORM_LINUX)

#ifdef CONFIG_RECV_TASKLET
	tasklet_init(&precvpriv->recv_tasklet,
	     (void(*)(unsigned long))recv_tasklet,
	     (unsigned long)padapter);
#else
	tasklet_init(&precvpriv->recv_tasklet,
	     (void(*)(unsigned long))recv_bh,
	     (unsigned long)padapter);
#endif

	skb_queue_head_init(&precvpriv->rx_skb_queue);

#ifdef CONFIG_PREALLOC_RECV_SKB
	{
		int i;
		u32 tmpaddr=0;
		int alignment=0;
		struct sk_buff *pskb=NULL;		
			
		skb_queue_head_init(&precvpriv->free_recv_skb_queue);

		for(i=0; i<NR_PREALLOC_RECV_SKB; i++)
		{

	#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
			pskb = dev_alloc_skb(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);			
	#else			
			pskb = netdev_alloc_skb(padapter->pnetdev, MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);	
	#endif

			if(pskb)
			{
				pskb->dev = padapter->pnetdev;

				tmpaddr = (u32)pskb->data;
	        		alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
				skb_reserve(pskb, (RECVBUFF_ALIGN_SZ - alignment));		
				
				skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
			}

			pskb=NULL;
			
		}
		
	}
#endif

#endif
	
exit:
	
	return res;	
	
}

void free_recv_priv (struct recv_priv *precvpriv)
{
	int i;
	struct recv_buf *precvbuf;
	_adapter *padapter = precvpriv->adapter;

	precvbuf = (struct recv_buf *)precvpriv->precv_buf;

	for(i=0; i < NR_RECVBUFF ; i++)
	{ 
		os_recvbuf_resource_free(padapter, precvbuf);
		precvbuf++;	
	}	
		
	if(precvpriv->pallocated_recv_buf)
		precvpriv->pallocated_recv_buf = NULL;


#ifdef PLATFORM_LINUX

	if (skb_queue_len(&precvpriv->rx_skb_queue)) {
		printk(KERN_WARNING "rx_skb_queue not empty\n");
	}

	skb_queue_purge(&precvpriv->rx_skb_queue);

#ifdef CONFIG_PREALLOC_RECV_SKB

	if (skb_queue_len(&precvpriv->free_recv_skb_queue)) {
		printk(KERN_WARNING "free_recv_skb_queue not empty, %d\n", skb_queue_len(&precvpriv->free_recv_skb_queue));
	}

	skb_queue_purge(&precvpriv->free_recv_skb_queue);

#endif
	
#endif	

}

int init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf)
{
	int res=_SUCCESS;
#ifdef CONFIG_USB_HCI
	precvbuf->transfer_len = 0;

	precvbuf->len = 0;

	precvbuf->ref_cnt = 0;
	
#endif //#ifdef CONFIG_USB_HCI
	if(precvbuf->pbuf)
	{
		precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pbuf;
		precvbuf->pend = precvbuf->pdata + MAX_RECVBUF_SZ;
	}	

	return res;
	
}

void init_recvframe(union recv_frame *precvframe, struct recv_priv *precvpriv)
{		
	struct recv_buf *precvbuf = precvframe->u.hdr.precvbuf;
	
	/* Perry: This can be removed */
	_init_listhead(&precvframe->u.hdr.list);

	precvframe->u.hdr.len=0;
	
#ifdef PLATFORM_LINUX

	if(precvbuf)
	{		
		if(precvbuf->pskb)
		{
			precvframe->u.hdr.pkt = skb_clone(precvbuf->pskb, GFP_ATOMIC);			
		}	
	}
	
#endif	     

}

int free_recvframe(union recv_frame *precvframe, _queue *pfree_recv_queue)
{
	_irqL irqL;
	_adapter *padapter=precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	
_func_enter_;

	
#ifdef PLATFORM_WINDOWS
	os_read_port(padapter, precvframe->u.hdr.precvbuf);
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
        _enter_critical_ex(&precvbuf->recvbuf_lock, &irql);

        precvbuf->ref_cnt--;
	 if(precvbuf->ref_cnt == 0 ){
                _enter_critical(&precvpriv->free_recv_buf_queue.lock, &irqL);
                list_delete(&(precvbuf->list));
                list_insert_tail(&(precvbuf->list), get_list_head(&precvpriv->free_recv_buf_queue));
                precvpriv->free_recv_buf_queue_cnt++;
                _exit_critical(&precvpriv->free_recv_buf_queue.lock, &irqL);
                RT_TRACE(_module_rtl871x_recv_c_,_drv_notice_,("os_read_port: precvbuf=0x%p enqueue:precvpriv->free_recv_buf_queue_cnt=%d\n",precvbuf,precvpriv->free_recv_buf_queue_cnt));
        }
                RT_TRACE(_module_rtl871x_recv_c_,_drv_notice_,("os_read_port: precvbuf=0x%p enqueue:precvpriv->free_recv_buf_queue_cnt=%d\n",precvbuf,precvpriv->free_recv_buf_queue_cnt));
 _exit_critical_ex(&precvbuf->recvbuf_lock, &irql);
	}
}
#endif
#endif

	_enter_critical(&pfree_recv_queue->lock, &irqL);
	//_spinlock_ex(&pfree_recv_queue->lock);
	
	list_delete(&(precvframe->u.hdr.list));	
	
	list_insert_tail(&(precvframe->u.hdr.list), get_list_head(pfree_recv_queue));

	if(padapter !=NULL){			
		if(pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt++;
	}
	
      //_spinunlock_ex(&pfree_recv_queue->lock);
      _exit_critical(&pfree_recv_queue->lock, &irqL);
	
_func_exit_;	

	return _SUCCESS;
	
}

void update_recvframe_attrib_from_recvstat(struct rx_pkt_attrib *pattrib, struct recv_stat *prxstat)
{
	u32 *pphy_info;
	struct phy_stat *pphy_stat;
	u16 drvinfo_sz=0;
	drvinfo_sz = (le32_to_cpu(prxstat->rxdw0)&0x000f0000)>>16;
	drvinfo_sz = drvinfo_sz<<3;

	//TODO:	
	//Offset 0
	pattrib->bdecrypted = ((le32_to_cpu(prxstat->rxdw0) & BIT(27)) >> 27)? 0:1;

#ifdef CONFIG_MP_INCLUDED 	
	pattrib->crc_err = ((le32_to_cpu(prxstat->rxdw0) & BIT(14)) >> 14 );
#endif

	//Offset 4


	//Offset 8
	

	//Offset 12
#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_RX
	if ( le32_to_cpu(prxstat->rxdw3) & BIT(13)) {
		pattrib->tcpchk_valid = 1; // valid
		if ( le32_to_cpu(prxstat->rxdw3) & BIT(11) ) {
			pattrib->tcp_chkrpt = 1; // correct
			//printk("tcp csum ok\n");
		} else
			pattrib->tcp_chkrpt = 0; // incorrect

		if ( le32_to_cpu(prxstat->rxdw3) & BIT(12) )
			pattrib->ip_chkrpt = 1; // correct
		else
			pattrib->ip_chkrpt = 0; // incorrect

	} else {
		pattrib->tcpchk_valid = 0; // invalid
	}	

#endif
	pattrib->mcs_rate=(u8)(( le32_to_cpu(prxstat->rxdw3))&0x3f);
	pattrib->htc=(u8) ( ( le32_to_cpu(prxstat->rxdw3) >>14)&0x1);
	//Offset 16


	//Offset 20
	
	
	//phy_info
	if(drvinfo_sz)
	{
		pphy_stat =(struct phy_stat *)(prxstat+1);
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n prxstat=%p  pphy_stat=%p \n",prxstat,pphy_stat));
		pphy_info=(u32 *)prxstat+1;
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n prxstat=%p  pphy_info=%p \n",prxstat,pphy_info));
		//printk("pphy_info, of0=0x%08x\n", *pphy_info);
		//printk("pphy_info, of1=0x%08x\n", *(pphy_info+1));
		//printk("pphy_info, of2=0x%08x\n", *(pphy_info+2));
		//printk("pphy_info, of3=0x%08x\n", *(pphy_info+3));		
		//printk("pphy_info, of4=0x%08x\n", *(pphy_info+4));
		//printk("pphy_info, of5=0x%08x\n", *(pphy_info+5));
		//printk("pphy_info, of6=0x%08x\n", *(pphy_info+6));
		//printk("pphy_info, of7=0x%08x\n", *(pphy_info+7));		

	}
	
	
}

//perform defrag
union recv_frame * recvframe_defrag(_adapter *adapter,_queue *defrag_q)
{
	_list	 *plist, *phead;	
	u8	*data,wlanhdr_offset;
	u8   curfragnum;
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
	list_delete(&(prframe->u.list));

	if(curfragnum!=pfhdr->attrib.frag_num)
	{
		//the first fragment number must be 0
		//free the whole queue
		free_recvframe(prframe, pfree_recv_queue);
		free_recvframe_queue(defrag_q, pfree_recv_queue); 
		
		return NULL;
	}
	
	curfragnum++;
	
	plist= get_list_head(defrag_q);

	plist = get_next(plist);

	data=get_recvframe_data(prframe);

	while(end_of_queue_search(phead, plist) == _FALSE)
	{			
		pnextrframe = LIST_CONTAINOR(plist, union recv_frame , u);
		pnfhdr=&pnextrframe->u.hdr;

		
		//check the fragment sequence  (2nd ~n fragment frame)
		
		if(curfragnum!=pnfhdr->attrib.frag_num)
		{
			//the fragment number must be increasing  (after decache) 
			//release the defrag_q & prframe
			free_recvframe(prframe, pfree_recv_queue);  
			free_recvframe_queue(defrag_q, pfree_recv_queue);  
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
		_memcpy(pfhdr->rx_tail, pnfhdr->rx_data, pnfhdr->len);
		
		recvframe_put(prframe, pnfhdr->len);
		
		pfhdr->attrib.icv_len=pnfhdr->attrib.icv_len;
		plist = get_next(plist);
				
	};

	//free the defrag_q queue and return the prframe
	free_recvframe_queue(defrag_q, pfree_recv_queue);  
	
	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("Performance defrag!!!!!\n"));
	
_func_exit_;	

	return prframe;

}  

//check if need to defrag, if needed queue the frame to defrag_q
union recv_frame * recvframe_chk_defrag(_adapter *padapter,union recv_frame* precv_frame)
{
	u8	ismfrag;
	u8	fragnum;
	u8   *psta_addr;
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
	psta=get_stainfo(pstapriv, psta_addr);
	if(psta==NULL)
		pdefrag_q=NULL;
	else	
		pdefrag_q=&psta->sta_recvpriv.defrag_q;

	if((ismfrag==0)&&(fragnum==0))
	{
		prtnframe = precv_frame;//isn't a fragment frame
	}	
	
	if(ismfrag==1)
	{
		//0~(n-1) fragment frame
		//enqueue to defraf_g
	  	if(pdefrag_q != NULL)
		{		
			if(fragnum==0)
			{
				//the first fragment				
				if(_queue_empty(pdefrag_q) == _FALSE)
				{
					//free current defrag_q
					free_recvframe_queue(pdefrag_q, pfree_recv_queue);	
				}	
			}

			
			//Then enqueue the 0~(n-1) fragment into the defrag_q
			
			//_spinlock(&pdefrag_q->lock);
			phead = get_list_head(pdefrag_q);
			list_insert_tail(&pfhdr->list, phead);			
			//_spinunlock(&pdefrag_q->lock);
			
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("Enqueuq: ismfrag = %d, fragnum= %d\n", ismfrag,fragnum));
			
			prtnframe=NULL;
			
		}
		else
		{
			//can't find this ta's defrag_queue, so free this recv_frame 
			free_recvframe(precv_frame, pfree_recv_queue);
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
			//_spinlock(&pdefrag_q->lock);
			phead = get_list_head(pdefrag_q);
			list_insert_tail(&pfhdr->list,phead);
			//_spinunlock(&pdefrag_q->lock);			
		
			//call recvframe_defrag to defrag
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("defrag: ismfrag = %d, fragnum= %d\n", ismfrag, fragnum));
			precv_frame = recvframe_defrag(padapter, pdefrag_q);
			prtnframe=precv_frame;
		
		}
		else
		{
			//can't find this ta's defrag_queue, so free this recv_frame 
			free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe=NULL;
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("Free because pdefrag_q ==NULL: ismfrag = %d, fragnum= %d\n", ismfrag,fragnum));
		}
		
	}

	
	if((prtnframe!=NULL)&&(prtnframe->u.hdr.attrib.privacy))
	{
		//after defrag we must check tkip mic code 
		if(recvframe_chkmic(padapter,  prtnframe)==_FAIL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\nrecvframe_chkmic(padapter,  prtnframe)==_FAIL \n"));
			free_recvframe(prtnframe,pfree_recv_queue);
			prtnframe=NULL;
		}
	}
	
_func_exit_;		

	return prtnframe;
	
}

int recvbuf2recvframe(_adapter *padapter, struct recv_buf *precvbuf)
{
#ifdef CONFIG_USB_HCI
	_irqL	irql;	
	uint pkt_offset;
	u32 transfer_len;
	u16 drvinfo_sz;
#endif
	u8 *pbuf;
	u8 bsumbit = _FALSE;
	uint pkt_len ;
	struct recv_stat *prxstat;	
	u16 pkt_cnt;
	_queue *pfree_recv_queue;
#ifdef CONFIG_SKB_COPY  //compile flag for linux only
	_pkt  *pkt_copy=NULL;
#endif
	union recv_frame *precvframe=NULL; 
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct intf_hdl * pintfhdl=&padapter->pio_queue->intf;

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("+recvbuf2recvframe()\n"));
	
	pfree_recv_queue = &(precvpriv->free_recv_queue);
	
	pbuf = (u8*)precvbuf->pbuf;	
	
	prxstat = (struct recv_stat *)pbuf;	
	
	//pkt_cnt = le16_to_cpu((prxstat->rxdw2>>16)&0x3ffff);
	pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16)&0x3ffff;
	
	//pkt_len = le32_to_cpu(prxstat->rxdw0&0x00003fff); //pkt_len = prxstat->frame_length;	
	pkt_len =  le32_to_cpu(prxstat->rxdw0)&0x00003fff;
	
#ifdef CONFIG_USB_HCI

	transfer_len = precvbuf->transfer_len;

#if 0 //temp remove when disable usb rx aggregation
	if((pkt_cnt > 10) || (pkt_cnt < 1) || (transfer_len<RXDESC_SIZE) ||(pkt_len<=0))
	{		
		return _FAIL;
	}
#endif

	precvbuf->ref_cnt++;
	do{		

		prxstat = (struct recv_stat *)pbuf;		      
	       //pkt_len = le32_to_cpu(prxstat->rxdw0&0x00003fff); //pkt_len = prxstat->frame_length;	
	       pkt_len =  le32_to_cpu(prxstat->rxdw0)&0x00003fff;

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("rxdesc : offsset0:0x%08x, offsset4:0x%08x, offsset8:0x%08x, offssetc:0x%08x\n", 
			prxstat->rxdw0, prxstat->rxdw1, prxstat->rxdw2, prxstat->rxdw4));
	
		//drvinfo_sz = le16_to_cpu((prxstat->rxdw0&0x000f0000)>>16);//uint 2^3 = 8 bytes
		drvinfo_sz = (le32_to_cpu(prxstat->rxdw0)&0x000f0000)>>16;
		drvinfo_sz = drvinfo_sz<<3;


		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("DRV_INFO_SIZE=%d\n", drvinfo_sz));

		if(pkt_len<=0)
		{
			//KeBugCheckEx(0x87120044, 0xe0, 0x4c, 0x87, 0x44);	
	
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe(), pkt_len<=0\n"));
		
			goto  _exit_recvbuf2recvframe;
		}		

		   

		precvframe = alloc_recvframe(pfree_recv_queue);
		if(precvframe==NULL)
		{
			//KeBugCheckEx(0x87120055, 0xe0, 0x4c, 0x87, 0x55);	

			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe(), precvframe==NULL\n"));
			
			goto  _exit_recvbuf2recvframe;
		}

		precvframe->u.hdr.precvbuf = precvbuf;
	
		//pkt_offset = (u16)_RND512(pkt_len + drvinfo_sz + RXDESC_SIZE);
		//pkt_offset = (u16)_RND256(pkt_len + drvinfo_sz + RXDESC_SIZE);
		pkt_offset = (u16)_RND128(pkt_len + drvinfo_sz + RXDESC_SIZE);//TODO: need to check init. setting.
		
#ifdef CONFIG_SKB_COPY  //compile flag for linux only		

	#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
		pkt_copy = dev_alloc_skb(pkt_offset>1600?pkt_offset:1600);
		pkt_copy->dev = padapter->pnetdev;
	#else			
		pkt_copy = netdev_alloc_skb(padapter->pnetdev, pkt_offset>1600?pkt_offset:1600);
	#endif		
		if(pkt_copy==NULL)
		{
			//printk("recvbuf2recvframe:can not allocate memory for skb copy\n");
			init_recvframe(precvframe, precvpriv); 
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pbuf;
			precvframe->u.hdr.rx_end = precvbuf->pend;
			goto _cont;	
		}

		_init_listhead(&precvframe->u.hdr.list);
		precvframe->u.hdr.len=0;
		precvframe->u.hdr.pkt = pkt_copy;		

		_memcpy(pkt_copy->data, pbuf, pkt_offset);			

		precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;

		precvframe->u.hdr.rx_end = pkt_copy->data + (pkt_offset>1600?pkt_offset:1600);
#else
		init_recvframe(precvframe, precvpriv); 
		precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pbuf;
		precvframe->u.hdr.rx_end = precvbuf->pend;

#endif

#ifdef CONFIG_SKB_COPY  //compile flag for linux only
_cont:
#endif

              recvframe_put(precvframe, pkt_len + drvinfo_sz + RXDESC_SIZE);
		recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);	

#ifdef PLATFORM_WINDOWS
		_enter_critical_ex(&precvbuf->recvbuf_lock, &irql);
		precvbuf->ref_cnt++;
		_exit_critical_ex(&precvbuf->recvbuf_lock, &irql);
#endif

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe(), transfer_len=%d, pkt_offset=%d, pkt_len=%d, ref_cnt=%d, precvbuf=0x%x\n",
				transfer_len, pkt_offset, pkt_len, precvbuf->ref_cnt, precvbuf));


		//because the endian issue, driver avoid reference to the rxstat after calling update_recvframe_attrib_from_recvstat();
		update_recvframe_attrib_from_recvstat(&precvframe->u.hdr.attrib, prxstat);		
						
		if( recv_entry(precvframe) != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe(), recv_entry(precvframe) != _SUCCESS\n"));			
		}

		transfer_len -= pkt_offset;
		pbuf += pkt_offset;
		pkt_cnt--;
		precvframe = NULL;

	}while((transfer_len>0) && (pkt_cnt>0));
		
	
_exit_recvbuf2recvframe:

	os_read_port(padapter, precvbuf);
	
#endif //#ifdef CONFIG_USB_HCI	


	return _SUCCESS;
	
}

#ifdef PLATFORM_LINUX
int amsdu_to_msdu(_adapter *padapter, union recv_frame *prframe)
{
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
		nSubframe_Length = *((u16*)(pdata + 12));
		//==m==>change the length order
		nSubframe_Length = (nSubframe_Length>>8) + (nSubframe_Length<<8);

		if( a_len < (ETHERNET_HEADER_SIZE + nSubframe_Length) ) {
			printk("nRemain_Length is %d and nSubframe_Length is : %d\n",a_len,nSubframe_Length);
			goto exit;
		}

		/* move the data point to data content */
		pdata += ETH_HLEN;
		a_len -= ETH_HLEN;

		/* Allocate new skb for releasing to upper layer */
#ifdef CONFIG_SKB_COPY
		sub_skb = dev_alloc_skb(nSubframe_Length + 12);
		skb_reserve(sub_skb, 12);
		data_ptr = (u8 *)skb_put(sub_skb, nSubframe_Length);
		_memcpy(data_ptr, pdata, nSubframe_Length);
#else
		sub_skb = skb_clone(prframe->u.hdr.pkt, GFP_ATOMIC);
		sub_skb->data = pdata;
		sub_skb->len = nSubframe_Length;
		sub_skb->tail = sub_skb->data + nSubframe_Length;
#endif

		//sub_skb->dev = padapter->pnetdev;
		subframes[nr_subframes++] = sub_skb;
		if(nr_subframes >= MAX_SUBFRAME_COUNT) {
			printk("ParseSubframe(): Too many Subframes! Packets dropped!\n");
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
		eth_type = (sub_skb->data[6] << 8) | sub_skb->data[7];
		if (sub_skb->len >= 8 &&
			((_memcmp(sub_skb->data, rtw_rfc1042_header, SNAP_SIZE) &&
			  eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) ||
			 _memcmp(sub_skb->data, rtw_bridge_tunnel_header, SNAP_SIZE) )) {
			/* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
			skb_pull(sub_skb, SNAP_SIZE);
			_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->src, ETH_ALEN);
			_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->dst, ETH_ALEN);
		} else {
			u16 len;
			/* Leave Ethernet header part of hdr and full payload */
			len = htons(sub_skb->len);
			_memcpy(skb_push(sub_skb, 2), &len, 2);
			_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->src, ETH_ALEN);
			_memcpy(skb_push(sub_skb, ETH_ALEN), pattrib->dst, ETH_ALEN);
		}

		/* Indicat the packets to upper layer */
		if (sub_skb) {
			//memset(sub_skb->cb, 0, sizeof(sub_skb->cb));

			sub_skb->protocol = eth_type_trans(sub_skb, padapter->pnetdev);
			sub_skb->dev = padapter->pnetdev;

#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_RX
			if ( (pattrib->tcpchk_valid == 1) && (pattrib->tcp_chkrpt == 1) ) {
				sub_skb->ip_summed = CHECKSUM_UNNECESSARY;
			} else {
				sub_skb->ip_summed = CHECKSUM_NONE;
			}
#else /* !CONFIG_RTL8712_TCP_CSUM_OFFLOAD_RX */
			sub_skb->ip_summed = CHECKSUM_NONE;
#endif

			netif_rx(sub_skb);
		}
	}

exit:

	prframe->u.hdr.len=0;
	free_recvframe(prframe, pfree_recv_queue);//free this recv_frame
	
	return ret;

}

#else

int amsdu_to_msdu(_adapter *padapter, union recv_frame *prframe)
{
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
	
		
		_memcpy(pnrframe->u.hdr.attrib.dst, ptr, ETH_ALEN);
		ptr+=ETH_ALEN;
		_memcpy(pnrframe->u.hdr.attrib.src, ptr, ETH_ALEN);
		ptr+=ETH_ALEN;
		
		_memcpy(&type_len, ptr, 2);
		type_len= ntohs((unsigned short )type_len); 
		ptr +=2;
		mv_len += ETH_HLEN;
	
		recvframe_put(pnrframe, type_len+ETH_HLEN);//update tail;

		if(pnrframe->u.hdr.rx_data >= pnrframe->u.hdr.rx_tail || type_len<8)
		{		
			//panic("pnrframe->u.hdr.rx_data >= pnrframe->u.hdr.rx_tail || type_len<8\n");				
			
			free_recvframe(pnrframe, pfree_recv_queue);
			
			goto exit;
		}	

		psnap=(struct ieee80211_snap_hdr *)(ptr);
		psnap_type=ptr+SNAP_SIZE;
	       if (psnap->dsap==0xaa && psnap->ssap==0xaa && psnap->ctrl==0x03)
		{
		 	if ( _memcmp(psnap->oui, oui_rfc1042, WLAN_IEEE_OUI_LEN))
			{
   				bsnaphdr=_TRUE;//wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_RFC1042;
  			}
  			else if (_memcmp(psnap->oui, SNAP_HDR_APPLETALK_DDP, WLAN_IEEE_OUI_LEN) &&
     					_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_DDP, 2) )
     			{  					
    				bsnaphdr=_TRUE;	//wlan_pkt_format = WLAN_PKT_FORMAT_APPLETALK;
  			}	
  			else if (_memcmp( psnap->oui, oui_8021h, WLAN_IEEE_OUI_LEN))
  			{
   				bsnaphdr=_TRUE;	//wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_TUNNEL;
  			}	
  			else
			{
  				 RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("drop pkt due to invalid frame format!\n"));
   				
				//KeBugCheckEx(0x87123333, 0xe0, 0x4c, 0x87, 0xdd);	

				//panic("0x87123333, 0xe0, 0x4c, 0x87, 0xdd\n");	

				free_recvframe(pnrframe, pfree_recv_queue);
				
				goto exit;
 		 	}
 		}
	 	else
	 	{
  			bsnaphdr=_FALSE;//wlan_pkt_format = WLAN_PKT_FORMAT_OTHERS;
	 	}	

		ptr += (bsnaphdr?SNAP_SIZE:0);
		_memcpy(&eth_type, ptr, 2);
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
			VlanPriInfo.TagHeader.VlanId =  VlanID ;

			VlanPriInfo.TagHeader.CanonicalFormatId = 0; // Should be zero.
			VlanPriInfo.TagHeader.Reserved = 0; // Should be zero.
			NDIS_PER_PACKET_INFO_FROM_PACKET(pnrframe->u.hdr.pkt, Ieee8021QInfo) = VlanPriInfo.Value;	
	
		}
#endif		
		
		pbuf = recvframe_pull(pnrframe, (mv_len-sizeof(struct ethhdr)));
		
		_memcpy(pbuf, pnrframe->u.hdr.attrib.dst, ETH_ALEN);
		_memcpy(pbuf+ETH_ALEN, pnrframe->u.hdr.attrib.src, ETH_ALEN);
		
		eth_type = htons((unsigned short)eth_type) ;
		_memcpy(pbuf+12, &eth_type, 2);

		padding_len = (4) - ((type_len + ETH_HLEN)&(4-1));

                a_len -= (type_len + ETH_HLEN + padding_len) ;

		pnrframe_new = NULL;

//#ifdef CONFIG_SKB_COPY	
#if 0
		if(a_len > ETH_HLEN) 
		{
			pnrframe_new = alloc_recvframe(pfree_recv_queue);
			if(pnrframe_new)
			{						
/*				_pkt *pskb_copy;
				unsigned int copy_len  = pnrframe->u.hdr.len;
			
				_init_listhead(&pnrframe_new->u.hdr.list);

				pnrframe_new->u.hdr.len=0;

		#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
				pskb_copy = dev_alloc_skb(copy_len+64);
		#else			
				pskb_copy = netdev_alloc_skb(padapter->pnetdev, copy_len + 64);
		#endif		
				if(pskb_copy==NULL)
				{			
					printk("amsdu_to_msdu:can not all(ocate memory for skb copy\n");
				}		
			
				pnrframe_new->u.hdr.pkt = pskb_copy;

				_memcpy(pskb_copy->data, pnrframe->u.hdr.rx_data, copy_len);
				
				pnrframe_new->u.hdr.rx_data = pnrframe->u.hdr.rx_data;
				pnrframe_new->u.hdr.rx_tail = pnrframe->u.hdr.rx_data + copy_len;*/

				_pkt *pskb_copy;
			
				_init_listhead(&pnrframe_new->u.hdr.list);

				pnrframe_new->u.hdr.len=0;

		#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
				pskb_copy = dev_alloc_skb(a_len);
		#else			
				pskb_copy = netdev_alloc_skb(padapter->pnetdev, a_len);
		#endif		
				if(pskb_copy==NULL)
				{			
					printk("amsdu_to_msdu:can not all(ocate memory for skb copy\n");
				}		
			
				pnrframe_new->u.hdr.pkt = pskb_copy;

				pdata += (type_len + ETH_HLEN + padding_len);

				_memcpy(pskb_copy->data, pdata, a_len);

				pdata = pskb_copy->data;
				pnrframe_new->u.hdr.rx_head = pskb_copy->head;
				pnrframe_new->u.hdr.rx_data = pnrframe_new->u.hdr.rx_tail = pdata;
				pnrframe_new->u.hdr.rx_end = pskb_copy->end;

			}
			else
			{
				printk("amsdu_to_msdu:can not allocate memory for pnrframe_new\n");
			}
		}

		if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE))
		{
			recv_indicatepkt(padapter, pnrframe);//indicate this recv_frame			
		}
		else
		{
			free_recvframe(pnrframe, pfree_recv_queue);//free this recv_frame
		}

		pnrframe = NULL;
		if(pnrframe_new)
		{
			pnrframe = pnrframe_new;
		}			
			
#else

		if(a_len > ETH_HLEN) 
		{			
			pnrframe_new = alloc_recvframe(pfree_recv_queue);

			if(pnrframe_new)
			{									
				//pnrframe_new->u.hdr.precvbuf = precvbuf;//precvbuf is assigned before call init_recvframe()
				//init_recvframe(pnrframe_new, precvpriv); 	
				{					
						_pkt *pskb = pnrframe->u.hdr.pkt;	  
						_init_listhead(&pnrframe_new->u.hdr.list);

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
				_enter_critical(&precvbuf->recvbuf_lock, &irql);
				precvbuf->ref_cnt++;
				_exit_critical(&precvbuf->recvbuf_lock, &irql);
#endif
				
			}
			else
			{
				//panic("pnrframe_new=%x\n", pnrframe_new);
			}
		}	

		if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE) )
		{
			recv_indicatepkt(padapter, pnrframe);//indicate this recv_frame	
		}
		else
		{			
			free_recvframe(pnrframe, pfree_recv_queue);//free this recv_frame
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

}
#endif

void rxcmd_event_hdl(_adapter *padapter, void *prxcmdbuf)
{	
	uint voffset;
	u8	*poffset;

#ifdef CONFIG_RECV_BH
	u8	*pevtcmd;
	struct evt_obj *pc2h;	
#endif

//	u8	*prspbuf;

	u16 pkt_len, cmd_len, drvinfo_sz;
	u8 eid, cmd_seq;	
	struct recv_stat *prxstat;
	struct evt_priv *pevtpriv = &(padapter->evtpriv);
	struct cmd_priv *pcmdpriv = &(padapter->cmdpriv);


	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("+rxcmd_event_hdl\n"));

	poffset = (u8*)prxcmdbuf;
	voffset = *(uint*)poffset;
	pkt_len = le32_to_cpu(voffset)&0x00003fff;

	prxstat = (struct recv_stat *)prxcmdbuf;
	drvinfo_sz = ((le32_to_cpu(prxstat->rxdw0)&0x000f0000)>>16);//uint 2^3 = 8 bytes		
	drvinfo_sz = drvinfo_sz<<3;

	poffset +=RXDESC_SIZE + drvinfo_sz;
	do{
		
		voffset  = *(uint*)poffset;
		cmd_len = (u16)(le32_to_cpu(voffset)&0xffff);
		cmd_seq = (u8)((le32_to_cpu(voffset)>>24)&0x7f);
		eid = (u8)((le32_to_cpu(voffset)>>16)&0xff);
	
		{

#ifdef CONFIG_RECV_BH		
			//
			//.2 async_event
			//		
			pc2h = (struct evt_obj*)_malloc(sizeof(struct evt_obj));		
			if(pc2h==NULL){
				break;
			}
						
			_init_listhead(&pc2h->list);

			if(cmd_len<64)
			{
				pevtcmd = (u8*)_malloc(64+8);
			}	
			else
			{				
				pevtcmd = (u8*)_malloc(cmd_len+8);
			}
			
			if(pevtcmd==NULL)
			{
				_mfree((u8 *)pc2h, sizeof(struct evt_obj));
				break;
			}
			
			//_memcpy(pevtcmd, poffset+4, cmd_len);
			_memcpy(pevtcmd, poffset, cmd_len+8);		

			pc2h->evtcode = eid;
			pc2h->evtsz = cmd_len;
			pc2h->parmbuf  = pevtcmd;

			enqueue_evt(pevtpriv, pc2h);//enqueue evt_obj
			
#ifdef PLATFORM_LINUX

			//printk("schedule event_tasklet, pc2h=%p, evtsz=%d\n", pc2h, pc2h->evtsz);

			tasklet_hi_schedule(&pevtpriv->event_tasklet);
#endif

#else
			event_handle(padapter, (uint*)poffset);
#endif

			poffset += (cmd_len + 8);//8 bytes aligment

		}

	}while(le32_to_cpu(voffset)&BIT(31));

}

int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, u16 seq_num)	
{
	u8	wsize = preorder_ctrl->wsize_b;
	u16	wend = (preorder_ctrl->indicate_seq + wsize -1)%4096;

	// Rx Reorder initialize condition.
	if(preorder_ctrl->indicate_seq == 0xffff)
	{		
		preorder_ctrl->indicate_seq = seq_num;

		//DbgPrint("check_indicate_seq, 1st->indicate_seq=%d\n", precvpriv->indicate_seq);
	}	

	//DbgPrint("enter->check_indicate_seq(): IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);

	// Drop out the packet which SeqNum is smaller than WinStart
	if( SN_LESS(seq_num, preorder_ctrl->indicate_seq) )
	{
		//RT_TRACE(COMP_RX_REORDER, DBG_LOUD, ("CheckRxTsIndicateSeq(): Packet Drop! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, NewSeqNum));

		//DbgPrint("CheckRxTsIndicateSeq(): Packet Drop! IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);
		
		return _FALSE;
	}

	//
	// Sliding window manipulation. Conditions includes:
	// 1. Incoming SeqNum is equal to WinStart =>Window shift 1
	// 2. Incoming SeqNum is larger than the WinEnd => Window shift N
	//
	if( SN_EQUAL(seq_num, preorder_ctrl->indicate_seq) )
	{
		preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) % 4096;
	}
	else if(SN_LESS(wend, seq_num))
	{
		//RT_TRACE(COMP_RX_REORDER, DBG_LOUD, ("CheckRxTsIndicateSeq(): Window Shift! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, NewSeqNum));
		//DbgPrint("CheckRxTsIndicateSeq(): Window Shift! IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);
		
		if(seq_num >= (wsize - 1))
			preorder_ctrl->indicate_seq = seq_num + 1 -wsize;
		else
			preorder_ctrl->indicate_seq = 4095 - (wsize - (seq_num +1)) + 1;
	}

	//DbgPrint("exit->check_indicate_seq(): IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);

	return _TRUE;
	
}

int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl, union recv_frame *prframe)
{	

	_list	*phead, *plist;
	union recv_frame *pnextrframe;
	struct rx_pkt_attrib *pnextattrib;
	 _queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;

	//DbgPrint("+enqueue_reorder_recvframe()\n");

	//_enter_critical_ex(&ppending_recvframe_queue->lock, &irql);
	//_spinlock_ex(&ppending_recvframe_queue->lock);


	phead = get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

	
	while(end_of_queue_search(phead, plist) == _FALSE)
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
	//_spinlock_ex(&ppending_recvframe_queue->lock);

	list_delete(&(prframe->u.hdr.list));	
	
	list_insert_tail(&(prframe->u.hdr.list), plist);

	//_spinunlock_ex(&ppending_recvframe_queue->lock);
	//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
	
	
	//RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("InsertRxReorderList(): Pkt insert into buffer!! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, SeqNum));
	return _TRUE;

}

int recv_indicatepkts_in_order(_adapter *padapter, struct recv_reorder_ctrl *preorder_ctrl, int bforced)
{	
//	_irqL irql;
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
	//_spinlock_ex(&ppending_recvframe_queue->lock);
	
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
		if(is_list_empty(phead))
		{
	              // _exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
			//_spinunlock_ex(&ppending_recvframe_queue->lock);
			return _TRUE;
		}
		else
		{
			prframe = LIST_CONTAINOR(plist, union recv_frame, u);
			pattrib = &prframe->u.hdr.attrib;	
			preorder_ctrl->indicate_seq = pattrib->seq_num;
		}
	}

	// Prepare indication list and indication.
	// Check if there is any packet need indicate.
	while(!is_list_empty(phead))
	{
		prframe = LIST_CONTAINOR(plist, union recv_frame, u);
		pattrib = &prframe->u.hdr.attrib;	

		if(!SN_LESS(preorder_ctrl->indicate_seq, pattrib->seq_num))
		{

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
			list_delete(&(prframe->u.hdr.list));
			
			if(SN_EQUAL(preorder_ctrl->indicate_seq, pattrib->seq_num))
			{
				preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;
			}
				
#if 0				
			index++;
			if(index==1)
			{
				//Cancel previous pending timer.
				//PlatformCancelTimer(Adapter, &pTS->RxPktPendingTimer);
				if(bforced!=_TRUE)
				{
				  //printk("_cancel_timer(&preorder_ctrl->reordering_ctrl_timer, &bcancelled);\n");
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
				//printk("recv_indicatepkts_in_order, amsdu!=1, indicate_seq=%d, seq_num=%d\n", preorder_ctrl->indicate_seq, pattrib->seq_num);
				
				if((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE))
				{
					recv_indicatepkt(padapter, prframe);		//indicate this recv_frame	
				}
			}
			else if(pattrib->amsdu==1)
			{			
				if(amsdu_to_msdu(padapter, prframe)!=_SUCCESS)
				{
					free_recvframe(prframe, &precvpriv->free_recv_queue);
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

	//_spinunlock_ex(&ppending_recvframe_queue->lock);
	//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);

/*
	//Release the indication lock and set to new indication step.
	if(bPktInBuf)
	{
		// Set new pending timer.
		//pTS->RxIndicateState = RXTS_INDICATE_REORDER;
		//PlatformSetTimer(Adapter, &pTS->RxPktPendingTimer, pHTInfo->RxReorderPendingTime);
		//printk("_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME)\n");
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

int recv_indicatepkt_reorder(_adapter *padapter, union recv_frame *prframe)
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

#ifdef CONFIG_DRVEXT_MODULE
		retval = drvext_rx_handler(padapter, prframe->u.hdr.rx_data, prframe->u.hdr.len);
		if (retval == _SUCCESS)
		{		
			free_recvframe(prframe, &padapter->recvpriv.free_recv_queue);
			return retval;
		}
#endif
		

		if(pattrib->qos !=1 /*|| pattrib->priority!=0 || IS_MCAST(pattrib->ra)*/)
		{
			if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE) )
			{
				RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@  recv_indicatepkt_reorder -recv_func recv_indicatepkt\n" ));	

				recv_indicatepkt(padapter, prframe);		
				return _SUCCESS;

			}
			else
			{	
				return _FAIL;			
			}
		}
		else if(preorder_ctrl->enable == _FALSE)
		{
			preorder_ctrl->indicate_seq = pattrib->seq_num;

			recv_indicatepkt(padapter, prframe);				

			preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;

			return _SUCCESS;	
		}			
#ifndef CONFIG_RECV_REORDERING_CTRL
		else
		{
			//indicate this recv_frame			
			recv_indicatepkt(padapter, prframe);		
			return _SUCCESS;	
		}
#endif	

	}	
	else if(pattrib->amsdu==1) //temp filter -> means didn't support A-MSDUs in a A-MPDU
	{
		//printk("recv_indicatepkt_reorder():pattrib->amsdu==1\n");
		if(preorder_ctrl->enable == _FALSE)
		{
			preorder_ctrl->indicate_seq = pattrib->seq_num;

			retval = amsdu_to_msdu(padapter, prframe);
		
			preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;

			return retval;	
		}
		else
		{
#ifndef CONFIG_RECV_REORDERING_CTRL

		preorder_ctrl->indicate_seq = pattrib->seq_num;

		retval = amsdu_to_msdu(padapter, prframe);

		preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;

		return retval;		
#endif
		}
		
	}
	else
	{

	}
		
	//printk("start reorder buffer ctrl\n");
		
	_enter_critical_ex(&ppending_recvframe_queue->lock, &irql);

	//s2. check if winstart_b(indicate_seq) needs to been updated		
	if(!check_indicate_seq(preorder_ctrl, pattrib->seq_num))
	{
		//pHTInfo->RxReorderDropCounter++;
		//ReturnRFDList(Adapter, pRfd);
		//RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("RxReorderIndicatePacket() ==> Packet Drop!!\n"));		
		//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
	        //return _FAIL;
                goto _err_exit;
	}


	//s3. Insert all packet into Reorder Queue to maintain its ordering.
	if(!enqueue_reorder_recvframe(preorder_ctrl, prframe))
	{
	       //DbgPrint("recv_indicatepkt_reorder, enqueue_reorder_recvframe fail!\n");
	       //_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
	       //return _FAIL;
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
		_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);	
	}
	else
	{
		_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
		_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);
	}

		
	return _SUCCESS;

_err_exit:
 
        _exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
		
        return _FAIL;


}

void reordering_ctrl_timeout_handler(void *pcontext)
{
	_irqL irql;
	struct recv_reorder_ctrl *preorder_ctrl = (struct recv_reorder_ctrl *)pcontext;
	_adapter *padapter = preorder_ctrl->padapter;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;


	if(padapter->bDriverStopped ||padapter->bSurpriseRemoved)
	{
		return;
	}

	//printk("+reordering_ctrl_timeout_handler()=>\n");

	_enter_critical_ex(&ppending_recvframe_queue->lock, &irql);

	recv_indicatepkts_in_order(padapter, preorder_ctrl, _TRUE);

	_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);

}


int process_recv_indicatepkts(_adapter *padapter, union recv_frame *prframe)
{
	int retval = _SUCCESS;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;	
	
#ifdef CONFIG_80211N_HT

	struct ht_priv	*phtpriv = &pmlmepriv->htpriv;

	if(phtpriv->ht_option==1) //B/G/N Mode
	{
		//prframe->u.hdr.preorder_ctrl = &precvpriv->recvreorder_ctrl[pattrib->priority];
			
		if(recv_indicatepkt_reorder(padapter, prframe)!=_SUCCESS)// including perform A-MPDU Rx Ordering Buffer Control
		{
			if((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE))
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
			return retval;
		}
		
#ifdef CONFIG_DRVEXT_MODULE
		retval = drvext_rx_handler(padapter, prframe->u.hdr.rx_data, prframe->u.hdr.len);
		if (retval == _SUCCESS)
		{		
			free_recvframe(prframe, &padapter->recvpriv.free_recv_queue);
			return retval;
		}
#endif

		
		if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE))
		{
			//indicate this recv_frame
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("@@@@ process_recv_indicatepkts- recv_func recv_indicatepkt\n" ));	
			recv_indicatepkt(padapter, prframe);	

			
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







u8 query_rx_pwr_percentage(s8	antpower)
{
	if ((antpower <= -100) || (antpower >= 20))
	{
		return	0;
	}
	else if (antpower >= 0)
	{
		return	100;
	}
	else
	{
		return	(100+antpower);
	}
	
}



static u8 evm_db2percentage(s8 value)
{
	//
	// -33dB~0dB to 0%~99%
	//
	s8 ret_val;
    
	ret_val = value;
	//ret_val /= 2;

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("EVMdbToPercentage92S Value=%d / %x \n", ret_val, ret_val));
		
	if(ret_val >= 0)
		ret_val = 0;
	if(ret_val <= -33)
		ret_val = -33;

	ret_val = 0 - ret_val;
	ret_val*=3;

	if(ret_val == 99)
		ret_val = 100;

	return(ret_val);
}


s32 signal_scale_mapping(s32 cur_sig )
{
	s32 ret_sig;

#ifdef RTK_DMP_PLATFORM
	// Step 1. Scale mapping.
	if(cur_sig >= 81 && cur_sig <= 100)
	{
		ret_sig = 100; 
	}
	if(cur_sig >= 61 && cur_sig <= 80)
	{
		// mapping to 90~95
		ret_sig = 90 + ((cur_sig - 60) / 4);
	}
	else if(cur_sig >= 41 && cur_sig <= 60)
	{
		// mapping to 78~88
		ret_sig = 78 + ((cur_sig - 40) / 2);
	}
	else if(cur_sig >= 31 && cur_sig <= 40)
	{
		// mapping to 67~76
		ret_sig = 66 + (cur_sig - 30);
	}
	else if(cur_sig >= 21 && cur_sig <= 30)
	{
		// mapping to 55~64
		ret_sig = 54 + (cur_sig - 20);
	}
	else if(cur_sig >= 5 && cur_sig <= 20)
	{
		// mapping to 42~52
		ret_sig = 42 + (((cur_sig - 5) * 2) / 3);
	}
	else if(cur_sig == 4)
	{
		ret_sig = 15;
	}
	else if(cur_sig == 3)
	{
		ret_sig = 13;
	}
	else if(cur_sig == 2)
	{
		ret_sig = 11;
	}
	else if(cur_sig == 1)
	{
		ret_sig = 9;
	}
	else
	{
		ret_sig = cur_sig;
	}
#else
/*
	if(cur_sig >= 51 && cur_sig <= 100)
	{
		ret_sig = 100;
	}
	else if(cur_sig >= 41 && cur_sig <= 50)
	{
		ret_sig = 80 + ((cur_sig - 40)*2);
	}
	else if(cur_sig >= 31 && cur_sig <= 40)
	{
		ret_sig = 66 + (cur_sig - 30);
	}
	else if(cur_sig >= 21 && cur_sig <= 30)
	{
		ret_sig = 54 + (cur_sig - 20);
	}
	else if(cur_sig >= 10 && cur_sig <= 20)
	{
		ret_sig = 42 + (((cur_sig - 10) * 2) / 3);
	}
	else if(cur_sig >= 5 && cur_sig <= 9)
	{
		ret_sig = 22 + (((cur_sig - 5) * 3) / 2);
	}
	else if(cur_sig >= 1 && cur_sig <= 4)
	{
		ret_sig = 6 + (((cur_sig - 1) * 3) / 2);
	}
	else
	{
		ret_sig = cur_sig;
	}
*/

	if(cur_sig >= 70 && cur_sig <= 100)
	{
		ret_sig = 100;
	}
	else if ( cur_sig >= 30 && cur_sig <= 69 )
	{
		ret_sig = 100 - ( ( 70 - cur_sig ) * 2 );
	}
	else
	{
		ret_sig = 10;
	}

#endif
	
	return ret_sig;
}



s32  translate2dbm(_adapter *padapter,u8 signal_strength_idx	)
{
	s32	signal_power; // in dBm.


	// Translate to dBm (x=0.5y-95).
	signal_power = (s32)((signal_strength_idx + 1) >> 1); 
	signal_power -= 95; 

	return signal_power;
}

void query_rx_phy_status(_adapter *padapter,union recv_frame *prframe)
{	

	u8 i,max_spatial_stream,evm;
	struct recv_stat *prxstat=(struct recv_stat *)prframe->u.hdr.rx_head;	
	struct phy_stat *pphy_stat=(struct phy_stat *)(prxstat+1);
	u8 *pphy_head=(u8 *)(prxstat+1);
	s8				rx_pwr[4], rx_pwr_all;
		u8				 pwdb_all;
		u32				rssi,total_rssi=0;
	u8 				bcck_rate=0,rf_rx_num = 0,cck_highpwr = 0;

	
	struct phy_cck_rx_status *pcck_buf;


	// Record it for next packet processing
	bcck_rate=(prframe->u.hdr.attrib.mcs_rate<=3? 1:0);
	if(bcck_rate)
	{
		u8 report;
	
		// CCK Driver info Structure is not the same as OFDM packet.
		pcck_buf = (struct phy_cck_rx_status *)pphy_stat;	
//		Adapter->RxStats.NumQryPhyStatusCCK++;
	
		// 
		// (1)Hardware does not provide RSSI for CCK
		// (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive)
		//
//		if(pHalData->eRFPowerState == eRfOn)
//			cck_highpwr = (u1Byte)pHalData->bCckHighPower;
//		else
//			cck_highpwr = FALSE;
		if(!cck_highpwr)
		{
			report = pcck_buf->cck_agc_rpt & 0xc0;
			report = report>>6;
			switch(report)
			{
				// 03312009 modified by cosa
				// Modify the RF RNA gain value to -40, -20, -2, 14 by Jenyu's suggestion
				// Note: different RF with the different RNA gain.
				case 0x3:
					rx_pwr_all = -40 - (pcck_buf->cck_agc_rpt & 0x3e);
					break;
				case 0x2:
					rx_pwr_all = -20 - (pcck_buf->cck_agc_rpt & 0x3e);
					break;
				case 0x1:
					rx_pwr_all = -2 - (pcck_buf->cck_agc_rpt & 0x3e);
					break;
				case 0x0:
					rx_pwr_all = 14 - (pcck_buf->cck_agc_rpt & 0x3e);
					break;
			}
		}
		else
		{
			report =((u8)(le32_to_cpu( pphy_stat->phydw1) >>8)) & 0x60;
			report = report>>5;
			switch(report)
			{
				case 0x3:
					rx_pwr_all = -40 - ((pcck_buf->cck_agc_rpt & 0x1f)<<1) ;
					break;
				case 0x2:
					rx_pwr_all = -20 - ((pcck_buf->cck_agc_rpt & 0x1f)<<1);
					break;
				case 0x1:
					rx_pwr_all = -2 - ((pcck_buf->cck_agc_rpt & 0x1f)<<1) ;
					break;
				case 0x0:
					rx_pwr_all = 14 - ((pcck_buf->cck_agc_rpt & 0x1f)<<1) ;
					break;
			}
		}

		pwdb_all= query_rx_pwr_percentage(rx_pwr_all);
		//if(pMgntInfo->CustomerID == RT_CID_819x_Lenovo)		
		{
			// CCK gain is smaller than OFDM/MCS gain, 
			// so we add gain diff by experiences, the val is 6
			pwdb_all+=6;
			if(pwdb_all > 100)
				pwdb_all = 100;
			// modify the offset to make the same gain index with OFDM.
			if(pwdb_all > 34 && pwdb_all <= 42)
				pwdb_all -= 2;
			else if(pwdb_all > 26 && pwdb_all <= 34)
				pwdb_all -= 6;
			else if(pwdb_all > 14 && pwdb_all <= 26)
				pwdb_all -= 8;
			else if(pwdb_all > 4 && pwdb_all <= 14)
				pwdb_all -= 4;
		}
		

		//
		// (3) Get Signal Quality (EVM)
		//
		
		//if(bPacketMatchBSSID)
		{
			u8	sq;

			{
				
			if(pwdb_all> 40 )
			{
				sq = 100;
			}else
			{
				sq = pcck_buf->sq_rpt;
				
				if(pcck_buf->sq_rpt > 64)
					sq = 0;
				else if (pcck_buf->sq_rpt < 20)
					sq= 100;
				else
					sq = ((64-sq) * 100) / 44;

			}
			}
			prframe->u.hdr.attrib.signal_qual=sq;
			prframe->u.hdr.attrib.rx_mimo_signal_qual[0] =sq;
			prframe->u.hdr.attrib.rx_mimo_signal_qual[1] = -1;
		}
	}
	else
	{

		// 
		// (1)Get RSSI for HT rate
		//
		for(i=0; i<((padapter->registrypriv.rf_config) &0x0f ); i++)
		{
			rf_rx_num++;
			//if (IS_HARDWARE_TYPE_8192S(Adapter))
			{
				rx_pwr[i] = ((pphy_head[PHY_STAT_GAIN_TRSW_SHT+i]&0x3F)*2) - 110;
			}		
			/* Translate DBM to percentage. */
			rssi=query_rx_pwr_percentage(rx_pwr[i]);
			total_rssi += rssi;
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("RF-%d RXPWR=%x RSSI=%d\n", i, rx_pwr[i], rssi));
			
			//Get Rx snr value in DB			

		}
		
		
		//
		// (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive)
		//
		rx_pwr_all = (((pphy_head[PHY_STAT_PWDB_ALL_SHT]) >> 1 )& 0x7f) -106;
		pwdb_all = query_rx_pwr_percentage(rx_pwr_all);	
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("PWDB_ALL=%d\n",	pwdb_all));
		
		
	//	if(pMgntInfo->CustomerID != RT_CID_819x_Lenovo)		
		{
			//
			// (3)EVM of HT rate
			//
			if(prframe->u.hdr.attrib.htc &&  prframe->u.hdr.attrib.mcs_rate >=20 && prframe->u.hdr.attrib.mcs_rate<=27)

				max_spatial_stream = 2; //both spatial stream make sense
			else
				max_spatial_stream = 1; //only spatial stream 1 makes sense

			for(i=0; i<max_spatial_stream; i++)
			{
				// Do not use shift operation like "rx_evmX >>= 1" because the compilor of free build environment
				// fill most significant bit to "zero" when doing shifting operation which may change a negative 
				// value to positive one, then the dbm value (which is supposed to be negative)  is not correct anymore.
				evm =evm_db2percentage( (pphy_head[PHY_STAT_RXEVM_SHT+i] /*/ 2*/));	//dbm

				RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("RXRATE=%x RXEVM=%x EVM=%s%d\n", 
				prframe->u.hdr.attrib.mcs_rate, pphy_head[PHY_STAT_RXEVM_SHT+i], "%",evm));
				
			//	if(bPacketMatchBSSID)
				{
					if(i==0) // Fill value in RFD, Get the first spatial stream only
					{
						prframe->u.hdr.attrib.signal_qual = (u8)(evm & 0xff);
					}
					prframe->u.hdr.attrib.rx_mimo_signal_qual[i] = (u8)(evm & 0xff);
				}
			}
		}
		//
		// 4. Record rx statistics for debug 
		//		

	}

	//UI BSS List signal strength(in percentage), make it good looking, from 0~100.
	//It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp().
	if(bcck_rate)
	{
		prframe->u.hdr.attrib.signal_strength=(u8)signal_scale_mapping(pwdb_all);
	}
	else
	{	
		if (rf_rx_num != 0)
		{
			prframe->u.hdr.attrib.signal_strength= (u8)(signal_scale_mapping( total_rssi/=rf_rx_num));
		}
	}
}

 void process_link_qual(_adapter *padapter,union recv_frame *prframe)
{
	u32	last_evm=0, nSpatialStream, tmpVal;
 	struct rx_pkt_attrib *pattrib;

	if(prframe == NULL || padapter==NULL){
		return;
	}

	pattrib = &prframe->u.hdr.attrib;	
	if(pattrib->signal_qual != 0)	
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
			padapter->recvpriv.signal_qual_data.total_val += pattrib->signal_qual;

			padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index++] = pattrib->signal_qual;
			if(padapter->recvpriv.signal_qual_data.index >= PHY_LINKQUALITY_SLID_WIN_MAX)
				padapter->recvpriv.signal_qual_data.index = 0;

			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("Total SQ=%d  pattrib->signal_qual= %d\n", padapter->recvpriv.signal_qual_data.total_val, pattrib->signal_qual));

			// <1> Showed on UI for user, in percentage.
			tmpVal = padapter->recvpriv.signal_qual_data.total_val/padapter->recvpriv.signal_qual_data.total_num;
			padapter->recvpriv.signal=(u8)tmpVal;
			
	}
	else
		
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" pattrib->signal_qual =%d\n", pattrib->signal_qual ));
	
}	// Process_UiLinkQuality8192S



void process_rssi(_adapter *padapter,union recv_frame *prframe)
{
		u32			last_rssi, tmp_val;
	struct rx_pkt_attrib *pattrib= &prframe->u.hdr.attrib;
//	if(pRfd->Status.bPacketToSelf || pRfd->Status.bPacketBeacon)
	{
//		Adapter->RxStats.RssiCalculateCnt++;	//For antenna Test
		if(padapter->recvpriv.signal_strength_data.total_num++ >= PHY_RSSI_SLID_WIN_MAX)
		{
			padapter->recvpriv.signal_strength_data.total_num = PHY_RSSI_SLID_WIN_MAX;
			last_rssi = padapter->recvpriv.signal_strength_data.elements[padapter->recvpriv.signal_strength_data.index];
			padapter->recvpriv.signal_strength_data.total_val -= last_rssi;
		}
		padapter->recvpriv.signal_strength_data.total_val  +=pattrib->signal_strength;
	
		padapter->recvpriv.signal_strength_data.elements[padapter->recvpriv.signal_strength_data.index++] = pattrib->signal_strength;
		if(padapter->recvpriv.signal_strength_data.index >= PHY_RSSI_SLID_WIN_MAX)
			padapter->recvpriv.signal_strength_data.index = 0;
	

		tmp_val = padapter->recvpriv.signal_strength_data.total_val/padapter->recvpriv.signal_strength_data.total_num;
		padapter->recvpriv.rssi=(s8)translate2dbm( padapter,(u8)tmp_val);

		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("UI RSSI = %d, ui_rssi.TotalVal = %d, ui_rssi.TotalNum = %d \n", tmp_val, padapter->recvpriv.signal_strength_data.total_val,padapter->recvpriv.signal_strength_data.total_num));
	}

	
	
}	// Process_UI_RSSI_8192S

void process_phy_info(_adapter *padapter,union recv_frame *prframe){
	query_rx_phy_status(padapter, prframe);
	process_rssi(padapter, prframe);
	process_link_qual(padapter,  prframe);

}

int recv_func(_adapter *padapter, void *pcontext)
{	
	struct rx_pkt_attrib *pattrib;
	union recv_frame *prframe, *orig_prframe;	
	int retval = _SUCCESS;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;

		
	prframe = (union recv_frame *)pcontext;	
	orig_prframe = prframe;

	pattrib = &prframe->u.hdr.attrib;	

#ifdef CONFIG_MP_INCLUDED		
       if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)	)//&&(padapter->mppriv.check_mp_pkt == 0))
       {     	       	
		if(pattrib->crc_err == 1)
			padapter->mppriv.rx_crcerrpktcount++;
		else
			padapter->mppriv.rx_pktcount++;	         	
					   
		if(check_fwstate(pmlmepriv, WIFI_MP_LPBK_STATE) ==_FALSE){
			RT_TRACE(_module_rtl871x_recv_c_,_drv_alert_,("MP - Not in loopback mode , drop pkt \n"));
			free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
			goto _exit_recv_func;	
		}
       }	   
#endif	


	//check the frame crtl field and decache
	retval=validate_recv_frame(padapter, prframe);
	if(retval !=_SUCCESS)
	{ 		
		free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recv_func: drop pkt \n"));
		goto _exit_recv_func;
	}
	process_phy_info(padapter, prframe);
	prframe=decryptor(padapter, prframe);
	if(prframe==NULL){
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("decryptor: drop pkt \n"));
		retval = _FAIL;
		goto _exit_recv_func;
	}


	prframe = recvframe_chk_defrag(padapter, prframe);
	if(prframe==NULL){
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvframe_chk_defrag: drop pkt \n"));
		goto _exit_recv_func;
	}

	prframe=portctrl(padapter, prframe);
	if(prframe==NULL)	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("portctrl: drop pkt \n"));
		retval = _FAIL;
		goto _exit_recv_func;		
	}


//#if defined(CONFIG_80211N_HT) && defined(CONFIG_RECV_REORDERING_CTRL)
#ifdef CONFIG_80211N_HT

	retval = process_recv_indicatepkts(padapter, prframe);
	if(retval != _SUCCESS)
	{		
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recv_func: process_recv_indicatepkts fail! \n"));
		free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
		goto _exit_recv_func;	
	}	

#else

	if(!pattrib->amsdu)
	{
		retval=wlanhdr_to_ethhdr (prframe);
		if(retval != _SUCCESS)
		{		
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("wlanhdr_to_ethhdr: drop pkt \n"));
			free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
			goto _exit_recv_func;	
		}
		
#ifdef CONFIG_DRVEXT_MODULE
		retval = drvext_rx_handler(padapter, prframe->u.hdr.rx_data, prframe->u.hdr.len);
		if (retval == _SUCCESS)
		{		
			free_recvframe(orig_prframe, pfree_recv_queue);
			goto _exit_recv_func;
		}
#endif
		
		if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE))
		{	
			RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@ recv_func: recv_func recv_indicatepkt\n" ));	
			//indicate this recv_frame
			recv_indicatepkt(padapter, prframe);
		}
		else
		{	
			RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@  recv_func: free_recvframe\n" ));		
			RT_TRACE(_module_rtl871x_recv_c_, _drv_debug_, ("recv_func:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));		
			retval = _FAIL;
			free_recvframe(orig_prframe, pfree_recv_queue); //free this recv_frame
		}		
		
	}	
	else if(pattrib->amsdu==1)
	{		
	
		retval = amsdu_to_msdu(padapter, prframe);
		if(retval != _SUCCESS)
		{			
			free_recvframe(orig_prframe, pfree_recv_queue);
			goto _exit_recv_func;	
		}
	}
	else
	{
		
	}
	
#endif


_exit_recv_func:

	return retval;	

}

#ifdef  PLATFORM_LINUX
#ifdef CONFIG_RECV_TASKLET
static int recvbuf2recvframe_u(_adapter *padapter, struct sk_buff *pskb)
{
	u8 *pbuf, shift_sz = 0;	
	u8	frag, mf;
	uint	pkt_len;
	s32 transfer_len;
	struct recv_stat *prxstat;	
	u16	drvinfo_sz, pkt_offset, tmp_len, alloc_sz;
	s16	pkt_cnt;
	_queue *pfree_recv_queue;
	_pkt  *pkt_copy=NULL;
	union recv_frame *precvframe=NULL; 
	struct recv_priv *precvpriv = &padapter->recvpriv;

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("+recvbuf2recvframe_u()\n"));
	
	pfree_recv_queue = &(precvpriv->free_recv_queue);
	
	//pbuf = (u8*)precvbuf->pbuf;
	pbuf = pskb->data;	
	prxstat = (struct recv_stat *)pbuf;
	
	//pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16)&0x3ffff;
	pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16)&0xff;
	pkt_len =  le32_to_cpu(prxstat->rxdw0)&0x00003fff;
	
#ifdef CONFIG_USB_HCI

	//transfer_len = precvbuf->transfer_len;
	transfer_len = pskb->len;

	//	Added by Albert 2010/04/21
	//	Test throughput with Netgear 3700 ( No security ) with Chariot 3T3R pairs.
	//	The packet count will be a big number so that the containing packet will effect the Rx reordering.

	if ( transfer_len < pkt_len )
	{	//	 In this case, it means the MAX_RECVBUF_SZ is too small to get the data from 8712u.
		return _FAIL;
	}
	
	do{		

		prxstat = (struct recv_stat *)pbuf;	   
		pkt_len =  le32_to_cpu(prxstat->rxdw0)&0x00003fff;

		mf = (le32_to_cpu(prxstat->rxdw1) >> 27) & 0x1;//more fragment bit
		frag = (le32_to_cpu(prxstat->rxdw2) >> 12) & 0xf;//fragmentation number

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("rxdesc : offsset0:0x%08x, offsset4:0x%08x, offsset8:0x%08x, offssetc:0x%08x\n", 
			prxstat->rxdw0, prxstat->rxdw1, prxstat->rxdw2, prxstat->rxdw4));
			
		drvinfo_sz = (le32_to_cpu(prxstat->rxdw0)&0x000f0000)>>16;//uint 2^3 = 8 bytes
		drvinfo_sz = drvinfo_sz<<3;

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("DRV_INFO_SIZE=%d\n", drvinfo_sz));

		if(pkt_len<=0)
		{	
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe(), pkt_len<=0\n"));		
			goto  _exit_recvbuf2recvframe_u;
		}		

		if ( ( le32_to_cpu( prxstat->rxdw0 ) >> 23 ) & 0x01 )	//	Qos data, wireless lan header length is 26
		{
			shift_sz = 2;
		}
		
		precvframe = alloc_recvframe(pfree_recv_queue);
		if(precvframe==NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe(), precvframe==NULL\n"));			
			goto  _exit_recvbuf2recvframe_u;
		}

		_init_listhead(&precvframe->u.hdr.list);	
		precvframe->u.hdr.precvbuf = NULL;	//can't access the precvbuf 
		precvframe->u.hdr.len=0;
	
		tmp_len = pkt_len + drvinfo_sz + RXDESC_SIZE;
		pkt_offset = (u16)_RND128(tmp_len);

		// for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet.
		if((mf ==1)&&(frag == 0)){
			alloc_sz = 1658;	//	1658 + 6 = 1664, 1664 is 128 alignment.
		}
		else {
			alloc_sz = tmp_len;
		}

		//	2 is for IP header 4 bytes alignment in QoS packet case.
		//	4 is for skb->data 4 bytes alignment.
		alloc_sz += 6;	
		
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
		pkt_copy = dev_alloc_skb(alloc_sz);
#else			
		pkt_copy = netdev_alloc_skb(padapter->pnetdev, alloc_sz);
#endif		
		if(pkt_copy)
		{
			pkt_copy->dev = padapter->pnetdev;
			precvframe->u.hdr.pkt = pkt_copy;
			skb_reserve( pkt_copy, 4 - ((u32) ( pkt_copy->data ) % 4 ) );
			skb_reserve( pkt_copy, shift_sz );
			_memcpy(pkt_copy->data, pbuf, tmp_len);
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;
			precvframe->u.hdr.rx_end = pkt_copy->data + alloc_sz;
		}
		else
		{	
			//printk("recvbuf2recvframe:can not allocate memory for skb copy\n");				
			precvframe->u.hdr.pkt = skb_clone(pskb, GFP_ATOMIC);	
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pbuf;
			precvframe->u.hdr.rx_end = pbuf + alloc_sz;
		}
		

		recvframe_put(precvframe, tmp_len);
		recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);	

		//because the endian issue, driver avoid reference to the rxstat after calling update_recvframe_attrib_from_recvstat();
		update_recvframe_attrib_from_recvstat(&precvframe->u.hdr.attrib, prxstat);		
						
		if( recv_entry(precvframe) != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe(), recv_entry(precvframe) != _SUCCESS\n"));			
		}

		transfer_len -= pkt_offset;
		pbuf += pkt_offset;
		pkt_cnt--;
		precvframe = NULL;
		pkt_copy = NULL;

	}while((transfer_len>0) && (pkt_cnt>0));

_exit_recvbuf2recvframe_u:

	//dev_kfree_skb_any(pskb);
	
#endif //#ifdef CONFIG_USB_HCI	

	return _SUCCESS;
	
}

void recv_tasklet(void *priv)
{
	struct sk_buff *pskb;
	_adapter *padapter = (_adapter*)priv;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	
	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue)))
	{

		recvbuf2recvframe_u(padapter, pskb);

#ifdef CONFIG_PREALLOC_RECV_SKB

		pskb->tail = pskb->data;
		pskb->len = 0;
		
		skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
		
#else
		dev_kfree_skb_any(pskb);
#endif
				
	}	
}

#else
void recv_bh(void *priv)
{	
	int ret = _SUCCESS;
	union recv_frame *prframe= NULL;
	_adapter *padapter = (_adapter*)priv;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *precv_pending_queue=&precvpriv->recv_pending_queue;

	while(1)
	{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
		{
			printk("recv_bh => bDriverStopped or bSurpriseRemoved \n");
			break;
		}

		prframe = dequeue_recvframe(precv_pending_queue);
		if(prframe==NULL)   //no recv frame need to process
			break;

		ret = recv_func(padapter, prframe);
		if(ret != _SUCCESS)		
		{
			//printk("recv_bh(): after recv_func(), ret=%d\n", ret);
			RT_TRACE(_module_rtl871x_recv_c_, _drv_debug_, ("recv_bh(): after recv_func(), ret=%d\n", ret));		
		}
	}
	
}
#endif
#endif

