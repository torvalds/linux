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
#define _HCI_OPS_OS_C_

#include <drv_types.h>
#include <rtl8188e_hal.h>


#ifdef CONFIG_SUPPORT_USB_INT
void interrupt_handler_8188eu(_adapter *padapter,u16 pkt_len,u8 *pbuf)
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
	if(  pHalData->IntArray[1]  & IMR_TXERR_88E )
		DBG_871X("===> %s Tx Error Flag Interrupt Status \n",__FUNCTION__);
	if(  pHalData->IntArray[1]  & IMR_RXERR_88E )
		DBG_871X("===> %s Rx Error Flag INT Status \n",__FUNCTION__);
	if(  pHalData->IntArray[1]  & IMR_TXFOVW_88E )
		DBG_871X("===> %s Transmit FIFO Overflow \n",__FUNCTION__);
	if(  pHalData->IntArray[1]  & IMR_RXFOVW_88E )
		DBG_871X("===> %s Receive FIFO Overflow \n",__FUNCTION__);	
#endif//DBG_CONFIG_ERROR_DETECT_INT


	// C2H Event 
	if(pbuf[0]!= 0){
		_rtw_memcpy(&(pHalData->C2hArray[0]), &(pbuf[USB_INTR_CONTENT_C2H_OFFSET]), 16);		
		//rtw_c2h_wk_cmd(padapter); to do..
	}		
		
}
#endif

static s32 pre_recv_entry(union recv_frame *precvframe, struct recv_stat *prxstat, u8 *pphy_status)
{	
	s32 ret=_SUCCESS;
#ifdef CONFIG_CONCURRENT_MODE	
	u8 *secondary_myid, *paddr1;
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
		secondary_myid = adapter_mac_addr(secondary_padapter);

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
						rx_query_phy_status(precvframe_if2, pphy_status);
	
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
		//rx_query_phy_status(precvframe, pphy_status);
	
	//ret = rtw_recv_entry(precvframe);

#endif

	return ret;

}

int recvbuf2recvframe(PADAPTER padapter, void *ptr)
{
	u8	*pbuf;
	u16	pkt_cnt;
	u32	pkt_offset;
	s32	transfer_len;
	struct recv_stat	*prxstat;
	u8 *pphy_status = NULL;
	union recv_frame	*precvframe = NULL;
	struct rx_pkt_attrib	*pattrib = NULL;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	_queue			*pfree_recv_queue = &precvpriv->free_recv_queue;
	_pkt *pskb;

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	pskb = NULL;
	transfer_len = (s32)((struct recv_buf*)ptr)->transfer_len;
	pbuf = ((struct recv_buf*)ptr)->pbuf;
#else
	pskb = (_pkt*)ptr;
	transfer_len = (s32)pskb->len;
	pbuf = pskb->data;
#endif
	prxstat = (struct recv_stat *)pbuf;
	pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16) & 0xff;

#if 0 //temp remove when disable usb rx aggregation
	if((pkt_cnt > 10) || (pkt_cnt < 1) || (transfer_len<RXDESC_SIZE) ||(pkt_len<=0))
	{		
		return _FAIL;
	}
#endif
	
	do{		
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 ("recvbuf2recvframe: rxdesc=offsset 0:0x%08x, 4:0x%08x, 8:0x%08x, C:0x%08x\n",
			  prxstat->rxdw0, prxstat->rxdw1, prxstat->rxdw2, prxstat->rxdw4));

		prxstat = (struct recv_stat *)pbuf;

		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if(precvframe==NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: precvframe==NULL\n"));
			DBG_8192C("%s()-%d: rtw_alloc_recvframe() failed! RX Drop!\n", __FUNCTION__, __LINE__);	
			goto _exit_recvbuf2recvframe;
		}

		_rtw_init_listhead(&precvframe->u.hdr.list);	
		precvframe->u.hdr.precvbuf = NULL;	//can't access the precvbuf for new arch.
		precvframe->u.hdr.len=0;

		rtl8188e_query_rx_desc_status(precvframe, prxstat);

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
			DBG_8192C("%s()-%d: RX Warning!,pkt_len<=0 or pkt_offset> transfoer_len \n", __FUNCTION__, __LINE__);	
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}
	
		if(rtw_os_alloc_recvframe(padapter, precvframe, 
			(pbuf+pattrib->shift_sz + pattrib->drvinfo_sz + RXDESC_SIZE), pskb) == _FAIL)
		{
			rtw_free_recvframe(precvframe, pfree_recv_queue);

			goto _exit_recvbuf2recvframe;
		}

		recvframe_put(precvframe, pattrib->pkt_len);
		//recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);	

		if(pattrib->pkt_rpt_type == NORMAL_RX)//Normal rx packet
		{
			if(pattrib->physt)
				pphy_status = pbuf + RXDESC_OFFSET;

#ifdef CONFIG_CONCURRENT_MODE
			if(rtw_buddy_adapter_up(padapter))
			{
				if(pre_recv_entry(precvframe, prxstat, pphy_status) != _SUCCESS)
				{
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,
						("recvbuf2recvframe: recv_entry(precvframe) != _SUCCESS\n"));
				}
			}
#endif //CONFIG_CONCURRENT_MODE

			if(pattrib->physt && pphy_status)
				rx_query_phy_status(precvframe, pphy_status);

			if(rtw_recv_entry(precvframe) != _SUCCESS)
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,
					("recvbuf2recvframe: rtw_recv_entry(precvframe) != _SUCCESS\n"));
			}

		}
		else{ // pkt_rpt_type == TX_REPORT1-CCX, TX_REPORT2-TX RTP,HIS_REPORT-USB HISR RTP
				
			//enqueue recvframe to txrtp queue
			if(pattrib->pkt_rpt_type == TX_REPORT1){
				//DBG_8192C("rx CCX \n");
				//CCX-TXRPT ack for xmit mgmt frames.
				handle_txrpt_ccx_88e(padapter, precvframe->u.hdr.rx_data);
			}
			else if(pattrib->pkt_rpt_type == TX_REPORT2){
				//DBG_8192C("recv TX RPT \n");
				ODM_RA_TxRPT2Handle_8188E(
							&pHalData->odmpriv,
							precvframe->u.hdr.rx_data,
							pattrib->pkt_len,
							pattrib->MacIDValidEntry[0],
							pattrib->MacIDValidEntry[1]
							);
				
			}
			else if(pattrib->pkt_rpt_type == HIS_REPORT)
			{
				//DBG_8192C("%s , rx USB HISR \n",__FUNCTION__);
				#ifdef CONFIG_SUPPORT_USB_INT
				interrupt_handler_8188eu(padapter,pattrib->pkt_len,precvframe->u.hdr.rx_data);
				#endif
			}				
			rtw_free_recvframe(precvframe, pfree_recv_queue);			
			
		}
	
#ifdef CONFIG_USB_RX_AGGREGATION
		switch(pHalData->UsbRxAggMode)
		{
			case USB_RX_AGG_DMA:
			case USB_RX_AGG_MIX:
				pkt_offset = (u16)_RND128(pkt_offset);
				break;
				case USB_RX_AGG_USB:
				pkt_offset = (u16)_RND4(pkt_offset);
				break;
			case USB_RX_AGG_DISABLE:
			default:
				break;
		}
#endif
		pkt_cnt--;
		transfer_len -= pkt_offset;
		pbuf += pkt_offset;	
		precvframe = NULL;
	
		if(transfer_len>0 && pkt_cnt==0)
			pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16) & 0xff;

	}while((transfer_len>0) && (pkt_cnt>0));

_exit_recvbuf2recvframe:

	return _SUCCESS;	
}



void rtl8188eu_xmit_tasklet(void *priv)
{	
	int ret = _FALSE;
	_adapter *padapter = (_adapter*)priv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	while(1)
	{
		if (RTW_CANNOT_TX(padapter))
		{
			DBG_8192C("xmit_tasklet => bDriverStopped or bSurpriseRemoved or bWritePortCancel\n");
			break;
		}

		if (rtw_xmit_ac_blocked(padapter) == _TRUE)
			break;

		ret = rtl8188eu_xmitframe_complete(padapter, pxmitpriv, NULL);

		if(ret==_FALSE)
			break;
	}
	
}

void rtl8188eu_set_intf_ops(struct _io_ops	*pops)
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

void rtl8188eu_set_hw_type(struct dvobj_priv *pdvobj)
{
	pdvobj->HardwareType = HARDWARE_TYPE_RTL8188EU;
	DBG_871X("CHIP TYPE: RTL8188E\n");
}

