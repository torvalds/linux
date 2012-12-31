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
#define _RECV_OSDEP_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#include <wifi.h>
#include <recv_osdep.h>

#include <osdep_intf.h>
#include <ethernet.h>
#include <linux/if_arp.h>


#ifdef CONFIG_USB_HCI
#include <usb_ops.h>
#endif

//init os related resource in struct recv_priv
int os_recv_resource_init(struct recv_priv *precvpriv, _adapter *padapter)
{	
	int	res=_SUCCESS;

	return res;
}

//alloc os related resource in union recv_frame
int os_recv_resource_alloc(_adapter *padapter, union recv_frame *precvframe)
{	
	int	res=_SUCCESS;
	struct recv_priv *precvpriv = &(padapter->recvpriv);	
	
	precvframe->u.hdr.pkt_newalloc = precvframe->u.hdr.pkt = NULL;

	return res;

}

//free os related resource in union recv_frame
void os_recv_resource_free(struct recv_priv *precvpriv)
{

}


//alloc os related resource in struct recv_buf
#ifdef CONFIG_RTL8712
int os_recvbuf_resource_alloc(_adapter *padapter, struct recv_buf *precvbuf)
{
	int res=_SUCCESS;

#ifdef CONFIG_USB_HCI	
	precvbuf->irp_pending = _FALSE;
	precvbuf->purb = usb_alloc_urb(0, GFP_KERNEL);
	if(precvbuf->purb == NULL){		 				
		res = _FAIL;			
	}

	precvbuf->pskb = NULL;

	precvbuf->reuse = _FALSE;

	precvbuf->pallocated_buf  = precvbuf->pbuf = NULL;

        precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pend = NULL;

	precvbuf->transfer_len = 0;

	precvbuf->len = 0;
	
#endif
#ifdef CONFIG_SDIO_HCI
	precvbuf->pskb = NULL;

	precvbuf->pallocated_buf  = precvbuf->pbuf = NULL;

        precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pend = NULL;


	precvbuf->len = 0;
#endif
	return res;
	
}

//free os related resource in struct recv_buf
int os_recvbuf_resource_free(_adapter *padapter, struct recv_buf *precvbuf)
{
	int ret = _SUCCESS;
	
	if(precvbuf->pskb)
		dev_kfree_skb_any(precvbuf->pskb);

#ifdef CONFIG_USB_HCI
	if(precvbuf->purb)
	{
		usb_kill_urb(precvbuf->purb);
		usb_free_urb(precvbuf->purb);
	}
#endif

	return ret;	
}

#endif

void handle_tkip_mic_err(_adapter *padapter,u8 bgroup)
{
#ifdef CONFIG_IOCTL_CFG80211
	enum nl80211_key_type key_type;
#endif //CONFIG_IOCTL_CFG80211
    union iwreq_data wrqu;
    struct iw_michaelmicfailure    ev;
    struct mlme_priv*              pmlmepriv  = &padapter->mlmepriv;

    
    _memset( &ev, 0x00, sizeof( ev ) );

#ifdef CONFIG_IOCTL_CFG80211
	if ( bgroup )
	{
		key_type |= NL80211_KEYTYPE_GROUP;
	}
	else
	{
		key_type |= NL80211_KEYTYPE_PAIRWISE;
	}

	cfg80211_michael_mic_failure(padapter->pnetdev, (u8 *)&pmlmepriv->assoc_bssid[ 0 ], key_type, -1,
		NULL, GFP_ATOMIC);
#endif
	
    if ( bgroup )
    {
        ev.flags |= IW_MICFAILURE_GROUP;
    }
    else
    {
        ev.flags |= IW_MICFAILURE_PAIRWISE;
    }
   
    ev.src_addr.sa_family = ARPHRD_ETHER;
    _memcpy( ev.src_addr.sa_data, &pmlmepriv->assoc_bssid[ 0 ], ETH_ALEN );

    _memset( &wrqu, 0x00, sizeof( wrqu ) );
    wrqu.data.length = sizeof( ev );

    wireless_send_event( padapter->pnetdev, IWEVMICHAELMICFAILURE, &wrqu, (char*) &ev );
}

void recv_indicatepkt(_adapter *padapter, union recv_frame *precv_frame)
{	
       struct recv_priv *precvpriv;
       _queue	*pfree_recv_queue;	     
	_pkt *skb;	
#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_RX
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
#endif

_func_enter_;

	precvpriv = &(padapter->recvpriv);	
	pfree_recv_queue = &(precvpriv->free_recv_queue);	
     
	skb = precv_frame->u.hdr.pkt;	       
       if(skb == NULL)
       {        
            RT_TRACE(_module_recv_osdep_c_,_drv_err_,("recv_indicatepkt():skb==NULL something wrong!!!!\n"));		   
	     goto _recv_indicatepkt_drop;
	}

	   
	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("recv_indicatepkt():skb != NULL !!!\n"));		
	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("\n recv_indicatepkt():precv_frame->u.hdr.rx_head=%p  precv_frame->hdr.rx_data=%p ", precv_frame->u.hdr.rx_head, precv_frame->u.hdr.rx_data));
	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("precv_frame->hdr.rx_tail=%p precv_frame->u.hdr.rx_end=%p precv_frame->hdr.len=%d \n", precv_frame->u.hdr.rx_tail, precv_frame->u.hdr.rx_end, precv_frame->u.hdr.len));
		
	skb->data = precv_frame->u.hdr.rx_data;
	skb->tail = precv_frame->u.hdr.rx_tail;	
	skb->len = precv_frame->u.hdr.len;
	
	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("\n skb->head=%p skb->data=%p skb->tail=%p skb->end=%p skb->len=%d\n", skb->head, skb->data, skb->tail, skb->end, skb->len));
	
#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_RX
        if ( (pattrib->tcpchk_valid == 1) && (pattrib->tcp_chkrpt == 1) ) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		//printk("CHECKSUM_UNNECESSARY \n");
	} else {
		skb->ip_summed = CHECKSUM_NONE;
		//printk("CHECKSUM_NONE(%d, %d) \n", pattrib->tcpchk_valid, pattrib->tcp_chkrpt);
	}
#else /* !CONFIG_RTL8712_TCP_CSUM_OFFLOAD_RX */

	skb->ip_summed = CHECKSUM_NONE;

#endif
	skb->dev = padapter->pnetdev;
	skb->protocol = eth_type_trans(skb, padapter->pnetdev);
	
	netif_rx(skb);


	precv_frame->u.hdr.pkt = NULL; // pointers to NULL before free_recvframe()


	free_recvframe(precv_frame, pfree_recv_queue);


	RT_TRACE(_module_recv_osdep_c_,_drv_info_,("\n recv_indicatepkt :after netif_rx!!!!\n"));

_func_exit_;		

        return;		

_recv_indicatepkt_drop:

	 //enqueue back to free_recv_queue	
	 if(precv_frame)
		 free_recvframe(precv_frame, pfree_recv_queue);

	 
 	 precvpriv->rx_drop++;	

_func_exit_;

}

void os_read_port(_adapter *padapter, struct recv_buf *precvbuf)
{	
	struct recv_priv *precvpriv = &padapter->recvpriv;

#ifdef CONFIG_USB_HCI

	precvbuf->ref_cnt--;

	//free skb in recv_buf
	dev_kfree_skb_any(precvbuf->pskb);

	precvbuf->pskb = NULL;
	precvbuf->reuse = _FALSE;

	if(precvbuf->irp_pending == _FALSE)
	{
		read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
	}	
		

#endif
#ifdef CONFIG_SDIO_HCI
		precvbuf->pskb = NULL;
#endif

}

void _reordering_ctrl_timeout_handler (void *FunctionContext)
{
	struct recv_reorder_ctrl *preorder_ctrl = (struct recv_reorder_ctrl *)FunctionContext;
	reordering_ctrl_timeout_handler(preorder_ctrl);
}

void init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl)
{
	_adapter *padapter = preorder_ctrl->padapter;

	_init_timer(&(preorder_ctrl->reordering_ctrl_timer), padapter->pnetdev, _reordering_ctrl_timeout_handler, preorder_ctrl);

	
}

