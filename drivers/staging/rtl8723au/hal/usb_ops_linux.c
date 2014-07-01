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
 ******************************************************************************/
#define _HCI_OPS_OS_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>
#include <usb_ops.h>
#include <recv_osdep.h>
#include <rtl8723a_hal.h>
#include <rtl8723a_recv.h>

static int usbctrl_vendorreq(struct rtw_adapter *padapter, u8 request,
			     u16 value, u16 index, void *pdata, u16 len,
			     u8 requesttype)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct usb_device *udev = pdvobjpriv->pusbdev;
	unsigned int pipe;
	int status = 0;
	u8 reqtype;
	u8 *pIo_buf;
	int vendorreq_times = 0;

	if (padapter->bSurpriseRemoved) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
			 ("usbctrl_vendorreq:(padapter->bSurpriseRemoved)!!!"));
		status = -EPERM;
		goto exit;
	}

	if (len > MAX_VENDOR_REQ_CMD_SIZE) {
		DBG_8723A("[%s] Buffer len error , vendor request failed\n",
			  __func__);
		status = -EINVAL;
		goto exit;
	}

	mutex_lock(&pdvobjpriv->usb_vendor_req_mutex);

	/*  Acquire IO memory for vendorreq */
	pIo_buf = pdvobjpriv->usb_vendor_req_buf;

	if (pIo_buf == NULL) {
		DBG_8723A("[%s] pIo_buf == NULL \n", __func__);
		status = -ENOMEM;
		goto release_mutex;
	}

	while (++vendorreq_times <= MAX_USBCTRL_VENDORREQ_TIMES) {
		memset(pIo_buf, 0, len);

		if (requesttype == 0x01) {
			pipe = usb_rcvctrlpipe(udev, 0);/* read_in */
			reqtype =  REALTEK_USB_VENQT_READ;
		} else {
			pipe = usb_sndctrlpipe(udev, 0);/* write_out */
			reqtype =  REALTEK_USB_VENQT_WRITE;
			memcpy(pIo_buf, pdata, len);
		}

		status = usb_control_msg(udev, pipe, request, reqtype,
					 value, index, pIo_buf, len,
					 RTW_USB_CONTROL_MSG_TIMEOUT);

		if (status == len) {   /*  Success this control transfer. */
			rtw_reset_continual_urb_error(pdvobjpriv);
			if (requesttype == 0x01) {
				/* For Control read transfer, we have to copy
				 * the read data from pIo_buf to pdata.
				 */
				memcpy(pdata, pIo_buf,  len);
			}
		} else { /*  error cases */
			DBG_8723A("reg 0x%x, usb %s %u fail, status:%d value ="
				  " 0x%x, vendorreq_times:%d\n",
				  value, (requesttype == 0x01) ?
				  "read" : "write",
				  len, status, *(u32 *)pdata, vendorreq_times);

			if (status < 0) {
				if (status == -ESHUTDOWN || status == -ENODEV)
					padapter->bSurpriseRemoved = true;
			} else { /*  status != len && status >= 0 */
				if (status > 0) {
					if (requesttype == 0x01) {
						/*
						 * For Control read transfer,
						 * we have to copy the read
						 * data from pIo_buf to pdata.
						 */
						memcpy(pdata, pIo_buf,  len);
					}
				}
			}

			if (rtw_inc_and_chk_continual_urb_error(pdvobjpriv)) {
				padapter->bSurpriseRemoved = true;
				break;
			}
		}

		/*  firmware download is checksumed, don't retry */
		if ((value >= FW_8723A_START_ADDRESS &&
		     value <= FW_8723A_END_ADDRESS) || status == len)
			break;
	}

release_mutex:
	mutex_unlock(&pdvobjpriv->usb_vendor_req_mutex);
exit:
	return status;
}

u8 rtl8723au_read8(struct rtw_adapter *padapter, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u8 data = 0;

	request = 0x05;
	requesttype = 0x01;/* read_in */
	index = 0;/* n/a */

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;

	usbctrl_vendorreq(padapter, request, wvalue, index, &data,
			  len, requesttype);

	return data;
}

u16 rtl8723au_read16(struct rtw_adapter *padapter, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	__le16 data;

	request = 0x05;
	requesttype = 0x01;/* read_in */
	index = 0;/* n/a */

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;

	usbctrl_vendorreq(padapter, request, wvalue, index, &data,
			  len, requesttype);

	return le16_to_cpu(data);
}

u32 rtl8723au_read32(struct rtw_adapter *padapter, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	__le32 data;

	request = 0x05;
	requesttype = 0x01;/* read_in */
	index = 0;/* n/a */

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;

	usbctrl_vendorreq(padapter, request, wvalue, index, &data,
			  len, requesttype);

	return le32_to_cpu(data);
}

int rtl8723au_write8(struct rtw_adapter *padapter, u32 addr, u8 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u8 data;
	int ret;

	request = 0x05;
	requesttype = 0x00;/* write_out */
	index = 0;/* n/a */

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;

	data = val;

	ret = usbctrl_vendorreq(padapter, request, wvalue, index, &data,
				len, requesttype);

	return ret;
}

int rtl8723au_write16(struct rtw_adapter *padapter, u32 addr, u16 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	__le16 data;
	int ret;

	request = 0x05;
	requesttype = 0x00;/* write_out */
	index = 0;/* n/a */

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;

	data = cpu_to_le16(val);

	ret = usbctrl_vendorreq(padapter, request, wvalue, index, &data,
				len, requesttype);
	return ret;
}

int rtl8723au_write32(struct rtw_adapter *padapter, u32 addr, u32 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	__le32 data;
	int ret;

	request = 0x05;
	requesttype = 0x00;/* write_out */
	index = 0;/* n/a */

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;
	data = cpu_to_le32(val);

	ret = usbctrl_vendorreq(padapter, request, wvalue, index, &data,
				len, requesttype);

	return ret;
}

int rtl8723au_writeN(struct rtw_adapter *padapter,
		     u32 addr, u32 length, u8 *pdata)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u8 buf[VENDOR_CMD_MAX_DATA_LEN] = {0};
	int ret;

	request = 0x05;
	requesttype = 0x00;/* write_out */
	index = 0;/* n/a */

	wvalue = (u16)(addr&0x0000ffff);
	len = length;
	memcpy(buf, pdata, len);

	ret = usbctrl_vendorreq(padapter, request, wvalue, index, buf,
				len, requesttype);

	return ret;
}

/*
 * Description:
 *	Recognize the interrupt content by reading the interrupt
 *	register or content and masking interrupt mask (IMR)
 *	if it is our NIC's interrupt. After recognizing, we may clear
 *	the all interrupts (ISR).
 * Arguments:
 *	[in] Adapter -
 *		The adapter context.
 *	[in] pContent -
 *		Under PCI interface, this field is ignord.
 *		Under USB interface, the content is the interrupt
 *		content pointer.
 *		Under SDIO interface, this is the interrupt type which
 *		is Local interrupt or system interrupt.
 *	[in] ContentLen -
 *		The length in byte of pContent.
 * Return:
 *	If any interrupt matches the mask (IMR), return true, and
 *	return false otherwise.
 */
static bool
InterruptRecognized8723AU(struct rtw_adapter *Adapter, void *pContent,
			  u32 ContentLen)
{
	struct hal_data_8723a	*pHalData = GET_HAL_DATA(Adapter);
	u8 *buffer = (u8 *)pContent;
	struct reportpwrstate_parm report;

	memcpy(&pHalData->IntArray[0], &buffer[USB_INTR_CONTENT_HISR_OFFSET],
	       4);
	pHalData->IntArray[0] &= pHalData->IntrMask[0];

	/* For HISR extension. Added by tynli. 2009.10.07. */
	memcpy(&pHalData->IntArray[1],
	       &buffer[USB_INTR_CONTENT_HISRE_OFFSET], 4);
	pHalData->IntArray[1] &= pHalData->IntrMask[1];

	/* We sholud remove this function later because DDK suggest
	 * not to executing too many operations in MPISR  */

	memcpy(&report.state, &buffer[USB_INTR_CPWM_OFFSET], 1);

	return (pHalData->IntArray[0] & pHalData->IntrMask[0]) != 0 ||
		(pHalData->IntArray[1] & pHalData->IntrMask[1]) != 0;
}

static void usb_read_interrupt_complete(struct urb *purb)
{
	int err;
	struct rtw_adapter *padapter = (struct rtw_adapter *)purb->context;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped ||
	    padapter->bReadPortCancel) {
		DBG_8723A("%s() RX Warning! bDriverStopped(%d) OR "
			  "bSurpriseRemoved(%d) bReadPortCancel(%d)\n",
			  __func__, padapter->bDriverStopped,
			  padapter->bSurpriseRemoved,
			  padapter->bReadPortCancel);
		return;
	}

	if (purb->status == 0) {
		struct c2h_evt_hdr *c2h_evt;

		c2h_evt = (struct c2h_evt_hdr *)purb->transfer_buffer;

		if (purb->actual_length > USB_INTR_CONTENT_LENGTH) {
			DBG_8723A("usb_read_interrupt_complete: purb->actual_"
				  "length > USB_INTR_CONTENT_LENGTH\n");
			goto urb_submit;
		}

		InterruptRecognized8723AU(padapter, purb->transfer_buffer,
					  purb->actual_length);

		if (c2h_evt_exist(c2h_evt)) {
			if (c2h_id_filter_ccx_8723a(c2h_evt->id)) {
				/* Handle CCX report here */
				handle_txrpt_ccx_8723a(padapter, (void *)
						       c2h_evt->payload);
				schedule_work(&padapter->evtpriv.irq_wk);
			} else {
				struct evt_work *c2w;
				int res;

				c2w = kmalloc(sizeof(struct evt_work),
						GFP_ATOMIC);

				if (!c2w) {
					printk(KERN_WARNING "%s: unable to "
					       "allocate work buffer\n",
					       __func__);
					goto urb_submit;
				}

				c2w->adapter = padapter;
				INIT_WORK(&c2w->work, rtw_evt_work);
				memcpy(c2w->u.buf, purb->transfer_buffer, 16);

				res = queue_work(padapter->evtpriv.wq,
						 &c2w->work);

				if (!res) {
					printk(KERN_ERR "%s: Call to "
					       "queue_work() failed\n",
					       __func__);
					kfree(c2w);
					goto urb_submit;
				}
			}
		}

urb_submit:
		err = usb_submit_urb(purb, GFP_ATOMIC);
		if (err && (err != -EPERM)) {
			DBG_8723A("cannot submit interrupt in-token(err = "
				  "0x%08x), urb_status = %d\n",
				  err, purb->status);
		}
	} else {
		DBG_8723A("###=> usb_read_interrupt_complete => urb "
			  "status(%d)\n", purb->status);

		switch (purb->status) {
		case -EINVAL:
		case -EPIPE:
		case -ENODEV:
		case -ESHUTDOWN:
			RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
				 ("usb_read_port_complete:bSurpriseRemoved ="
				  "true\n"));
			/* Fall Through here */
		case -ENOENT:
			padapter->bDriverStopped = true;
			RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
				 ("usb_read_port_complete:bDriverStopped ="
				  "true\n"));
			break;
		case -EPROTO:
			break;
		case -EINPROGRESS:
			DBG_8723A("ERROR: URB IS IN PROGRESS!\n");
			break;
		default:
			break;
		}
	}
}

int rtl8723au_read_interrupt(struct rtw_adapter *adapter, u32 addr)
{
	int err;
	unsigned int pipe;
	int ret = _SUCCESS;
	struct dvobj_priv *pdvobj = adapter_to_dvobj(adapter);
	struct recv_priv *precvpriv = &adapter->recvpriv;
	struct usb_device *pusbd = pdvobj->pusbdev;

	/* translate DMA FIFO addr to pipehandle */
	pipe = usb_rcvintpipe(pusbd, pdvobj->RtInPipe[1]);

	usb_fill_int_urb(precvpriv->int_in_urb, pusbd, pipe,
			 precvpriv->int_in_buf, USB_INTR_CONTENT_LENGTH,
			 usb_read_interrupt_complete, adapter, 1);

	err = usb_submit_urb(precvpriv->int_in_urb, GFP_ATOMIC);
	if (err && (err != -EPERM)) {
		DBG_8723A("cannot submit interrupt in-token(err = 0x%08x),"
			  "urb_status = %d\n", err,
			  precvpriv->int_in_urb->status);
		ret = _FAIL;
	}

	return ret;
}

static int recvbuf2recvframe(struct rtw_adapter *padapter, struct sk_buff *pskb)
{
	u8	*pbuf;
	u8	shift_sz = 0;
	u16	pkt_cnt;
	u32	pkt_offset, skb_len, alloc_sz;
	int	transfer_len;
	struct recv_stat *prxstat;
	struct phy_stat	*pphy_info;
	struct sk_buff *pkt_copy;
	struct recv_frame *precvframe;
	struct rx_pkt_attrib *pattrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct rtw_queue *pfree_recv_queue = &precvpriv->free_recv_queue;

	transfer_len = (int)pskb->len;
	pbuf = pskb->data;

	prxstat = (struct recv_stat *)pbuf;
	pkt_cnt = (le32_to_cpu(prxstat->rxdw2) >> 16) & 0xff;

	do {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 ("recvbuf2recvframe: rxdesc = offsset 0:0x%08x, "
			  "4:0x%08x, 8:0x%08x, C:0x%08x\n", prxstat->rxdw0,
			  prxstat->rxdw1, prxstat->rxdw2, prxstat->rxdw4));

		prxstat = (struct recv_stat *)pbuf;

		precvframe = rtw_alloc_recvframe23a(pfree_recv_queue);
		if (!precvframe) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 ("recvbuf2recvframe: precvframe == NULL\n"));
			DBG_8723A("%s()-%d: rtw_alloc_recvframe23a() failed! RX "
				  "Drop!\n", __func__, __LINE__);
			goto _exit_recvbuf2recvframe;
		}

		INIT_LIST_HEAD(&precvframe->list);

		update_recvframe_attrib(precvframe, prxstat);

		pattrib = &precvframe->attrib;

		if (pattrib->crc_err) {
			DBG_8723A("%s()-%d: RX Warning! rx CRC ERROR !!\n",
				  __func__, __LINE__);
			rtw_free_recvframe23a(precvframe);
			goto _exit_recvbuf2recvframe;
		}

		pkt_offset = RXDESC_SIZE + pattrib->drvinfo_sz +
			pattrib->shift_sz + pattrib->pkt_len;

		if (pattrib->pkt_len <= 0 || pkt_offset > transfer_len) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
				 ("recvbuf2recvframe: pkt_len<= 0\n"));
			DBG_8723A("%s()-%d: RX Warning!\n",
				  __func__, __LINE__);
			rtw_free_recvframe23a(precvframe);
			goto _exit_recvbuf2recvframe;
		}

		/*	Modified by Albert 20101213 */
		/*	For 8 bytes IP header alignment. */
		/*	Qos data, wireless lan header length is 26 */
		if (pattrib->qos)
			shift_sz = 6;
		else
			shift_sz = 0;

		skb_len = pattrib->pkt_len;

		/* for first fragment packet, driver need allocate
		 * 1536+drvinfo_sz+RXDESC_SIZE to defrag packet.
		 * modify alloc_sz for recvive crc error packet
		 * by thomas 2011-06-02 */
		if (pattrib->mfrag == 1 && pattrib->frag_num == 0) {
			/* alloc_sz = 1664;	1664 is 128 alignment. */
			if (skb_len <= 1650)
				alloc_sz = 1664;
			else
				alloc_sz = skb_len + 14;
		} else {
			alloc_sz = skb_len;
		/*  6 is for IP header 8 bytes alignment in QoS packet case. */
		/*  8 is for skb->data 4 bytes alignment. */
			alloc_sz += 14;
		}

		pkt_copy = netdev_alloc_skb(padapter->pnetdev, alloc_sz);
		if (pkt_copy) {
			pkt_copy->dev = padapter->pnetdev;
			precvframe->pkt = pkt_copy;
			/* force pkt_copy->data at 8-byte alignment address */
			skb_reserve(pkt_copy, 8 -
				    ((unsigned long)(pkt_copy->data) & 7));
			/*force ip_hdr at 8-byte alignment address
			  according to shift_sz. */
			skb_reserve(pkt_copy, shift_sz);
			memcpy(pkt_copy->data, pbuf + pattrib->shift_sz +
			       pattrib->drvinfo_sz + RXDESC_SIZE, skb_len);
			skb_put(pkt_copy, skb_len);
		} else {
			if (pattrib->mfrag == 1 && pattrib->frag_num == 0) {
				DBG_8723A("recvbuf2recvframe: alloc_skb fail, "
					  "drop frag frame \n");
				rtw_free_recvframe23a(precvframe);
				goto _exit_recvbuf2recvframe;
			}

			precvframe->pkt = skb_clone(pskb, GFP_ATOMIC);
			if (!precvframe->pkt) {
				DBG_8723A("recvbuf2recvframe: skb_clone "
					  "fail\n");
				rtw_free_recvframe23a(precvframe);
				goto _exit_recvbuf2recvframe;
			}
		}

		if (pattrib->physt) {
			pphy_info = (struct phy_stat *)(pbuf + RXDESC_OFFSET);
			update_recvframe_phyinfo(precvframe, pphy_info);
		}

		if (rtw_recv_entry23a(precvframe) != _SUCCESS)
			RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				 ("recvbuf2recvframe: rtw_recv_entry23a"
				  "(precvframe) != _SUCCESS\n"));

		pkt_cnt--;
		transfer_len -= pkt_offset;
		pbuf += pkt_offset;
		precvframe = NULL;
		pkt_copy = NULL;

		if (transfer_len > 0 && pkt_cnt == 0)
			pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16) & 0xff;

	} while (transfer_len > 0 && pkt_cnt > 0);

_exit_recvbuf2recvframe:

	return _SUCCESS;
}

void rtl8723au_recv_tasklet(void *priv)
{
	struct sk_buff *pskb;
	struct rtw_adapter *padapter = (struct rtw_adapter *)priv;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue))) {
		if (padapter->bDriverStopped || padapter->bSurpriseRemoved) {
			DBG_8723A("recv_tasklet => bDriverStopped or "
				  "bSurpriseRemoved \n");
			dev_kfree_skb_any(pskb);
			break;
		}

		recvbuf2recvframe(padapter, pskb);
		skb_reset_tail_pointer(pskb);

		pskb->len = 0;

		skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
	}
}

static void usb_read_port_complete(struct urb *purb)
{
	struct recv_buf *precvbuf = (struct recv_buf *)purb->context;
	struct rtw_adapter *padapter = (struct rtw_adapter *)precvbuf->adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
		 ("usb_read_port_complete!!!\n"));

	precvpriv->rx_pending_cnt--;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped ||
	    padapter->bReadPortCancel) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
			 ("usb_read_port_complete:bDriverStopped(%d) OR "
			  "bSurpriseRemoved(%d)\n", padapter->bDriverStopped,
			  padapter->bSurpriseRemoved));

		DBG_8723A("%s()-%d: RX Warning! bDriverStopped(%d) OR "
			  "bSurpriseRemoved(%d) bReadPortCancel(%d)\n",
			  __func__, __LINE__, padapter->bDriverStopped,
			  padapter->bSurpriseRemoved, padapter->bReadPortCancel);
		return;
	}

	if (purb->status == 0) {
		if (purb->actual_length > MAX_RECVBUF_SZ ||
		    purb->actual_length < RXDESC_SIZE) {
			RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
				 ("usb_read_port_complete: (purb->actual_"
				  "length > MAX_RECVBUF_SZ) || (purb->actual_"
				  "length < RXDESC_SIZE)\n"));
			rtl8723au_read_port(padapter, RECV_BULK_IN_ADDR, 0,
					    precvbuf);
			DBG_8723A("%s()-%d: RX Warning!\n",
				  __func__, __LINE__);
		} else {
			rtw_reset_continual_urb_error(
				adapter_to_dvobj(padapter));

			skb_put(precvbuf->pskb, purb->actual_length);
			skb_queue_tail(&precvpriv->rx_skb_queue,
				       precvbuf->pskb);

			if (skb_queue_len(&precvpriv->rx_skb_queue) <= 1)
				tasklet_schedule(&precvpriv->recv_tasklet);

			precvbuf->pskb = NULL;
			rtl8723au_read_port(padapter, RECV_BULK_IN_ADDR, 0,
					    precvbuf);
		}
	} else {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
			 ("usb_read_port_complete : purb->status(%d) != 0 \n",
			  purb->status));
		skb_put(precvbuf->pskb, purb->actual_length);
		precvbuf->pskb = NULL;

		DBG_8723A("###=> usb_read_port_complete => urb status(%d)\n",
			  purb->status);

		if (rtw_inc_and_chk_continual_urb_error(
			    adapter_to_dvobj(padapter))) {
			padapter->bSurpriseRemoved = true;
		}

		switch (purb->status) {
		case -EINVAL:
		case -EPIPE:
		case -ENODEV:
		case -ESHUTDOWN:
			RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
				 ("usb_read_port_complete:bSurprise"
				  "Removed = true\n"));
			/* Intentional fall through here */
		case -ENOENT:
			padapter->bDriverStopped = true;
			RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
				 ("usb_read_port_complete:"
				  "bDriverStopped = true\n"));
			break;
		case -EPROTO:
		case -EOVERFLOW:
			rtl8723au_read_port(padapter, RECV_BULK_IN_ADDR, 0,
					    precvbuf);
			break;
		case -EINPROGRESS:
			DBG_8723A("ERROR: URB IS IN PROGRESS!\n");
			break;
		default:
			break;
		}
	}
}

int rtl8723au_read_port(struct rtw_adapter *adapter, u32 addr, u32 cnt,
			struct recv_buf *precvbuf)
{
	int err;
	unsigned int pipe;
	unsigned long tmpaddr;
	unsigned long alignment;
	int ret = _SUCCESS;
	struct urb *purb;
	struct dvobj_priv *pdvobj = adapter_to_dvobj(adapter);
	struct recv_priv *precvpriv = &adapter->recvpriv;
	struct usb_device *pusbd = pdvobj->pusbdev;

	if (adapter->bDriverStopped || adapter->bSurpriseRemoved) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
			 ("usb_read_port:(padapter->bDriverStopped ||"
			  "padapter->bSurpriseRemoved)!!!\n"));
		return _FAIL;
	}

	if (!precvbuf) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
			 ("usb_read_port:precvbuf == NULL\n"));
		return _FAIL;
	}

	if (!precvbuf->pskb)
		precvbuf->pskb = skb_dequeue(&precvpriv->free_recv_skb_queue);

	/* re-assign for linux based on skb */
	if (!precvbuf->pskb) {
		precvbuf->pskb = netdev_alloc_skb(adapter->pnetdev, MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
		if (precvbuf->pskb == NULL) {
			RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("init_recvbuf(): alloc_skb fail!\n"));
			return _FAIL;
		}

		tmpaddr = (unsigned long)precvbuf->pskb->data;
		alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
		skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));
	}

	precvpriv->rx_pending_cnt++;

	purb = precvbuf->purb;

	/* translate DMA FIFO addr to pipehandle */
	pipe = usb_rcvbulkpipe(pusbd, pdvobj->RtInPipe[0]);

	usb_fill_bulk_urb(purb, pusbd, pipe, precvbuf->pskb->data,
			  MAX_RECVBUF_SZ, usb_read_port_complete,
			  precvbuf);/* context is precvbuf */

	err = usb_submit_urb(purb, GFP_ATOMIC);
	if ((err) && (err != -EPERM)) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
			 ("cannot submit rx in-token(err = 0x%.8x), URB_STATUS "
			  "= 0x%.8x", err, purb->status));
		DBG_8723A("cannot submit rx in-token(err = 0x%08x), urb_status "
			  "= %d\n", err, purb->status);
		ret = _FAIL;
	}
	return ret;
}

void rtl8723au_xmit_tasklet(void *priv)
{
	int ret;
	struct rtw_adapter *padapter = (struct rtw_adapter *)priv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if (check_fwstate(&padapter->mlmepriv, _FW_UNDER_SURVEY))
		return;

	while (1) {
		if (padapter->bDriverStopped || padapter->bSurpriseRemoved ||
		    padapter->bWritePortCancel) {
			DBG_8723A("xmit_tasklet => bDriverStopped or "
				  "bSurpriseRemoved or bWritePortCancel\n");
			break;
		}

		ret = rtl8723au_xmitframe_complete(padapter, pxmitpriv, NULL);

		if (!ret)
			break;
	}
}

void rtl8723au_set_hw_type(struct rtw_adapter *padapter)
{
	padapter->chip_type = RTL8723A;
	padapter->HardwareType = HARDWARE_TYPE_RTL8723AU;
	DBG_8723A("CHIP TYPE: RTL8723A\n");
}
