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
#define _XMIT_OSDEP_C_


#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


#include <if_ether.h>
#include <ip.h>
#include <rtl871x_byteorder.h>
#include <wifi.h>
#include <mlme_osdep.h>
#include <xmit_osdep.h>
#include <osdep_intf.h>
#include <circ_buf.h>
 #ifdef CONFIG_SDIO_HCI
#include <linux/mmc/sdio_func.h> 
#endif
uint remainder_len(struct pkt_file *pfile)
{

	return (pfile->buf_len - ((u32)(pfile->cur_addr) - (u32)(pfile->buf_start)));

}

void _open_pktfile (_pkt *pktptr, struct pkt_file *pfile)
{
_func_enter_;

	pfile->pkt = pktptr;
	pfile->cur_addr = pfile->buf_start = pktptr->data;
	pfile->pkt_len = pfile->buf_len = pktptr->len;

	pfile->cur_buffer = pfile->buf_start ;
	
_func_exit_;
}

uint _pktfile_read (struct pkt_file *pfile, u8 *rmem, uint rlen)
{
	uint len = 0;

_func_enter_;

	len = remainder_len(pfile);
	len = (rlen > len)? len : rlen;

	if (rmem)
		skb_copy_bits(pfile->pkt, pfile->buf_len - pfile->pkt_len, rmem, len);

	pfile->cur_addr += len;
	pfile->pkt_len -= len;

_func_exit_;

	return len; 
}

sint endofpktfile (struct pkt_file *pfile)
{
_func_enter_;
	if (pfile->pkt_len == 0){
_func_exit_;	
		return _TRUE;
	}
	else{
_func_exit_;	
		return _FALSE;
	}
}


void set_qos(struct pkt_file *ppktfile, struct pkt_attrib *pattrib)
{
	int i;
	struct ethhdr etherhdr;
	struct iphdr ip_hdr; 
	u16 UserPriority = 0;

	_open_pktfile(ppktfile->pkt, ppktfile);	
	_pktfile_read(ppktfile, (unsigned char*)&etherhdr, ETH_HLEN);	

	//i = _pktfile_read (ppktfile, (u8*)&ip_hdr, sizeof(ip_hdr));

	// get UserPriority from IP hdr
	if (pattrib->ether_type == 0x0800)
	{		
		i = _pktfile_read(ppktfile, (u8*)&ip_hdr, sizeof(ip_hdr));
		//UserPriority = (ntohs(ip_hdr.tos) >> 5) & 0x3 ;
		UserPriority = ip_hdr.tos >> 5;
	}
	else
	{
		// "When priority processing of data frames is supported, 
		//a STA's SME should send EAPOL-Key frames at the highest priority."

		if (pattrib->ether_type == 0x888e)
			UserPriority = 7;
	}

	pattrib->priority = UserPriority;
	pattrib->hdrlen = WLAN_HDR_A3_QOS_LEN;
	pattrib->subtype = WIFI_QOS_DATA_TYPE;
}

#ifdef CONFIG_SDIO_HCI
int os_xmit_resource_alloc(_adapter *padapter, struct xmit_buf *pxmitbuf)
{
	int	res=_SUCCESS;   

	return res;

}
void os_xmit_resource_free(_adapter *padapter, struct xmit_buf *pxmitbuf)
{

}
#endif
#ifdef CONFIG_USB_HCI

void urb_reset4_workitem_callback(struct work_struct *work)
{

	struct xmit_priv *pxmitpriv = container_of(work, struct xmit_priv, xmit_pipe4_reset_wi);
	_adapter *padapter = container_of(pxmitpriv, _adapter, xmitpriv);
	struct dvobj_priv * pdvobjpriv = (struct dvobj_priv *)&padapter->dvobjpriv;
	struct usb_device *pusbdev=pdvobjpriv->pusbdev;	  

	usb_clear_halt(pusbdev,usb_sndbulkpipe(pusbdev, 0x04));

}

void urb_reset6_workitem_callback(struct work_struct *work)
{
	struct xmit_priv *pxmitpriv = container_of(work, struct xmit_priv, xmit_pipe6_reset_wi);
	_adapter *padapter = container_of(pxmitpriv, _adapter, xmitpriv);
	struct dvobj_priv * pdvobjpriv = (struct dvobj_priv *)&padapter->dvobjpriv;
	struct usb_device *pusbdev=pdvobjpriv->pusbdev;	  

	usb_clear_halt(pusbdev,usb_sndbulkpipe(pusbdev, 0x06));

}

void urb_resetd_workitem_callback(struct work_struct *work)
{
	struct xmit_priv *pxmitpriv = container_of(work, struct xmit_priv, xmit_piped_reset_wi);
	_adapter *padapter = container_of(pxmitpriv, _adapter, xmitpriv);
	struct dvobj_priv * pdvobjpriv = (struct dvobj_priv *)&padapter->dvobjpriv;
	struct usb_device *pusbdev=pdvobjpriv->pusbdev;	  

	usb_clear_halt(pusbdev,usb_sndbulkpipe(pusbdev, 0x0d));
}

void SetFilter(struct work_struct *work)
{
	_adapter *padapter = container_of(work, _adapter, wkFilterRxFF0);
	struct dvobj_priv * pdvobjpriv = (struct dvobj_priv *)&padapter->dvobjpriv;
	u8  oldvalue = 0x00, newvalue = 0x00;
	_irqL	irqL;

	printk("+ SetFilter() \n");

	oldvalue = read8( padapter, 0x117 );
	newvalue = oldvalue & 0xfe;
	write8( padapter, 0x117, newvalue );

	_enter_critical( &padapter->lockRxFF0Filter, &irqL );
	padapter->blnEnableRxFF0Filter = 1;
	_exit_critical( &padapter->lockRxFF0Filter, &irqL );
	
	do
	{
		msleep( 100 );
	}
	while( padapter->blnEnableRxFF0Filter == 1 );

	write8( padapter, 0x117, oldvalue );

	printk("- SetFilter() \n");
}



int os_xmit_resource_alloc(_adapter *padapter, struct xmit_buf *pxmitbuf)
{
	int i;
	
       for(i=0; i<8; i++)
      	{
      		pxmitbuf->pxmit_urb[i] = usb_alloc_urb(0, GFP_KERNEL);
             	if(pxmitbuf->pxmit_urb[i] == NULL) 
             	{
             		printk("pxmitbuf->pxmit_urb[i]==NULL");
	        	return _FAIL;	 
             	}      		  	
	
      	}
                     
	return _SUCCESS;
	
}
void os_xmit_resource_free(_adapter *padapter, struct xmit_buf *pxmitbuf)
{
        int i;
        for(i=0; i<8; i++)
        {
                if(pxmitbuf->pxmit_urb[i])
                {
                        usb_kill_urb(pxmitbuf->pxmit_urb[i]);
                        usb_free_urb(pxmitbuf->pxmit_urb[i]);
                }
        }
}
#endif

void os_xmit_complete(_adapter *padapter, struct xmit_frame *pxframe)
{
	if(pxframe->pkt)
	{
		RT_TRACE(_module_xmit_osdep_c_,_drv_err_,("linux : os_xmit_complete, dev_kfree_skb()\n"));	

		dev_kfree_skb_any(pxframe->pkt);		
	}	

	pxframe->pkt = NULL;

}

int xmit_entry(_pkt *pkt, _nic_hdl pnetdev)
{
	struct xmit_frame *pxmitframe = NULL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	int ret = 0;

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("+xmit_enry\n"));

	if (if_up(padapter) == _FALSE)
	{
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("xmit_entry: if_up fail\n"));
		ret = 0;
		goto _xmit_entry_drop;
	}

	pxmitframe = alloc_xmitframe(pxmitpriv);
	if (pxmitframe == NULL)
	{
		//printk("pxmitframe == NULL \n");
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("pxmitframe == NULL\n"));

		//if (!netif_queue_stopped(pnetdev))
		//       netif_stop_queue(pnetdev);

		ret = 0;
		goto _xmit_entry_drop;
	}

	if ((update_attrib(padapter, pkt, &pxmitframe->attrib)) == _FAIL)
	{
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("drop xmit pkt for update fail\n"));		
		ret = 0;
		goto _xmit_entry_drop;
	}

	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_TX);

	pxmitframe->pkt = pkt;
	if (pre_xmit(padapter, pxmitframe) == _TRUE)
	{
		//dump xmitframe directly or drop xframe
		RT_TRACE(_module_xmit_osdep_c_,_drv_err_,("xmit_entry(): dev_kfree_skb()\n"));			
		dev_kfree_skb_any(pkt);
		pxmitframe->pkt = NULL;
	}
	else
	{
#ifdef CONFIG_SDIO_HCI
		_up_sema(&(pxmitpriv->xmit_sema));
		RT_TRACE(_module_xmit_osdep_c_, _drv_notice_, ("xmit_entry: enqueue xmit pkt xmit_frame=0x%p up sema\n", pxmitframe));
#endif
	}

	pxmitpriv->tx_pkts++;

	RT_TRACE(_module_xmit_osdep_c_, _drv_notice_, ("xmit_entry:tx_pkts=%d\n", (u32)pxmitpriv->tx_pkts));

_func_exit_;

	return ret;

_xmit_entry_drop:

	RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("_xmit_etnry_drop\n"));

	if (pxmitframe) {
		free_xmitframe(pxmitpriv, pxmitframe);
	}

	pxmitpriv->tx_drop++;

	dev_kfree_skb_any(pkt);

_func_exit_;

	return ret;
}

