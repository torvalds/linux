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
 #define _RTL8723BU_RECV_C_

#include <rtl8723b_hal.h>


void rtl8723bu_init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf)
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

int	rtl8723bu_init_recv_priv(_adapter *padapter)
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
	     (void(*)(unsigned long))rtl8723bu_recv_tasklet,
	     (unsigned long)padapter);
#endif

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
#ifdef PLATFORM_LINUX
	precvpriv->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(precvpriv->int_in_urb == NULL){
		DBG_8192C("alloc_urb for interrupt in endpoint fail !!!!\n");
	}
#endif
	precvpriv->int_in_buf = rtw_zmalloc(USB_INTR_CONTENT_LENGTH);
	if(precvpriv->int_in_buf == NULL){
		DBG_8192C("alloc_mem for interrupt in endpoint fail !!!!\n");
	}
#endif

	//init recv_buf
	_rtw_init_queue(&precvpriv->free_recv_buf_queue);

	_rtw_init_queue(&precvpriv->recv_buf_pending_queue);

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

#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
			pskb = rtw_alloc_skb_premem();
#else
			pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
#endif

			if(pskb)
			{
				pskb->dev = padapter->pnetdev;

#ifndef CONFIG_PREALLOC_RX_SKB_BUFFER
				tmpaddr = (SIZE_PTR)pskb->data;
				alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
				skb_reserve(pskb, (RECVBUFF_ALIGN_SZ - alignment));
#endif //!

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

void rtl8723bu_free_recv_priv (_adapter *padapter)
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
		rtw_mfree(precvpriv->int_in_buf, USB_INTR_CONTENT_LENGTH);
#endif

#ifdef PLATFORM_LINUX

	if (skb_queue_len(&precvpriv->rx_skb_queue)) {
		DBG_8192C(KERN_WARNING "rx_skb_queue not empty\n");
	}

	rtw_skb_queue_purge(&precvpriv->rx_skb_queue);

#ifdef CONFIG_PREALLOC_RECV_SKB

	if (skb_queue_len(&precvpriv->free_recv_skb_queue)) {
		DBG_8192C(KERN_WARNING "free_recv_skb_queue not empty, %d\n", skb_queue_len(&precvpriv->free_recv_skb_queue));
	}

#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
	{
		int i=0;
		struct sk_buff *skb;

		while ((skb = skb_dequeue(&precvpriv->free_recv_skb_queue)) != NULL)
		{
			if(i<NR_PREALLOC_RECV_SKB)
				rtw_free_skb_premem(skb);
			else				
				_rtw_skb_free(skb);

			i++;
		}	
	}	
#else 
	rtw_skb_queue_purge(&precvpriv->free_recv_skb_queue);
#endif //CONFIG_PREALLOC_RX_SKB_BUFFER

#endif

#endif

}


void update_recvframe_attrib(
	PADAPTER padapter,
	union recv_frame *precvframe,
	u8 *prxstat)
{
	struct rx_pkt_attrib	*pattrib;


	pattrib = &precvframe->u.hdr.attrib;
	_rtw_memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	// update rx report to recv_frame attribute
	pattrib->pkt_len = (u16)GET_RX_STATUS_DESC_PKT_LEN_8723B(prxstat);
	pattrib->pkt_rpt_type = GET_RX_STATUS_DESC_RPT_SEL_8723B(prxstat) ? C2H_PACKET : NORMAL_RX;

	if (pattrib->pkt_rpt_type == NORMAL_RX) {
		// Normal rx packet
		pattrib->drvinfo_sz = (u8)GET_RX_STATUS_DESC_DRVINFO_SIZE_8723B(prxstat) << 3;
		pattrib->shift_sz = (u8)GET_RX_STATUS_DESC_SHIFT_8723B(prxstat);
		pattrib->physt = (u8)GET_RX_STATUS_DESC_PHY_STATUS_8723B(prxstat);

		pattrib->crc_err = (u8)GET_RX_STATUS_DESC_CRC32_8723B(prxstat);
		pattrib->icv_err = (u8)GET_RX_STATUS_DESC_ICV_8723B(prxstat);

		pattrib->bdecrypted = (u8)GET_RX_STATUS_DESC_SWDEC_8723B(prxstat) ? 0 : 1;
		pattrib->encrypt = (u8)GET_RX_STATUS_DESC_SECURITY_8723B(prxstat);

		pattrib->qos = (u8)GET_RX_STATUS_DESC_QOS_8723B(prxstat);
		pattrib->priority = (u8)GET_RX_STATUS_DESC_TID_8723B(prxstat);

		pattrib->amsdu = (u8)GET_RX_STATUS_DESC_AMSDU_8723B(prxstat);

		pattrib->seq_num = (u16)GET_RX_STATUS_DESC_SEQ_8723B(prxstat);
		pattrib->frag_num = (u8)GET_RX_STATUS_DESC_FRAG_8723B(prxstat);
		pattrib->mfrag = (u8)GET_RX_STATUS_DESC_MORE_FRAG_8723B(prxstat);
		pattrib->mdata = (u8)GET_RX_STATUS_DESC_MORE_DATA_8723B(prxstat);

		pattrib->data_rate = (u8)GET_RX_STATUS_DESC_RX_RATE_8723B(prxstat);
	}
}

/*
 * Notice:
 *	Before calling this function,
 *	precvframe->u.hdr.rx_data should be ready!
 */
void update_recvframe_phyinfo(
	union recv_frame	*precvframe,
	struct phy_stat *pphy_status)
{
	PADAPTER 			padapter = precvframe->u.hdr.adapter;
	struct rx_pkt_attrib	*pattrib = &precvframe->u.hdr.attrib;
	HAL_DATA_TYPE		*pHalData= GET_HAL_DATA(padapter); 	
	PODM_PHY_INFO_T 	pPHYInfo  = (PODM_PHY_INFO_T)(&pattrib->phy_info); 

	u8					*wlanhdr;
	ODM_PACKET_INFO_T	pkt_info;
	u8 *sa = NULL;
	//_irqL		irqL;
	struct sta_priv *pstapriv;
	struct sta_info *psta;
	
	pkt_info.bPacketMatchBSSID =_FALSE;
	pkt_info.bPacketToSelf = _FALSE;
	pkt_info.bPacketBeacon = _FALSE;


	wlanhdr = get_recvframe_data(precvframe);

	pkt_info.bPacketMatchBSSID = ((!IsFrameTypeCtrl(wlanhdr)) &&
		!pattrib->icv_err && !pattrib->crc_err &&
		_rtw_memcmp(get_hdr_bssid(wlanhdr), get_bssid(&padapter->mlmepriv), ETH_ALEN));

	pkt_info.bPacketToSelf = pkt_info.bPacketMatchBSSID && (_rtw_memcmp(get_ra(wlanhdr), myid(&padapter->eeprompriv), ETH_ALEN));

	pkt_info.bPacketBeacon = pkt_info.bPacketMatchBSSID && (GetFrameSubType(wlanhdr) == WIFI_BEACON);
/*
	if(pkt_info.bPacketBeacon){
		if(check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == _TRUE){				
			sa = padapter->mlmepriv.cur_network.network.MacAddress;
			#if 0
			{					
				DBG_871X("==> rx beacon from AP[%02x:%02x:%02x:%02x:%02x:%02x]\n",
					sa[0],sa[1],sa[2],sa[3],sa[4],sa[5]);					
			}
			#endif
		}
		//to do Ad-hoc
	}
	else{
		sa = get_sa(wlanhdr);
	}		
*/		
	sa = get_ta(wlanhdr);

	pkt_info.StationID = 0xFF;

	pstapriv = &padapter->stapriv;
	psta = rtw_get_stainfo(pstapriv, sa);
	if (psta)
	{
             pkt_info.StationID = psta->mac_id;
		//DBG_871X("%s ==> StationID(%d)\n",__FUNCTION__,pkt_info.StationID);
	}
	pkt_info.DataRate = pattrib->data_rate;

	//rtl8723b_query_rx_phy_status(precvframe, pphy_status);
	//_enter_critical_bh(&pHalData->odm_stainfo_lock, &irqL);
	 ODM_PhyStatusQuery(&pHalData->odmpriv,pPHYInfo,(u8 *)pphy_status,&(pkt_info));	
	if(psta) psta->rssi = pattrib->phy_info.RecvSignalPower;
	//_exit_critical_bh(&pHalData->odm_stainfo_lock, &irqL);
	precvframe->u.hdr.psta = NULL;
	if (pkt_info.bPacketMatchBSSID &&
		(check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE))
	{			
		if (psta)
		{                  
			precvframe->u.hdr.psta = psta;
			rtl8723b_process_phy_info(padapter, precvframe);	               
                }
	}
	else if (pkt_info.bPacketToSelf || pkt_info.bPacketBeacon)
	{
		if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE)
		{			
			if (psta)
			{
				precvframe->u.hdr.psta = psta;
			}
		}
		rtl8723b_process_phy_info(padapter, precvframe);             
	}
}


