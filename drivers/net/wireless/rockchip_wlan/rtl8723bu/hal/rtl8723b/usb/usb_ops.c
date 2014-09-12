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
  *******************************************************************************/
 #define _USB_OPS_C_

#include <rtl8723b_hal.h>

static u8 usb_read8(struct intf_hdl *pintfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u8 data=0;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x01;//read_in
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;	
	
	usbctrl_vendorreq(pintfhdl, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return data;
		
}

static u16 usb_read16(struct intf_hdl *pintfhdl, u32 addr)
{       
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u16 data=0;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x01;//read_in
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;	
	
	usbctrl_vendorreq(pintfhdl, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return data;
	
}

static u32 usb_read32(struct intf_hdl *pintfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data=0;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x01;//read_in
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;	
	
	usbctrl_vendorreq(pintfhdl, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return data;
	
}

static int usb_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u8 data;
	int ret;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;
	
	data = val;	
	
	 ret = usbctrl_vendorreq(pintfhdl, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
	return ret;
	
}

static int usb_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{	
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u16 data;
	int ret;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;
	
	data = val;
		
	ret = usbctrl_vendorreq(pintfhdl, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
	return ret;
	
}

static int usb_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	int ret;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;
	data = val;	
	

	ret =usbctrl_vendorreq(pintfhdl, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
	return ret;
	
}

static int usb_writeN(struct intf_hdl *pintfhdl, u32 addr, u32 length, u8 *pdata)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u8 buf[VENDOR_CMD_MAX_DATA_LEN]={0};
	int ret;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = length;
	 _rtw_memcpy(buf, pdata, len );
	
	ret = usbctrl_vendorreq(pintfhdl, request, wvalue, index, buf, len, requesttype);
	
	_func_exit_;
	
	return ret;
	
}


#ifdef CONFIG_SUPPORT_USB_INT
void interrupt_handler_8723bu(_adapter *padapter,u16 pkt_len,u8 *pbuf)
{
	HAL_DATA_TYPE	*pHalData=GET_HAL_DATA(padapter);
	struct reportpwrstate_parm pwr_rpt;

	if ( pkt_len != INTERRUPT_MSG_FORMAT_LEN )
	{
		DBG_8192C("%s Invalid interrupt content length (%d)!\n", __FUNCTION__, pkt_len);
		return ;
	}

	// HISR 
	_rtw_memcpy(&(pHalData->IntArray[0]), &(pbuf[USB_INTR_CONTENT_HISR_OFFSET]), 4);
	_rtw_memcpy(&(pHalData->IntArray[1]), &(pbuf[USB_INTR_CONTENT_HISRE_OFFSET]), 4);

	#if 0 //DBG
	{
		u32 hisr=0 ,hisr_ex=0;
		_rtw_memcpy(&hisr,&(pHalData->IntArray[0]),4);
		hisr = le32_to_cpu(hisr);	
		
		_rtw_memcpy(&hisr_ex,&(pHalData->IntArray[1]),4);
		hisr_ex = le32_to_cpu(hisr_ex);
		
		if((hisr != 0) || (hisr_ex!=0))
			DBG_871X("===> %s hisr:0x%08x ,hisr_ex:0x%08x \n",__FUNCTION__,hisr,hisr_ex);
	}
	#endif


#ifdef CONFIG_LPS_LCLK
	if(  pHalData->IntArray[0]  & IMR_CPWM_88E )
	{
		_rtw_memcpy(&pwr_rpt.state, &(pbuf[USB_INTR_CONTENT_CPWM1_OFFSET]), 1);
		//_rtw_memcpy(&pwr_rpt.state2, &(pbuf[USB_INTR_CONTENT_CPWM2_OFFSET]), 1);

		//88e's cpwm value only change BIT0, so driver need to add PS_STATE_S2 for LPS flow.		
		pwr_rpt.state |= PS_STATE_S2;		
		_set_workitem(&(adapter_to_pwrctl(padapter)->cpwm_event));
	}
#endif//CONFIG_LPS_LCLK

#ifdef CONFIG_INTERRUPT_BASED_TXBCN

	#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
	if (pHalData->IntArray[0] & IMR_BCNDMAINT0_88E)
	#endif
	#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
	if (pHalData->IntArray[0] & (IMR_TBDER_88E|IMR_TBDOK_88E))
	#endif	
	{
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
		#if 0
		if(pHalData->IntArray[0] & IMR_BCNDMAINT0_88E)
			DBG_8192C("%s: HISR_BCNERLY_INT\n", __func__);
		if(pHalData->IntArray[0] & IMR_TBDOK_88E)
			DBG_8192C("%s: HISR_TXBCNOK\n", __func__);
		if(pHalData->IntArray[0] & IMR_TBDER_88E)
			DBG_8192C("%s: HISR_TXBCNERR\n", __func__);
		#endif
		

		if(check_fwstate(pmlmepriv, WIFI_AP_STATE))
	{
			//send_beacon(padapter);
			if(pmlmepriv->update_bcn == _TRUE)
	{
				//tx_beacon_hdl(padapter, NULL);
				set_tx_beacon_cmd(padapter);
	}
	}
#ifdef CONFIG_CONCURRENT_MODE
		if(check_buddy_fwstate(padapter, WIFI_AP_STATE))
	{
			//send_beacon(padapter);
			if(padapter->pbuddy_adapter->mlmepriv.update_bcn == _TRUE)
	{
				//tx_beacon_hdl(padapter, NULL);
				set_tx_beacon_cmd(padapter->pbuddy_adapter);
	}
	}
#endif

	}
#endif //CONFIG_INTERRUPT_BASED_TXBCN




#ifdef DBG_CONFIG_ERROR_DETECT_INT
	if(  pHalData->IntArray[1]  & IMR_TXERR_8723B )
		DBG_871X("===> %s Tx Error Flag Interrupt Status \n",__FUNCTION__);
	if(  pHalData->IntArray[1]  & IMR_RXERR_8723B )
		DBG_871X("===> %s Rx Error Flag INT Status \n",__FUNCTION__);
	if(  pHalData->IntArray[1]  & IMR_TXFOVW_8723B )
		DBG_871X("===> %s Transmit FIFO Overflow \n",__FUNCTION__);
	if(  pHalData->IntArray[1]  & IMR_RXFOVW_8723B )
		DBG_871X("===> %s Receive FIFO Overflow \n",__FUNCTION__);
#endif//DBG_CONFIG_ERROR_DETECT_INT


	// C2H Event 
	if(pbuf[0]!= 0){
		_rtw_memcpy(&(pHalData->C2hArray[0]), &(pbuf[USB_INTR_CONTENT_C2H_OFFSET]), 16);		
		//rtw_c2h_wk_cmd(padapter); to do..
	}		

		}
#endif

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
static void usb_read_interrupt_complete(struct urb *purb, struct pt_regs *regs)
{
	int	err;
	_adapter		*padapter = (_adapter	 *)purb->context;

	if (RTW_CANNOT_RX(padapter))
	{
		DBG_8192C("%s() RX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) \n", 
		__FUNCTION__,padapter->bDriverStopped, padapter->bSurpriseRemoved);

		return;
	}

	if (purb->status == 0)//SUCCESS
	{
		if (purb->actual_length > INTERRUPT_MSG_FORMAT_LEN)
		{
			DBG_8192C("usb_read_interrupt_complete: purb->actual_length > INTERRUPT_MSG_FORMAT_LEN(%d)\n",INTERRUPT_MSG_FORMAT_LEN);			
		}

		interrupt_handler_8723bu(padapter, purb->actual_length,purb->transfer_buffer );

		err = usb_submit_urb(purb, GFP_ATOMIC);
		if ((err) && (err != (-EPERM)))
		{
			DBG_8192C("cannot submit interrupt in-token(err = 0x%08x),urb_status = %d\n",err, purb->status);
		}
	}
	else
	{
		DBG_8192C("###=> usb_read_interrupt_complete => urb status(%d)\n", purb->status);

		switch(purb->status) {
			case -EINVAL:
			case -EPIPE:
			case -ENODEV:
			case -ESHUTDOWN:
				//padapter->bSurpriseRemoved = _TRUE;
				//RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("usb_read_port_complete:bSurpriseRemoved=TRUE\n"));
			case -ENOENT:
				padapter->bDriverStopped = _TRUE;
				RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("usb_read_port_complete:bDriverStopped=TRUE\n"));
				break;
			case -EPROTO:
				break;
			case -EINPROGRESS:
				DBG_8192C("ERROR: URB IS IN PROGRESS!/n");
				break;
			default:
				break;
		}
	}

}

static u32 usb_read_interrupt(struct intf_hdl *pintfhdl, u32 addr)
{
	int	err;
	unsigned int pipe;
	u32	ret = _SUCCESS;
	_adapter			*adapter = pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(adapter);
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;

_func_enter_;

	if (RTW_CANNOT_RX(adapter))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_interrupt:( RTW_CANNOT_RX )!!!\n"));
		return _FAIL;
	}

	//translate DMA FIFO addr to pipehandle
	pipe = ffaddr2pipehdl(pdvobj, addr);

	usb_fill_int_urb(precvpriv->int_in_urb, pusbd, pipe, 
					precvpriv->int_in_buf,
            				INTERRUPT_MSG_FORMAT_LEN,
            				usb_read_interrupt_complete,
            				adapter,
            				1);

	err = usb_submit_urb(precvpriv->int_in_urb, GFP_ATOMIC);
	if((err) && (err != (-EPERM)))
	{
		DBG_8192C("cannot submit interrupt in-token(err = 0x%08x),urb_status = %d\n",err, precvpriv->int_in_urb->status);
		ret = _FAIL;
	}

_func_exit_;

	return ret;
}
#endif


static s32 pre_recv_entry(union recv_frame *precvframe, struct phy_stat *pphy_status)
{
	s32 ret=_SUCCESS;
#ifdef CONFIG_CONCURRENT_MODE
	u8 *primary_myid, *secondary_myid, *paddr1;
	union recv_frame	*precvframe_if2 = NULL;
	_adapter *primary_padapter = precvframe->u.hdr.adapter;
	_adapter *secondary_padapter = primary_padapter->pbuddy_adapter;
	struct recv_priv *precvpriv = &primary_padapter->recvpriv;
	_queue *pfree_recv_queue = &precvpriv->free_recv_queue;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(primary_padapter);

	if(!secondary_padapter)
		return ret;

	paddr1 = GetAddr1Ptr(precvframe->u.hdr.rx_data);

	if(IS_MCAST(paddr1) == _FALSE)//unicast packets
	{
		//primary_myid = myid(&primary_padapter->eeprompriv);
		secondary_myid = myid(&secondary_padapter->eeprompriv);

		if(_rtw_memcmp(paddr1, secondary_myid, ETH_ALEN))
		{
			//change to secondary interface
			precvframe->u.hdr.adapter = secondary_padapter;
		}

		//ret = recv_entry(precvframe);

	}
	else // Handle BC/MC Packets
	{
		//clone/copy to if2
		_pkt	 *pkt_copy = NULL;
		struct rx_pkt_attrib *pattrib = NULL;

		precvframe_if2 = rtw_alloc_recvframe(pfree_recv_queue);

		if(!precvframe_if2)
			return _FAIL;

		precvframe_if2->u.hdr.adapter = secondary_padapter;
		_rtw_memcpy(&precvframe_if2->u.hdr.attrib, &precvframe->u.hdr.attrib, sizeof(struct rx_pkt_attrib));
		pattrib = &precvframe_if2->u.hdr.attrib;

		//driver need to set skb len for skb_copy().
		//If skb->len is zero, skb_copy() will not copy data from original skb.
		skb_put(precvframe->u.hdr.pkt, pattrib->pkt_len);

		pkt_copy = rtw_skb_copy( precvframe->u.hdr.pkt);
		if (pkt_copy == NULL)
		{
			if((pattrib->mfrag == 1)&&(pattrib->frag_num == 0))
			{
				DBG_8192C("pre_recv_entry(): rtw_skb_copy fail , drop frag frame \n");
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				return ret;
			}

			pkt_copy = rtw_skb_clone( precvframe->u.hdr.pkt);
			if(pkt_copy == NULL)
			{
				DBG_8192C("pre_recv_entry(): rtw_skb_clone fail , drop frame\n");
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				return ret;
			}
		}

		pkt_copy->dev = secondary_padapter->pnetdev;

		precvframe_if2->u.hdr.pkt = pkt_copy;
		precvframe_if2->u.hdr.rx_head = pkt_copy->head;
		precvframe_if2->u.hdr.rx_data = pkt_copy->data;
		precvframe_if2->u.hdr.rx_tail = skb_tail_pointer(pkt_copy);
		precvframe_if2->u.hdr.rx_end = skb_end_pointer(pkt_copy);
		precvframe_if2->u.hdr.len = pkt_copy->len;

		//recvframe_put(precvframe_if2, pattrib->pkt_len);

		if ( pHalData->ReceiveConfig & RCR_APPFCS)
			recvframe_pull_tail(precvframe_if2, IEEE80211_FCS_LEN);

		if (pattrib->physt)
			update_recvframe_phyinfo(precvframe_if2, pphy_status);

		if(rtw_recv_entry(precvframe_if2) != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,
				("recvbuf2recvframe: rtw_recv_entry(precvframe) != _SUCCESS\n"));
		}
	}

	//if (precvframe->u.hdr.attrib.physt)
	//	update_recvframe_phyinfo(precvframe, pphy_status);

	//ret = rtw_recv_entry(precvframe);
#endif

	return ret;

}

#if 0
static s32 pre_recv_entry(union recv_frame *precvframe, u8 *pphy_status)
{	
	s32 ret=_SUCCESS;
#ifdef CONFIG_CONCURRENT_MODE	
	u8 *primary_myid, *secondary_myid, *paddr1;
	union recv_frame	*precvframe_if2 = NULL;
	_adapter *primary_padapter = precvframe->u.hdr.adapter;
	_adapter *secondary_padapter = primary_padapter->pbuddy_adapter;
	struct recv_priv *precvpriv = &primary_padapter->recvpriv;
	_queue *pfree_recv_queue = &precvpriv->free_recv_queue;
	u8	*pbuf = precvframe->u.hdr.rx_data;
	
	if(!secondary_padapter)
		return ret;
	
	paddr1 = GetAddr1Ptr(pbuf);

	if(IS_MCAST(paddr1) == _FALSE)//unicast packets
	{
		//primary_myid = myid(&primary_padapter->eeprompriv);
		secondary_myid = myid(&secondary_padapter->eeprompriv);

		if(_rtw_memcmp(paddr1, secondary_myid, ETH_ALEN))
		{			
			//change to secondary interface
			precvframe->u.hdr.adapter = secondary_padapter;
		}	

		//ret = recv_entry(precvframe);

	}
	else // Handle BC/MC Packets	
	{
		
		u8 clone = _TRUE;
#if 0		
		u8 type, subtype, *paddr2, *paddr3;
	
		type =  GetFrameType(pbuf);
		subtype = GetFrameSubType(pbuf); //bit(7)~bit(2)
		
		switch (type)
		{
			case WIFI_MGT_TYPE: //Handle BC/MC mgnt Packets
				if(subtype == WIFI_BEACON)
				{
					paddr3 = GetAddr3Ptr(precvframe->u.hdr.rx_data);
				
					if (check_fwstate(&secondary_padapter->mlmepriv, _FW_LINKED) &&
						_rtw_memcmp(paddr3, get_bssid(&secondary_padapter->mlmepriv), ETH_ALEN))
					{
						//change to secondary interface
						precvframe->u.hdr.adapter = secondary_padapter;
						clone = _FALSE;
					}

					if(check_fwstate(&primary_padapter->mlmepriv, _FW_LINKED) &&
						_rtw_memcmp(paddr3, get_bssid(&primary_padapter->mlmepriv), ETH_ALEN))
					{
						if(clone==_FALSE)
						{
							clone = _TRUE;									
						}	
						else
						{
							clone = _FALSE;
						}

						precvframe->u.hdr.adapter = primary_padapter;	
					}

					if(check_fwstate(&primary_padapter->mlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) ||
						check_fwstate(&secondary_padapter->mlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING))
					{
						clone = _TRUE;
						precvframe->u.hdr.adapter = primary_padapter;	
					}
				
				}
				else if(subtype == WIFI_PROBEREQ)
				{
					//probe req frame is only for interface2
					//change to secondary interface
					precvframe->u.hdr.adapter = secondary_padapter;
					clone = _FALSE;
				}			
				break;
			case WIFI_CTRL_TYPE: // Handle BC/MC ctrl Packets
			
				break;
			case WIFI_DATA_TYPE: //Handle BC/MC data Packets
					//Notes: AP MODE never rx BC/MC data packets
			
				paddr2 = GetAddr2Ptr(precvframe->u.hdr.rx_data);

				if(_rtw_memcmp(paddr2, get_bssid(&secondary_padapter->mlmepriv), ETH_ALEN))
				{
					//change to secondary interface
					precvframe->u.hdr.adapter = secondary_padapter;
					clone = _FALSE;
				}

				break;
			default:
			
				break;			
		}
#endif

		if(_TRUE == clone)
		{
			//clone/copy to if2		
			struct rx_pkt_attrib *pattrib = NULL;
		
			precvframe_if2 = rtw_alloc_recvframe(pfree_recv_queue);
			if(precvframe_if2)
			{
				precvframe_if2->u.hdr.adapter = secondary_padapter;
		
				_rtw_init_listhead(&precvframe_if2->u.hdr.list);	
				precvframe_if2->u.hdr.precvbuf = NULL;	//can't access the precvbuf for new arch.
				precvframe_if2->u.hdr.len=0;

				_rtw_memcpy(&precvframe_if2->u.hdr.attrib, &precvframe->u.hdr.attrib, sizeof(struct rx_pkt_attrib));

				pattrib = &precvframe_if2->u.hdr.attrib;

				if(rtw_os_alloc_recvframe(secondary_padapter, precvframe_if2, pbuf, NULL) == _SUCCESS)				
				{						
					recvframe_put(precvframe_if2, pattrib->pkt_len);
					//recvframe_pull(precvframe_if2, drvinfo_sz + RXDESC_SIZE);

					if (pattrib->physt && pphy_status)
						rtl8812_query_rx_phy_status(precvframe_if2, pphy_status);
	
					ret = rtw_recv_entry(precvframe_if2);				
				}	
				else
				{
					rtw_free_recvframe(precvframe_if2, pfree_recv_queue);
					DBG_8192C("%s()-%d: alloc_skb() failed!\n", __FUNCTION__, __LINE__);	
				}

			}
			
		}
		
	}
	//if (precvframe->u.hdr.attrib.physt)
	//	rtl8812_query_rx_phy_status(precvframe, pphy_status);

	//ret = rtw_recv_entry(precvframe);

#endif

	return ret;

}
#endif
#ifdef CONFIG_C2H_PACKET_EN
static void rtl8723bu_c2h_packet_handler(PADAPTER padapter, u8 *pbuf, u16 length)
{
	u8 *tmpBuf=NULL;

	if(length == 0)
		return;
	
	DBG_871X("+%s() length=%d\n", __func__, length);

	tmpBuf = rtw_zmalloc(length);
	if (tmpBuf == NULL)
		return;

	_rtw_memcpy(tmpBuf, pbuf, length);

	rtw_c2h_packet_wk_cmd(padapter, tmpBuf, length);

	DBG_871X("-%s()\n", __func__);

	return;
}
#endif


static int recvbuf2recvframe(_adapter *padapter, 
#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
struct recv_buf *precvbuf
#else
_pkt *pskb
#endif
)
{
	u8	*pbuf;
	u8	pkt_cnt = 0;
	u32	pkt_offset;
	s32	transfer_len;
	u8				*pphy_status = NULL;	
	union recv_frame	*precvframe = NULL;
	struct rx_pkt_attrib	*pattrib = NULL;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	_queue			*pfree_recv_queue = &precvpriv->free_recv_queue;

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	transfer_len = (s32)precvbuf->transfer_len;	
	pbuf = precvbuf->pbuf;
#else
	transfer_len = (s32)pskb->len;	
	pbuf = pskb->data;
#endif//CONFIG_USE_USB_BUFFER_ALLOC_RX

#ifdef CONFIG_USB_RX_AGGREGATION
	pkt_cnt = GET_RX_STATUS_DESC_USB_AGG_PKTNUM_8723B(pbuf);
#endif

	do{		
		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if(precvframe==NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: precvframe==NULL\n"));
			DBG_8192C("%s()-%d: rtw_alloc_recvframe() failed! RX Drop!\n", __FUNCTION__, __LINE__);	
			goto _exit_recvbuf2recvframe;
		}
		if(transfer_len >1500)
		_rtw_init_listhead(&precvframe->u.hdr.list);	
		precvframe->u.hdr.precvbuf = NULL;	//can't access the precvbuf for new arch.
		precvframe->u.hdr.len=0;

		update_recvframe_attrib(padapter,precvframe, (struct recv_stat*)pbuf);

		pattrib = &precvframe->u.hdr.attrib;
		
		if ( (padapter->registrypriv.mp_mode == 0) && ((pattrib->crc_err) || (pattrib->icv_err)))
		{
			DBG_8192C("%s: RX Warning! crc_err=%d icv_err=%d, skip!\n", __FUNCTION__, pattrib->crc_err, pattrib->icv_err);

			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}			

		pkt_offset = RXDESC_SIZE + pattrib->drvinfo_sz + pattrib->shift_sz + pattrib->pkt_len;

		if((pattrib->pkt_len<=0) || (pkt_offset>transfer_len))
		{	
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe: pkt_len<=0\n"));
			DBG_871X("%s()-%d: RX Warning!,pkt_len<=0 or pkt_offset> transfer_len \n", __FUNCTION__, __LINE__);	
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		if(rtw_os_alloc_recvframe(padapter, precvframe, 
			(pbuf+pattrib->shift_sz + pattrib->drvinfo_sz + RXDESC_SIZE), 
#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
			NULL
#else
			pskb
#endif
			) == _FAIL)
		{
			rtw_free_recvframe(precvframe, pfree_recv_queue);

			goto _exit_recvbuf2recvframe;
		}	

		recvframe_put(precvframe, pattrib->pkt_len);
		//recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);	

		if(pattrib->pkt_rpt_type == NORMAL_RX)//Normal rx packet
		{

			if(pattrib->physt)
					pphy_status = (pbuf + RXDESC_OFFSET);
#ifdef CONFIG_CONCURRENT_MODE
			if(rtw_buddy_adapter_up(padapter))
		{
				if(pre_recv_entry(precvframe, (struct phy_stat*)pphy_status) != _SUCCESS)
		{
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,
						("recvbuf2recvframe: recv_entry(precvframe) != _SUCCESS\n"));
				}
		}
#endif //CONFIG_CONCURRENT_MODE

		if (pattrib->physt && pphy_status)
			update_recvframe_phyinfo(precvframe, (struct phy_stat*)pphy_status);


		if(rtw_recv_entry(precvframe) != _SUCCESS)
		{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,
					("recvbuf2recvframe: rtw_recv_entry(precvframe) != _SUCCESS\n"));
		}

		}
		else{ // pkt_rpt_type == TX_REPORT1-CCX, TX_REPORT2-TX RTP,HIS_REPORT-USB HISR RTP
			if (pattrib->pkt_rpt_type == C2H_PACKET) {
				C2H_EVT_HDR 	C2hEvent;
				
				u16 len_c2h = pattrib->pkt_len;
				u8 *pbuf_c2h = precvframe->u.hdr.rx_data;
				u8 *pdata_c2h;				

				C2hEvent.CmdID = pbuf_c2h[0];
				C2hEvent.CmdSeq = pbuf_c2h[1];
				C2hEvent.CmdLen = (len_c2h -2);
				pdata_c2h = pbuf_c2h+2;

				if(C2hEvent.CmdID == C2H_CCX_TX_RPT)
				{
					CCX_FwC2HTxRpt_8723b(padapter, pdata_c2h, C2hEvent.CmdLen);
				}
				else
				{
#ifdef CONFIG_C2H_PACKET_EN
					rtl8723bu_c2h_packet_handler(padapter, precvframe->u.hdr.rx_data, pattrib->pkt_len);
#endif
				}
				DBG_8192C("rx C2H_PACKET \n");
			
			}
			rtw_free_recvframe(precvframe, pfree_recv_queue);
		}

#ifdef CONFIG_USB_RX_AGGREGATION
		// jaguar 8-byte alignment
		pkt_offset = (u16)_RND8(pkt_offset);
		pkt_cnt--;
		pbuf += pkt_offset;	
#endif
		transfer_len -= pkt_offset;
		precvframe = NULL;

	}while(transfer_len>0);

_exit_recvbuf2recvframe:

	return _SUCCESS;	
}


#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
void rtl8723bu_recv_tasklet(void *priv)
{	
	struct recv_buf *precvbuf = NULL;
	_adapter	*padapter = (_adapter*)priv;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	while (NULL != (precvbuf = rtw_dequeue_recvbuf(&precvpriv->recv_buf_pending_queue)))
	{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
		{
			DBG_8192C("recv_tasklet => bDriverStopped or bSurpriseRemoved \n");
			
			break;
		}
		

		recvbuf2recvframe(padapter, precvbuf);

		rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
	}	
	
}

static void usb_read_port_complete(struct urb *purb, struct pt_regs *regs)
{	
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;	
	_adapter 			*padapter =(_adapter *)precvbuf->adapter;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete!!!\n"));
	
	precvpriv->rx_pending_cnt --;
		
	if (RTW_CANNOT_RX(padapter))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)\n", padapter->bDriverStopped, padapter->bSurpriseRemoved));		

		goto exit;
	}

	if(purb->status==0)//SUCCESS
	{
		if ((purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: (purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE)\n"));

			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		}
		else 
		{			
			rtw_reset_continual_io_error(adapter_to_dvobj(padapter));
			
			precvbuf->transfer_len = purb->actual_length;	

			//rtw_enqueue_rx_transfer_buffer(precvpriv, rx_transfer_buf);			
			rtw_enqueue_recvbuf(precvbuf, &precvpriv->recv_buf_pending_queue);

			tasklet_schedule(&precvpriv->recv_tasklet);			
		}		
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete : purb->status(%d) != 0 \n", purb->status));
	
		DBG_8192C("###=> usb_read_port_complete => urb status(%d)\n", purb->status);

		if(rtw_inc_and_chk_continual_io_error(adapter_to_dvobj(padapter)) == _TRUE ){
			padapter->bSurpriseRemoved = _TRUE;
		}

		switch(purb->status) {
			case -EINVAL:
			case -EPIPE:			
			case -ENODEV:
			case -ESHUTDOWN:
				//padapter->bSurpriseRemoved=_TRUE;
				//RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bSurpriseRemoved=TRUE\n"));
			case -ENOENT:
				padapter->bDriverStopped=_TRUE;			
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped=TRUE\n"));
				break;
			case -EPROTO:
			case -EILSEQ:
			case -ETIME:
			case -ECOMM:
			case -EOVERFLOW:
				#ifdef DBG_CONFIG_ERROR_DETECT	
				{	
					HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
					pHalData->srestpriv.Wifi_Error_Status = USB_READ_PORT_FAIL;			
				}
				#endif
				rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);			
				break;
			case -EINPROGRESS:
				DBG_8192C("ERROR: URB IS IN PROGRESS!/n");
				break;
			default:
				break;				
		}
		
	}	

exit:	
	
_func_exit_;
	
}

static u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{		
	int err;
	unsigned int pipe;
	u32 ret = _SUCCESS;
	PURB purb = NULL;	
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	_adapter		*adapter = pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(adapter);
	struct pwrctrl_priv *pwrctl = dvobj_to_pwrctl(pdvobj);
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;

_func_enter_;
	
	if (RTW_CANNOT_RX(adapter) || (precvbuf == NULL))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:( RTW_CANNOT_RX ) || precvbuf == NULL!!!\n"));
		return _FAIL;
	}

	rtl8723bu_init_recvbuf(adapter, precvbuf);

	if(precvbuf->pbuf)
	{			
		precvpriv->rx_pending_cnt++;
	
		purb = precvbuf->purb;		

		//translate DMA FIFO addr to pipehandle
		pipe = ffaddr2pipehdl(pdvobj, addr);	

		usb_fill_bulk_urb(purb, pusbd, pipe, 
					precvbuf->pbuf,
            				MAX_RECVBUF_SZ,
            				usb_read_port_complete,
            				precvbuf);//context is precvbuf

		purb->transfer_dma = precvbuf->dma_transfer_addr;
		purb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;								

		err = usb_submit_urb(purb, GFP_ATOMIC);	
		if((err) && (err != (-EPERM)))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("cannot submit rx in-token(err=0x%.8x), URB_STATUS =0x%.8x", err, purb->status));
			DBG_8192C("cannot submit rx in-token(err = 0x%08x),urb_status = %d\n",err,purb->status);
			ret = _FAIL;
		}
		
	}

_func_exit_;

	return ret;
}
#else	// CONFIG_USE_USB_BUFFER_ALLOC_RX


void rtl8723bu_recv_tasklet(void *priv)
{
	_pkt			*pskb;
	_adapter		*padapter = (_adapter*)priv;
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	
	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue)))
	{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
		{
			DBG_8192C("recv_tasklet => bDriverStopped or bSurpriseRemoved \n");
			rtw_skb_free(pskb);
			break;
		}
	
		recvbuf2recvframe(padapter, pskb);

#ifdef CONFIG_PREALLOC_RECV_SKB

		skb_reset_tail_pointer(pskb);

		pskb->len = 0;
		
		skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
		
#else
		rtw_skb_free(pskb);
#endif
				
	}
	
}


static void usb_read_port_complete(struct urb *purb, struct pt_regs *regs)
{
	_irqL irqL;
	uint isevt, *pbuf;
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;	
	_adapter 			*padapter =(_adapter *)precvbuf->adapter;
	struct recv_priv	*precvpriv = &padapter->recvpriv;	
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete!!!\n"));
	
	//_enter_critical(&precvpriv->lock, &irqL);
	//precvbuf->irp_pending=_FALSE;
	//precvpriv->rx_pending_cnt --;
	//_exit_critical(&precvpriv->lock, &irqL);
		
	precvpriv->rx_pending_cnt --;
		
	//if(precvpriv->rx_pending_cnt== 0)
	//{		
	//	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: rx_pending_cnt== 0, set allrxreturnevt!\n"));
	//	_rtw_up_sema(&precvpriv->allrxreturnevt);	
	//}

	if(RTW_CANNOT_RX(padapter))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)\n", padapter->bDriverStopped, padapter->bSurpriseRemoved));		
		
	#ifdef CONFIG_PREALLOC_RECV_SKB
		precvbuf->reuse = _TRUE;
	#else
		if(precvbuf->pskb){
			DBG_8192C("==> free skb(%p)\n",precvbuf->pskb);
			rtw_skb_free(precvbuf->pskb);
		}	
	#endif
		DBG_871X("%s()-%d: RX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) \n", 
		__FUNCTION__, __LINE__,padapter->bDriverStopped, padapter->bSurpriseRemoved);
		goto exit;
	}

	if(purb->status==0)//SUCCESS
	{
		if ((purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: (purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE)\n"));
			precvbuf->reuse = _TRUE;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
			DBG_8192C("%s()-%d: RX Warning!\n", __FUNCTION__, __LINE__);	
		}
		else 
		{	
			rtw_reset_continual_io_error(adapter_to_dvobj(padapter));
			
			precvbuf->transfer_len = purb->actual_length;			
			skb_put(precvbuf->pskb, purb->actual_length);	
			skb_queue_tail(&precvpriv->rx_skb_queue, precvbuf->pskb);

			if (skb_queue_len(&precvpriv->rx_skb_queue)<=1)
				tasklet_schedule(&precvpriv->recv_tasklet);

			precvbuf->pskb = NULL;
			precvbuf->reuse = _FALSE;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);			
		}		
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete : purb->status(%d) != 0 \n", purb->status));
	
		DBG_8192C("###=> usb_read_port_complete => urb status(%d)\n", purb->status);

		if(rtw_inc_and_chk_continual_io_error(adapter_to_dvobj(padapter)) == _TRUE ){
			padapter->bSurpriseRemoved = _TRUE;
		}

		switch(purb->status) {
			case -EINVAL:
			case -EPIPE:			
			case -ENODEV:
			case -ESHUTDOWN:
				//padapter->bSurpriseRemoved=_TRUE;
				//RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bSurpriseRemoved=TRUE\n"));
			case -ENOENT:
				padapter->bDriverStopped=_TRUE;			
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped=TRUE\n"));
				break;
			case -EPROTO:
			case -EILSEQ:
			case -ETIME:
			case -ECOMM:
			case -EOVERFLOW:
				#ifdef DBG_CONFIG_ERROR_DETECT	
				{	
					HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
					pHalData->srestpriv.Wifi_Error_Status = USB_READ_PORT_FAIL;			
				}
				#endif
				precvbuf->reuse = _TRUE;
				rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);			
				break;
			case -EINPROGRESS:
				DBG_8192C("ERROR: URB IS IN PROGRESS!/n");
				break;
			default:
				break;				
		}
		
	}	

exit:	
	
_func_exit_;
	
}

static u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{	
	_irqL irqL;
	int err;
	unsigned int pipe;
	SIZE_PTR tmpaddr=0;
	SIZE_PTR alignment=0;
	u32 ret = _SUCCESS;
	PURB purb = NULL;
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	_adapter		*adapter = pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(adapter);
	struct pwrctrl_priv *pwrctl = dvobj_to_pwrctl(pdvobj);
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;
	

_func_enter_;

	if (RTW_CANNOT_RX(adapter) || (precvbuf == NULL))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:( RTW_CANNOT_RX ) || precvbuf == NULL!!!\n"));
		return _FAIL;
	}

#ifdef CONFIG_PREALLOC_RECV_SKB
	if((precvbuf->reuse == _FALSE) || (precvbuf->pskb == NULL))
	{
		if (NULL != (precvbuf->pskb = skb_dequeue(&precvpriv->free_recv_skb_queue)))
		{
			precvbuf->reuse = _TRUE;
		}
	}
#endif

	rtl8723bu_init_recvbuf(adapter, precvbuf);		

	//re-assign for linux based on skb
	if((precvbuf->reuse == _FALSE) || (precvbuf->pskb == NULL))
	{
		precvbuf->pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);

		if(precvbuf->pskb == NULL)		
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("init_recvbuf(): alloc_skb fail!\n"));
			DBG_871X("#### usb_read_port() alloc_skb fail!#####\n");
			return _FAIL;
		}	

		tmpaddr = (SIZE_PTR)precvbuf->pskb->data;
        	alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
		skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));

		precvbuf->phead = precvbuf->pskb->head;
	   	precvbuf->pdata = precvbuf->pskb->data;
		precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
		precvbuf->pend = skb_end_pointer(precvbuf->pskb);
		precvbuf->pbuf = precvbuf->pskb->data;
	}	
	else//reuse skb
	{
		precvbuf->phead = precvbuf->pskb->head;
		precvbuf->pdata = precvbuf->pskb->data;
		precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
		precvbuf->pend = skb_end_pointer(precvbuf->pskb);
   		precvbuf->pbuf = precvbuf->pskb->data;

		precvbuf->reuse = _FALSE;
	}

	//_enter_critical(&precvpriv->lock, &irqL);
	//precvpriv->rx_pending_cnt++;
	//precvbuf->irp_pending = _TRUE;
	//_exit_critical(&precvpriv->lock, &irqL);

	precvpriv->rx_pending_cnt++;

	purb = precvbuf->purb;

	//translate DMA FIFO addr to pipehandle
	pipe = ffaddr2pipehdl(pdvobj, addr);

	usb_fill_bulk_urb(purb, pusbd, pipe, 
					precvbuf->pbuf,
            				MAX_RECVBUF_SZ,
            				usb_read_port_complete,
            				precvbuf);//context is precvbuf

	err = usb_submit_urb(purb, GFP_ATOMIC);
	if((err) && (err != (-EPERM)))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("cannot submit rx in-token(err=0x%.8x), URB_STATUS =0x%.8x", err, purb->status));
		DBG_8192C("cannot submit rx in-token(err = 0x%08x),urb_status = %d\n",err,purb->status);
		ret = _FAIL;
	}

_func_exit_;

	return ret;
}
#endif	// CONFIG_USE_USB_BUFFER_ALLOC_RX


void rtl8723bu_xmit_tasklet(void *priv)
{	
	int ret = _FALSE;
	_adapter *padapter = (_adapter*)priv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if(check_fwstate(&padapter->mlmepriv, _FW_UNDER_SURVEY) == _TRUE)
		return;

	while(1)
	{
		if (RTW_CANNOT_TX(padapter))
		{
			DBG_8192C("xmit_tasklet => bDriverStopped or bSurpriseRemoved or bWritePortCancel\n");
			break;
		}

		ret = rtl8723bu_xmitframe_complete(padapter, pxmitpriv, NULL);

		if(ret==_FALSE)
			break;
		
	}
	
}



void rtl8723bu_set_intf_ops(struct _io_ops	*pops)
{
	_func_enter_;
	
	_rtw_memset((u8 *)pops, 0, sizeof(struct _io_ops));	

	pops->_read8 = &usb_read8;
	pops->_read16 = &usb_read16;
	pops->_read32 = &usb_read32;
	pops->_read_mem = &usb_read_mem;
	pops->_read_port = &usb_read_port;
	
	pops->_write8 = &usb_write8;
	pops->_write16 = &usb_write16;
	pops->_write32 = &usb_write32;
	pops->_writeN = &usb_writeN;
	
#ifdef CONFIG_USB_SUPPORT_ASYNC_VDN_REQ	
	pops->_write8_async= &usb_async_write8;
	pops->_write16_async = &usb_async_write16;
	pops->_write32_async = &usb_async_write32;
#endif	
	pops->_write_mem = &usb_write_mem;
	pops->_write_port = &usb_write_port;

	pops->_read_port_cancel = &usb_read_port_cancel;
	pops->_write_port_cancel = &usb_write_port_cancel;

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	pops->_read_interrupt = &usb_read_interrupt;
#endif

	_func_exit_;

}
void rtl8723bu_set_hw_type(_adapter *padapter)
{
	padapter->HardwareType = HARDWARE_TYPE_RTL8723BU;
	DBG_871X("CHIP TYPE: RTL8723BU\n");
}
