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
#define _RTL8192CU_RECV_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <mlme_osdep.h>
#include <ip.h>
#include <if_ether.h>
#include <ethernet.h>

#include <usb_ops.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

#include <wifi.h>
#include <circ_buf.h>

#include <rtl8192c_hal.h>


void rtl8192cu_init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf)
{

	precvbuf->transfer_len = 0;

	precvbuf->len = 0;

	precvbuf->ref_cnt = 0;

	if(precvbuf->pbuf)
	{
		precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pbuf;
		precvbuf->pend = precvbuf->pdata + MAX_RECVBUF_SZ;
	}

}

int	rtl8192cu_init_recv_priv(_adapter *padapter)
{
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	int	i, res = _SUCCESS;
	struct recv_buf *precvbuf;

#ifdef CONFIG_RECV_THREAD_MODE	
	_rtw_init_sema(&precvpriv->recv_sema, 0);//will be removed
	_rtw_init_sema(&precvpriv->terminate_recvthread_sema, 0);//will be removed
#endif

#ifdef PLATFORM_LINUX
	tasklet_init(&precvpriv->recv_tasklet,
	     (void(*)(unsigned long))rtl8192cu_recv_tasklet,
	     (unsigned long)padapter);
#endif

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
#ifdef PLATFORM_LINUX
	precvpriv->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(precvpriv->int_in_urb == NULL){
		DBG_8192C("alloc_urb for interrupt in endpoint fail !!!!\n");
	}
#endif
	precvpriv->int_in_buf = rtw_zmalloc(sizeof(INTERRUPT_MSG_FORMAT_EX));
	if(precvpriv->int_in_buf == NULL){
		DBG_8192C("alloc_mem for interrupt in endpoint fail !!!!\n");
	}
#endif

	//init recv_buf
	_rtw_init_queue(&precvpriv->free_recv_buf_queue);

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	_rtw_init_queue(&precvpriv->recv_buf_pending_queue);
#endif	// CONFIG_USE_USB_BUFFER_ALLOC_RX

	precvpriv->pallocated_recv_buf = rtw_zmalloc(NR_RECVBUFF *sizeof(struct recv_buf) + 4);
	if(precvpriv->pallocated_recv_buf==NULL){
		res= _FAIL;
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("alloc recv_buf fail!\n"));
		goto exit;
	}
	_rtw_memset(precvpriv->pallocated_recv_buf, 0, NR_RECVBUFF *sizeof(struct recv_buf) + 4);

	precvpriv->precv_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_recv_buf), 4);
	//precvpriv->precv_buf = precvpriv->pallocated_recv_buf + 4 -
	//						((uint) (precvpriv->pallocated_recv_buf) &(4-1));


	precvbuf = (struct recv_buf*)precvpriv->precv_buf;

	for(i=0; i < NR_RECVBUFF ; i++)
	{
		_rtw_init_listhead(&precvbuf->list);

		_rtw_spinlock_init(&precvbuf->recvbuf_lock);

		precvbuf->alloc_sz = MAX_RECVBUF_SZ;

		res = rtw_os_recvbuf_resource_alloc(padapter, precvbuf);
		if(res==_FAIL)
			break;

		precvbuf->ref_cnt = 0;
		precvbuf->adapter =padapter;


		//rtw_list_insert_tail(&precvbuf->list, &(precvpriv->free_recv_buf_queue.queue));

		precvbuf++;

	}

	precvpriv->free_recv_buf_queue_cnt = NR_RECVBUFF;

#ifdef PLATFORM_LINUX

	skb_queue_head_init(&precvpriv->rx_skb_queue);

#ifdef CONFIG_PREALLOC_RECV_SKB
	{
		int i;
		SIZE_PTR tmpaddr=0;
		SIZE_PTR alignment=0;
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

				tmpaddr = (SIZE_PTR)pskb->data;
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

void rtl8192cu_free_recv_priv (_adapter *padapter)
{
	int	i;
	struct recv_buf	*precvbuf;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	precvbuf = (struct recv_buf *)precvpriv->precv_buf;

	for(i=0; i < NR_RECVBUFF ; i++)
	{
		rtw_os_recvbuf_resource_free(padapter, precvbuf);
		precvbuf++;
	}

	if(precvpriv->pallocated_recv_buf)
		rtw_mfree(precvpriv->pallocated_recv_buf, NR_RECVBUFF *sizeof(struct recv_buf) + 4);

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
#ifdef PLATFORM_LINUX
	if(precvpriv->int_in_urb)
	{
		usb_free_urb(precvpriv->int_in_urb);
	}
#endif
	if(precvpriv->int_in_buf)
		rtw_mfree(precvpriv->int_in_buf, sizeof(INTERRUPT_MSG_FORMAT_EX));
#endif

#ifdef PLATFORM_LINUX

	if (skb_queue_len(&precvpriv->rx_skb_queue)) {
		DBG_8192C(KERN_WARNING "rx_skb_queue not empty\n");
	}

	skb_queue_purge(&precvpriv->rx_skb_queue);

#ifdef CONFIG_PREALLOC_RECV_SKB

	if (skb_queue_len(&precvpriv->free_recv_skb_queue)) {
		DBG_8192C(KERN_WARNING "free_recv_skb_queue not empty, %d\n", skb_queue_len(&precvpriv->free_recv_skb_queue));
	}

	skb_queue_purge(&precvpriv->free_recv_skb_queue);

#endif

#endif

}

void rtl8192cu_update_recvframe_attrib_from_recvstat(union recv_frame *precvframe, struct recv_stat *prxstat)
{
	u8	physt, qos, shift, icverr, htc, crcerr;
	u16	drvinfo_sz=0;
	struct phy_stat		*pphy_info;
	struct rx_pkt_attrib	*pattrib = &precvframe->u.hdr.attrib;
	_adapter				*padapter = precvframe->u.hdr.adapter;
	u8	bPacketMatchBSSID =_FALSE;
	u8	bPacketToSelf = _FALSE;
	u8	bPacketBeacon = _FALSE;


	//Offset 0
	drvinfo_sz = (le32_to_cpu(prxstat->rxdw0)&0x000f0000)>>16;
	drvinfo_sz = drvinfo_sz<<3;

	pattrib->bdecrypted = ((le32_to_cpu(prxstat->rxdw0) & BIT(27)) >> 27)? 0:1;

	physt = ((le32_to_cpu(prxstat->rxdw0) & BIT(26)) >> 26)? 1:0;

	shift = (le32_to_cpu(prxstat->rxdw0)&0x03000000)>>24;

	qos = ((le32_to_cpu(prxstat->rxdw0) & BIT(23)) >> 23)? 1:0;

	icverr = ((le32_to_cpu(prxstat->rxdw0) & BIT(15)) >> 15)? 1:0;

	pattrib->crc_err = crcerr = ((le32_to_cpu(prxstat->rxdw0) & BIT(14)) >> 14 )? 1:0;


	//Offset 4

	//Offset 8

	//Offset 12
#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
	if ( le32_to_cpu(prxstat->rxdw3) & BIT(13)) {
		pattrib->tcpchk_valid = 1; // valid
		if ( le32_to_cpu(prxstat->rxdw3) & BIT(11) ) {
			pattrib->tcp_chkrpt = 1; // correct
			//DBG_8192C("tcp csum ok\n");
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

	pattrib->mcs_rate=(u8)((le32_to_cpu(prxstat->rxdw3))&0x3f);
	pattrib->rxht=(u8)((le32_to_cpu(prxstat->rxdw3) >>6)&0x1);

	htc = (u8)((le32_to_cpu(prxstat->rxdw3) >>10)&0x1);

	//Offset 16
	//Offset 20


#if 0 //dump rxdesc for debug
	DBG_8192C("drvinfo_sz=%d\n", drvinfo_sz);
	DBG_8192C("physt=%d\n", physt);
	DBG_8192C("shift=%d\n", shift);
	DBG_8192C("qos=%d\n", qos);
	DBG_8192C("icverr=%d\n", icverr);
	DBG_8192C("htc=%d\n", htc);
	DBG_8192C("bdecrypted=%d\n", pattrib->bdecrypted);
	DBG_8192C("mcs_rate=%d\n", pattrib->mcs_rate);
	DBG_8192C("rxht=%d\n", pattrib->rxht);
#endif

	//phy_info
	if(drvinfo_sz && physt)
	{
		bPacketMatchBSSID = ((!IsFrameTypeCtrl(precvframe->u.hdr.rx_data)) && !icverr && !crcerr &&
			_rtw_memcmp(get_hdr_bssid(precvframe->u.hdr.rx_data), get_bssid(&padapter->mlmepriv), ETH_ALEN));
			
		bPacketToSelf = bPacketMatchBSSID &&  (_rtw_memcmp(get_da(precvframe->u.hdr.rx_data), myid(&padapter->eeprompriv), ETH_ALEN));

		bPacketBeacon = (GetFrameSubType(precvframe->u.hdr.rx_data) ==  WIFI_BEACON);
	

		pphy_info = (struct phy_stat *)(prxstat+1);

		//DBG_8192C("pphy_info, of0=0x%08x\n", *pphy_info);
		//DBG_8192C("pphy_info, of1=0x%08x\n", *(pphy_info+1));
		//DBG_8192C("pphy_info, of2=0x%08x\n", *(pphy_info+2));
		//DBG_8192C("pphy_info, of3=0x%08x\n", *(pphy_info+3));
		//DBG_8192C("pphy_info, of4=0x%08x\n", *(pphy_info+4));
		//DBG_8192C("pphy_info, of5=0x%08x\n", *(pphy_info+5));
		//DBG_8192C("pphy_info, of6=0x%08x\n", *(pphy_info+6));
		//DBG_8192C("pphy_info, of7=0x%08x\n", *(pphy_info+7));

		rtl8192c_query_rx_phy_status(precvframe, pphy_info);

		precvframe->u.hdr.psta = NULL;
		if(bPacketMatchBSSID && check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
		{
			u8 *sa;
			struct sta_info *psta=NULL;
			struct sta_priv *pstapriv = &padapter->stapriv;
			
			sa = get_sa(precvframe->u.hdr.rx_data);

			psta = rtw_get_stainfo(pstapriv, sa);
			if(psta)
			{
				precvframe->u.hdr.psta = psta;
				rtl8192c_process_phy_info(padapter, precvframe);
			}
		}
		else if( bPacketToSelf || (bPacketBeacon && bPacketMatchBSSID) )
		{
			if(check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE)
			{
				u8 *sa;
				struct sta_info *psta=NULL;
				struct sta_priv *pstapriv = &padapter->stapriv;
			
				sa = get_sa(precvframe->u.hdr.rx_data);
	
				psta = rtw_get_stainfo(pstapriv, sa);
				if(psta)
				{
					precvframe->u.hdr.psta = psta;
				}				
			}
					
			rtl8192c_process_phy_info(padapter, precvframe);
		}

#if 0 //dump phy_status for debug

		DBG_8192C("signal_qual=%d\n", pattrib->signal_qual);
		DBG_8192C("signal_strength=%d\n", pattrib->signal_strength);
#endif

	}


}

