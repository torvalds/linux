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
 *
 ******************************************************************************/
#define _HCI_OPS_OS_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>
#include <usb_ops.h>
#include <recv_osdep.h>
#include <rtl8192d_hal.h>

static int usbctrl_vendorreq(struct intf_hdl *pintfhdl, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype)
{
	struct rtw_adapter *padapter = pintfhdl->padapter ;
	struct dvobj_priv  *pdvobjpriv = adapter_to_dvobj(padapter);
	struct usb_device *udev = pdvobjpriv->pusbdev;

	unsigned int pipe;
	int status = 0;
	u32 tmp_buflen=0;
	u8 reqtype;
	u8 *pIo_buf;
	int vendorreq_times = 0;
	u8 *tmp_buf;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->adapter_type > PRIMARY_ADAPTER) {
		padapter = padapter->pbuddy_adapter;
		pdvobjpriv = adapter_to_dvobj(padapter);
		udev = pdvobjpriv->pusbdev;
	}
#endif

	if ((padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx)) {
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usbctrl_vendorreq:(padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		status = -EPERM;
		goto exit;
	}

	if (len>MAX_VENDOR_REQ_CMD_SIZE) {
		DBG_8192D("[%s] Buffer len error ,vendor request failed\n", __func__);
		status = -EINVAL;
		goto exit;
	}

	_enter_critical_mutex(&pdvobjpriv->usb_vendor_req_mutex);

	/*  Acquire IO memory for vendorreq */
	pIo_buf = pdvobjpriv->usb_vendor_req_buf;

	if (pIo_buf== NULL) {
		DBG_8192D("[%s] pIo_buf == NULL\n", __func__);
		status = -ENOMEM;
		goto release_mutex;
	}

	while (++vendorreq_times<= MAX_USBCTRL_VENDORREQ_TIMES) {
		memset(pIo_buf, 0, len);

		if (requesttype == 0x01) {
			pipe = usb_rcvctrlpipe(udev, 0);/* read_in */
			reqtype =  REALTEK_USB_VENQT_READ;
		} else {
			pipe = usb_sndctrlpipe(udev, 0);/* write_out */
			reqtype =  REALTEK_USB_VENQT_WRITE;
			memcpy(pIo_buf, pdata, len);
		}

		status = rtw_usb_control_msg(udev, pipe, request, reqtype, value, index, pIo_buf, len, RTW_USB_CONTROL_MSG_TIMEOUT);

		if (status == len) {   /*  Success this control transfer. */
			rtw_reset_continual_urb_error(pdvobjpriv);
			if (requesttype == 0x01) {
				/* For Control read transfer, we have to copy
				 * the read data from pIo_buf to pdata.
				 */
				memcpy(pdata, pIo_buf,  len);
			}
		} else { /*  error cases */
			DBG_8192D("reg 0x%x, usb %s %u fail, status:%d value=0x%x, vendorreq_times:%d\n",
				  value,(requesttype == 0x01) ? "read" : "write",
				  len, status, *(u32 *)pdata, vendorreq_times);

			if (status < 0) {
				if (status == (-ESHUTDOWN) ||
				    status == -ENODEV) {
					padapter->bSurpriseRemoved = true;
				} else {
					#ifdef DBG_CONFIG_ERROR_DETECT
					struct hal_data_8192du	*pHalData = GET_HAL_DATA(padapter);
					pHalData->srestpriv.Wifi_Error_Status = USB_VEN_REQ_CMD_FAIL;
					#endif
				}
			} else { /*  status != len && status >= 0 */
				if (status > 0) {
					if (requesttype == 0x01) {
						/* For Control read transfer,
						 * we have to copy the read data
						 * from pIo_buf to pdata.
						 */
						memcpy(pdata, pIo_buf,  len);
					}
				}
			}

			if (rtw_inc_and_chk_continual_urb_error(pdvobjpriv) == true) {
				padapter->bSurpriseRemoved = true;
				break;
			}

		}

		/*  firmware download is checksumed, don't retry */
		if ((value >= FW_8192D_START_ADDRESS && value <= FW_8192D_END_ADDRESS) || status == len)
			break;

	}

	/*  release IO memory used by vendorreq */
	kfree(tmp_buf);

release_mutex:
	_exit_critical_mutex(&pdvobjpriv->usb_vendor_req_mutex);
exit:
	return status;
}

static void usb_read_reg_rf_byfw(struct intf_hdl *pintfhdl, u16 byteCount, u32 registerIndex, void *buffer)
{
	u16	wPage = 0x0000, offset;
	u32	BufferLengthRead;
	struct rtw_adapter *	adapter = pintfhdl->padapter;
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);
	u8	RFPath=0,nPHY=0;

	RFPath =(u8) ((registerIndex&0xff0000)>>16);

	if (pHalData->interfaceIndex!=0)
	{
		nPHY = 1; /* MAC1 */
		if (registerIndex&MAC1_ACCESS_PHY0)/*  MAC1 need to access PHY0 */
			nPHY = 0;
	}
	else
	{
		if (registerIndex&MAC0_ACCESS_PHY1)
			nPHY = 1;
	}
	registerIndex &= 0xFF;
	wPage = ((nPHY<<7)|(RFPath<<5)|8)<<8;
	offset = (u16)registerIndex;

	/*  */
	/*  IN a vendor request to read back MAC register. */
	/*  */
	usbctrl_vendorreq(pintfhdl, 0x05, offset, wPage, buffer, byteCount, 0x01);
}

static void usb_read_reg(struct intf_hdl *pintfhdl, u16 value, void *pdata, u16 len)
{
	struct rtw_adapter		*padapter = pintfhdl->padapter;
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(padapter);
	u8	request;
	u8	requesttype;
	u16	index;

	request = 0x05;
	requesttype = 0x01;/* read_in */
	index = 0;/* n/a */

	if (pHalData->interfaceIndex!=0)
	{
		if (value<0x1000)
			value|=0x4000;
		else if ((value&MAC1_ACCESS_PHY0) && !(value&0x8000))
			value &= 0xFFF;
	}

	usbctrl_vendorreq(pintfhdl, request, value, index, pdata, len, requesttype);
}

static int usb_write_reg(struct intf_hdl *pintfhdl, u16 value, void *pdata, u16 len)
{
	struct rtw_adapter		*padapter = pintfhdl->padapter;
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(padapter);
	u8	request;
	u8	requesttype;
	u16	index;

	request = 0x05;
	requesttype = 0x00;/* write_out */
	index = 0;/* n/a */

	if (pHalData->interfaceIndex!=0)
	{
		if (value<0x1000)
			value|=0x4000;
		else if ((value&MAC1_ACCESS_PHY0) && !(value&0x8000))/*  MAC1 need to access PHY0 */
			value &= 0xFFF;
	}

	return usbctrl_vendorreq(pintfhdl, request, value, index, pdata, len, requesttype);
}

static u8 usb_read8(struct intf_hdl *pintfhdl, u32 addr)
{
	u16 wvalue;
	u16 len;
	__le32 data;

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;

	usb_read_reg(pintfhdl, wvalue, &data, len);

	return (u8)(le32_to_cpu(data)&0x0ff);
}

static u16 usb_read16(struct intf_hdl *pintfhdl, u32 addr)
{
	u16 wvalue;
	u16 len;
	__le32 data;

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;

	usb_read_reg(pintfhdl, wvalue, &data, len);

	return (u16)(le32_to_cpu(data)&0xffff);
}

static u32 usb_read32(struct intf_hdl *pintfhdl, u32 addr)
{
	u16 wvalue;
	u16 len;
	__le32 data;

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;

	if ((addr&0xff000000)>>24 == 0x66) {
		usb_read_reg_rf_byfw(pintfhdl, len, addr, &data);
	}
	else {
		usb_read_reg(pintfhdl, wvalue, &data, len);
	}

	return le32_to_cpu(data);
}

static int usb_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{
	u16 wvalue;
	u16 len;
	__le32 data;
	int ret;

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;

	data = cpu_to_le32(val & 0x000000ff);

	ret = usb_write_reg(pintfhdl, wvalue, &data, len);

	return ret;
}

static int usb_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{
	u16 wvalue;
	u16 len;
	__le32 data;
	int ret;

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;

	data = cpu_to_le32(val & 0x0000ffff);

	ret = usb_write_reg(pintfhdl, wvalue, &data, len);

	return ret;
}

static int usb_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	u16 wvalue;
	u16 len;
	__le32 data;
	int ret;

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;
	data = cpu_to_le32(val);

	ret = usb_write_reg(pintfhdl, wvalue, &data, len);

	return ret;
}

static int usb_writeN(struct intf_hdl *pintfhdl, u32 addr, u32 length, u8 *pdata)
{
	u16	wvalue;
	u16	len;
	u8	buf[VENDOR_CMD_MAX_DATA_LEN]={0};
	int	ret;

	wvalue = (u16)(addr&0x0000ffff);
	len = length;
	 memcpy(buf, pdata, len);

	ret = usb_write_reg(pintfhdl, wvalue, buf, len);

	return ret;
}

static s32 pre_recv_entry(struct recv_frame_hdr *precvframe, struct recv_stat *prxstat, struct phy_stat *pphy_info)
{
	s32 ret=_SUCCESS;
#ifdef CONFIG_CONCURRENT_MODE
	u8 *primary_myid, *secondary_myid, *paddr1;
	struct recv_frame_hdr	*precvframe_if2 = NULL;
	struct rtw_adapter *primary_padapter = precvframe->adapter;
	struct rtw_adapter *secondary_padapter = primary_padapter->pbuddy_adapter;
	struct recv_priv *precvpriv = &primary_padapter->recvpriv;
	struct __queue *pfree_recv_queue = &precvpriv->free_recv_queue;
	u8	*pbuf = precvframe->rx_data;

	if (!secondary_padapter)
		return ret;

	paddr1 = GetAddr1Ptr(precvframe->rx_data);

	if (IS_MCAST(paddr1) == false)/* unicast packets */
	{
		secondary_myid = myid(&secondary_padapter->eeprompriv);

		if (_rtw_memcmp(paddr1, secondary_myid, ETH_ALEN))
		{
			/* change to secondary interface */
			precvframe->adapter = secondary_padapter;
		}

	}
	else /*  Handle BC/MC Packets */
	{
		u8 clone = true;

		if (true == clone)
		{
			/* clone/copy to if2 */
			u8 shift_sz = 0;
			u32 alloc_sz, skb_len;
			struct sk_buff *pkt_copy = NULL;
			struct rx_pkt_attrib *pattrib = NULL;

			precvframe_if2 = rtw_alloc_recvframe(pfree_recv_queue);
			if (precvframe_if2)
			{
				precvframe_if2->adapter = secondary_padapter;

				INIT_LIST_HEAD(&precvframe_if2->list);
				precvframe_if2->precvbuf = NULL;	/* can't access the precvbuf for new arch. */
				precvframe_if2->len=0;

				memcpy(&precvframe_if2->attrib, &precvframe->attrib, sizeof(struct rx_pkt_attrib));

				pattrib = &precvframe_if2->attrib;

				/*	Modified by Albert 20101213 */
				/*	For 8 bytes IP header alignment. */
				if (pattrib->qos)	/*	Qos data, wireless lan header length is 26 */
				{
					shift_sz = 6;
				}
				else
				{
					shift_sz = 0;
				}

				skb_len = pattrib->pkt_len;

				/*  for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet. */
				/*  modify alloc_sz for recvive crc error packet by thomas 2011-06-02 */
				if ((pattrib->mfrag == 1)&&(pattrib->frag_num == 0)) {
					if (skb_len <= 1650)
						alloc_sz = 1664;
					else
						alloc_sz = skb_len + 14;
				}
				else {
					alloc_sz = skb_len;
					/*	6 is for IP header 8 bytes alignment in QoS packet case. */
					/*	8 is for skb->data 4 bytes alignment. */
					alloc_sz += 14;
				}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) /*  http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html */
				pkt_copy = dev_alloc_skb(alloc_sz);
#else
				pkt_copy = netdev_alloc_skb(secondary_padapter->pnetdev, alloc_sz);
#endif
				if (pkt_copy)
				{
					pkt_copy->dev = secondary_padapter->pnetdev;
					precvframe_if2->pkt = pkt_copy;
					precvframe_if2->rx_head = pkt_copy->data;
					precvframe_if2->rx_end = pkt_copy->data + alloc_sz;
					skb_reserve(pkt_copy, 8 - ((SIZE_PTR)(pkt_copy->data) & 7));/* force pkt_copy->data at 8-byte alignment address */
					skb_reserve(pkt_copy, shift_sz);/* force ip_hdr at 8-byte alignment address according to shift_sz. */
					memcpy(pkt_copy->data, pbuf, skb_len);
					precvframe_if2->rx_data = precvframe_if2->rx_tail = pkt_copy->data;
				}

				recvframe_put(precvframe_if2, skb_len);
				/* recvframe_pull(precvframe_if2, drvinfo_sz + RXDESC_SIZE); */

				rtl8192d_translate_rx_signal_stuff(precvframe_if2, pphy_info);

				ret = rtw_recv_entry(precvframe_if2);

			}

		}

	}

	rtl8192d_translate_rx_signal_stuff(precvframe, pphy_info);

	ret = rtw_recv_entry(precvframe);

#endif

	return ret;
}

static int recvbuf2recvframe(struct rtw_adapter *padapter, struct sk_buff *pskb)
{
	u8	*pbuf;
	u8	shift_sz = 0;
	u16	pkt_cnt;
	u32	pkt_offset, skb_len, alloc_sz;
	int	transfer_len;
	struct recv_stat	*prxstat;
	struct phy_stat	*pphy_info = NULL;
	struct sk_buff *pkt_copy = NULL;
	struct recv_frame_hdr	*precvframe = NULL;
	struct rx_pkt_attrib	*pattrib = NULL;
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(padapter);
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	struct __queue *pfree_recv_queue = &precvpriv->free_recv_queue;

	transfer_len = pskb->len;
	pbuf = pskb->data;

	prxstat = (struct recv_stat *)pbuf;
	pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16) & 0xff;

	do{
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 ("recvbuf2recvframe: rxdesc=offsset 0:0x%08x, 4:0x%08x, 8:0x%08x, C:0x%08x\n",
			  prxstat->rxdw0, prxstat->rxdw1, prxstat->rxdw2, prxstat->rxdw4));

		prxstat = (struct recv_stat *)pbuf;

		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if (precvframe==NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: precvframe==NULL\n"));
			DBG_8192D("%s()-%d: rtw_alloc_recvframe() failed! RX Drop!\n", __func__, __LINE__);
			goto _exit_recvbuf2recvframe;
		}

		INIT_LIST_HEAD(&precvframe->list);
		precvframe->precvbuf = NULL;	/* can't access the precvbuf for new arch. */
		precvframe->len=0;

		rtl8192d_query_rx_desc_status(precvframe, prxstat);

		pattrib = &precvframe->attrib;
		if (pattrib->physt)
		{
			pphy_info = (struct phy_stat *)(pbuf + RXDESC_OFFSET);
		}

		pkt_offset = RXDESC_SIZE + pattrib->drvinfo_sz + pattrib->shift_sz + pattrib->pkt_len;

		if ((pattrib->pkt_len<=0) || (pkt_offset>transfer_len))
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe: pkt_len<=0\n"));
			DBG_8192D("%s()-%d: RX Warning!\n", __func__, __LINE__);
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		/*	Modified by Albert 20101213 */
		/*	For 8 bytes IP header alignment. */
		if (pattrib->qos)	/*	Qos data, wireless lan header length is 26 */
		{
			shift_sz = 6;
		}
		else
		{
			shift_sz = 0;
		}

		skb_len = pattrib->pkt_len;

		/*  for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet. */
		/*  modify alloc_sz for recvive crc error packet by thomas 2011-06-02 */
		if ((pattrib->mfrag == 1)&&(pattrib->frag_num == 0)) {
			if (skb_len <= 1650)
				alloc_sz = 1664;
			else
				alloc_sz = skb_len + 14;
		}
		else {
			alloc_sz = skb_len;
			/*	6 is for IP header 8 bytes alignment in QoS packet case. */
			/*	8 is for skb->data 4 bytes alignment. */
			alloc_sz += 14;
		}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) /*  http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html */
		pkt_copy = dev_alloc_skb(alloc_sz);
#else
		pkt_copy = netdev_alloc_skb(padapter->pnetdev, alloc_sz);
#endif
		if (pkt_copy)
		{
			precvframe->pkt = pkt_copy;
			precvframe->rx_head = pkt_copy->data;
			precvframe->rx_end = pkt_copy->data + alloc_sz;
			skb_reserve(pkt_copy, 8 - ((SIZE_PTR)(pkt_copy->data) & 7));/* force pkt_copy->data at 8-byte alignment address */
			skb_reserve(pkt_copy, shift_sz);/* force ip_hdr at 8-byte alignment address according to shift_sz. */
			memcpy(pkt_copy->data, (pbuf + pattrib->shift_sz + pattrib->drvinfo_sz + RXDESC_SIZE), skb_len);
			precvframe->rx_data = precvframe->rx_tail = pkt_copy->data;
		}
		else
		{
			precvframe->pkt = skb_clone(pskb, GFP_ATOMIC);
			if (pkt_copy)
			{
				precvframe->rx_head = precvframe->rx_data = precvframe->rx_tail = pbuf;
				precvframe->rx_end = pbuf + alloc_sz;
			}
			else
			{
				DBG_8192D("recvbuf2recvframe: skb_clone fail\n");
				rtw_free_recvframe(precvframe, pfree_recv_queue);
				goto _exit_recvbuf2recvframe;
			}
		}

		recvframe_put(precvframe, skb_len);

		switch (pHalData->UsbRxAggMode) {
		case USB_RX_AGG_DMA:
		case USB_RX_AGG_DMA_USB:
			pkt_offset = (u16)_RND128(pkt_offset);
			break;
			case USB_RX_AGG_USB:
			pkt_offset = (u16)_RND4(pkt_offset);
			break;
		case USB_RX_AGG_DISABLE:
		default:
			break;
		}
#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_buddy_adapter_up(padapter)) {
			if (pre_recv_entry(precvframe, prxstat, pphy_info) != _SUCCESS)
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: recv_entry(precvframe) != _SUCCESS\n"));
		} else {
			rtl8192d_translate_rx_signal_stuff(precvframe, pphy_info);
			if (rtw_recv_entry(precvframe) != _SUCCESS)
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: rtw_recv_entry(precvframe) != _SUCCESS\n"));
		}
#else
		rtl8192d_translate_rx_signal_stuff(precvframe, pphy_info);
		if (rtw_recv_entry(precvframe) != _SUCCESS)
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: rtw_recv_entry(precvframe) != _SUCCESS\n"));
#endif /* CONFIG_CONCURRENT_MODE */
		pkt_cnt--;

		transfer_len -= pkt_offset;
		pbuf += pkt_offset;
		precvframe = NULL;
		pkt_copy = NULL;

		if (transfer_len > 0 && pkt_cnt == 0)
			pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16) & 0xff;

	}while ((transfer_len>0) && (pkt_cnt>0));

_exit_recvbuf2recvframe:

	return _SUCCESS;
}

void rtl8192du_recv_tasklet(void *priv)
{
	struct sk_buff *pskb;
	struct rtw_adapter		*padapter = (struct rtw_adapter*)priv;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue)))
	{
		if ((padapter->bDriverStopped == true)||(padapter->bSurpriseRemoved== true))
		{
			DBG_8192D("recv_tasklet => bDriverStopped or bSurpriseRemoved\n");
			dev_kfree_skb_any(pskb);
			break;
		}

		recvbuf2recvframe(padapter, pskb);

#ifdef CONFIG_PREALLOC_RECV_SKB

		skb_reset_tail_pointer(pskb);
		pskb->len = 0;

		skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);

#else
		dev_kfree_skb_any(pskb);
#endif

	}
}

static void usb_read_port_complete(struct urb *purb, struct pt_regs *regs)
{
	uint isevt, *pbuf;
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;
	struct rtw_adapter			*padapter =(struct rtw_adapter *)precvbuf->adapter;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete!!!\n"));

	precvpriv->rx_pending_cnt --;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped||padapter->bReadPortCancel)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)\n", padapter->bDriverStopped, padapter->bSurpriseRemoved));

	#ifdef CONFIG_PREALLOC_RECV_SKB
		precvbuf->reuse = true;
	#else
		if (precvbuf->pskb) {
			DBG_8192D("==> free skb(%p)\n",precvbuf->pskb);
			dev_kfree_skb_any(precvbuf->pskb);
		}
	#endif

		return;
	}

	if (purb->status==0)/* SUCCESS */
	{
		if ((purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: (purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE)\n"));
			precvbuf->reuse = true;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		} else {
			precvbuf->transfer_len = purb->actual_length;

			skb_put(precvbuf->pskb, purb->actual_length);
			skb_queue_tail(&precvpriv->rx_skb_queue, precvbuf->pskb);

			if (skb_queue_len(&precvpriv->rx_skb_queue)<=1)
				tasklet_schedule(&precvpriv->recv_tasklet);

			precvbuf->pskb = NULL;
			precvbuf->reuse = false;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		}
	} else {
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete : purb->status(%d) != 0\n", purb->status));

		DBG_8192D("###=> usb_read_port_complete => urb status(%d)\n", purb->status);

		switch (purb->status) {
		case -EINVAL:
		case -EPIPE:
		case -ENODEV:
		case -ESHUTDOWN:
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bSurpriseRemoved=TRUE\n"));
		case -ENOENT:
			padapter->bDriverStopped=true;
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped=TRUE\n"));
			break;
		case -EPROTO:
		case -EOVERFLOW:
			precvbuf->reuse = true;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
			break;
		case -EINPROGRESS:
			DBG_8192D("ERROR: URB IS IN PROGRESS!/n");
			break;
		default:
			break;
		}

	}

}

static u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	int err, pipe;
	SIZE_PTR tmpaddr=0;
	SIZE_PTR alignment=0;
	u32 ret = _SUCCESS;
	struct urb *purb = NULL;
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	struct rtw_adapter *adapter = pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(adapter);
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;

	if (adapter->bDriverStopped || adapter->bSurpriseRemoved ||
	    adapter->pwrctrlpriv.pnp_bstop_trx) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
			 ("usb_read_port:(padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		return _FAIL;
	}

	if (!precvbuf) {
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:precvbuf ==NULL\n"));
		return _FAIL;
	}
#ifdef CONFIG_PREALLOC_RECV_SKB
	if ((precvbuf->reuse == false) || (precvbuf->pskb == NULL)) {
		if (NULL != (precvbuf->pskb = skb_dequeue(&precvpriv->free_recv_skb_queue)))
			precvbuf->reuse = true;
	}
#endif

	if (precvbuf != NULL) {
		rtl8192du_init_recvbuf(adapter, precvbuf);

		/* re-assign for linux based on skb */
		if ((precvbuf->reuse == false) || (precvbuf->pskb == NULL)) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) /*  http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html */
			precvbuf->pskb = dev_alloc_skb(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
#else
			precvbuf->pskb = netdev_alloc_skb(adapter->pnetdev, MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
#endif
			if (precvbuf->pskb == NULL) {
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("init_recvbuf(): alloc_skb fail!\n"));
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
		else/* reuse skb */
		{
			precvbuf->phead = precvbuf->pskb->head;
			precvbuf->pdata = precvbuf->pskb->data;
			precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
			precvbuf->pend = skb_end_pointer(precvbuf->pskb);
			precvbuf->pbuf = precvbuf->pskb->data;

			precvbuf->reuse = false;
		}

		precvpriv->rx_pending_cnt++;

		purb = precvbuf->purb;

		/* translate DMA FIFO addr to pipehandle */
		pipe = ffaddr2pipehdl(pdvobj, addr);

		usb_fill_bulk_urb(purb, pusbd, pipe,
						precvbuf->pbuf,
						MAX_RECVBUF_SZ,
						usb_read_port_complete,
						precvbuf);/* context is precvbuf */

		err = usb_submit_urb(purb, GFP_ATOMIC);
		if ((err) && (err != (-EPERM))) {
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("cannot submit rx in-token(err=0x%.8x), URB_STATUS =0x%.8x", err, purb->status));
			DBG_8192D("cannot submit rx in-token(err = 0x%08x),urb_status = %d\n",err,purb->status);
			ret = _FAIL;
		}
	}

	return ret;
}

void rtl8192du_xmit_tasklet(void *priv)
{
	int ret = false;
	struct rtw_adapter *padapter = (struct rtw_adapter*)priv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if (check_fwstate(&padapter->mlmepriv, _FW_UNDER_SURVEY) == true
#ifdef CONFIG_DUALMAC_CONCURRENT
		|| (dc_check_xmit(padapter)== false)
#endif
		)
		return;

	while (1) {
		if ((padapter->bDriverStopped) || (padapter->bSurpriseRemoved)) {
			DBG_8192D("xmit_tasklet => bDriverStopped or bSurpriseRemoved\n");
			break;
		}

		ret = rtl8192du_xmitframe_complete(padapter, pxmitpriv, NULL);

		if (ret == false)
			break;
	}
}

void rtl8192du_set_intf_ops(struct _io_ops	*pops)
{

	memset((u8 *)pops, 0, sizeof(struct _io_ops));

	pops->_read8 = &usb_read8;
	pops->_read16 = &usb_read16;
	pops->_read32 = &usb_read32;
	pops->_read_mem = &usb_read_mem;
	pops->_read_port = &usb_read_port;

	pops->_write8 = &usb_write8;
	pops->_write16 = &usb_write16;
	pops->_write32 = &usb_write32;
	pops->_writeN = &usb_writeN;

	pops->_write_mem = &usb_write_mem;
	pops->_write_port = &usb_write_port;

	pops->_read_port_cancel = &usb_read_port_cancel;
	pops->_write_port_cancel = &usb_write_port_cancel;
}
