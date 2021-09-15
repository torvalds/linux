// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/osdep_intf.h"
#include "../include/usb_ops.h"
#include "../include/recv_osdep.h"
#include "../include/rtl8188e_hal.h"

static int usbctrl_vendorreq(struct intf_hdl *pintfhdl, u16 value, void *pdata, u16 len, u8 requesttype)
{
	struct adapter	*adapt = pintfhdl->padapter;
	struct dvobj_priv  *dvobjpriv = adapter_to_dvobj(adapt);
	struct usb_device *udev = dvobjpriv->pusbdev;
	unsigned int pipe;
	int status = 0;
	u8 *pIo_buf;
	int vendorreq_times = 0;

	if ((adapt->bSurpriseRemoved) || (adapt->pwrctrlpriv.pnp_bstop_trx)) {
		status = -EPERM;
		goto exit;
	}

	if (len > MAX_VENDOR_REQ_CMD_SIZE) {
		DBG_88E("[%s] Buffer len error ,vendor request failed\n", __func__);
		status = -EINVAL;
		goto exit;
	}

	_enter_critical_mutex(&dvobjpriv->usb_vendor_req_mutex, NULL);

	/*  Acquire IO memory for vendorreq */
	pIo_buf = dvobjpriv->usb_vendor_req_buf;

	if (!pIo_buf) {
		DBG_88E("[%s] pIo_buf == NULL\n", __func__);
		status = -ENOMEM;
		goto release_mutex;
	}

	if (requesttype == REALTEK_USB_VENQT_READ)
		pipe = usb_rcvctrlpipe(udev, 0);/* read_in */
	else
		pipe = usb_sndctrlpipe(udev, 0);/* write_out */

	while (++vendorreq_times <= MAX_USBCTRL_VENDORREQ_TIMES) {
		if (requesttype == REALTEK_USB_VENQT_READ)
			memset(pIo_buf, 0, len);
		else
			memcpy(pIo_buf, pdata, len);

		status = usb_control_msg(udev, pipe, REALTEK_USB_VENQT_CMD_REQ,
					 requesttype, value, REALTEK_USB_VENQT_CMD_IDX,
					 pIo_buf, len, RTW_USB_CONTROL_MSG_TIMEOUT);

		if (status == len) {   /*  Success this control transfer. */
			rtw_reset_continual_urb_error(dvobjpriv);
			if (requesttype == REALTEK_USB_VENQT_READ)
				memcpy(pdata, pIo_buf,  len);
		} else { /*  error cases */
			DBG_88E("reg 0x%x, usb %s %u fail, status:%d value=0x%x, vendorreq_times:%d\n",
				value, (requesttype == REALTEK_USB_VENQT_READ) ? "read" : "write",
				len, status, *(u32 *)pdata, vendorreq_times);

			if (status < 0) {
				if (status == (-ESHUTDOWN) || status == -ENODEV) {
					adapt->bSurpriseRemoved = true;
				} else {
					struct hal_data_8188e	*haldata = GET_HAL_DATA(adapt);
					haldata->srestpriv.wifi_error_status = USB_VEN_REQ_CMD_FAIL;
				}
			} else { /*  status != len && status >= 0 */
				if (status > 0) {
					if (requesttype == REALTEK_USB_VENQT_READ) {
						/*  For Control read transfer, we have to copy the read data from pIo_buf to pdata. */
						memcpy(pdata, pIo_buf,  len);
					}
				}
			}

			if (rtw_inc_and_chk_continual_urb_error(dvobjpriv)) {
				adapt->bSurpriseRemoved = true;
				break;
			}

		}

		/*  firmware download is checksumed, don't retry */
		if ((value >= FW_8188E_START_ADDRESS && value <= FW_8188E_END_ADDRESS) || status == len)
			break;
	}
release_mutex:
	_exit_critical_mutex(&dvobjpriv->usb_vendor_req_mutex, NULL);
exit:
	return status;
}

static u8 usb_read8(struct intf_hdl *pintfhdl, u32 addr)
{
	u16 wvalue = (u16)(addr & 0x0000ffff);
	u8 data;

	usbctrl_vendorreq(pintfhdl, wvalue, &data, 1, REALTEK_USB_VENQT_READ);

	return data;
}

static u16 usb_read16(struct intf_hdl *pintfhdl, u32 addr)
{
	u16 wvalue = (u16)(addr & 0x0000ffff);
	__le32 data;

	usbctrl_vendorreq(pintfhdl, wvalue, &data, 2, REALTEK_USB_VENQT_READ);

	return (u16)(le32_to_cpu(data) & 0xffff);
}

static u32 usb_read32(struct intf_hdl *pintfhdl, u32 addr)
{
	u16 wvalue = (u16)(addr & 0x0000ffff);
	__le32 data;

	usbctrl_vendorreq(pintfhdl, wvalue, &data, 4, REALTEK_USB_VENQT_READ);

	return le32_to_cpu(data);
}

static int usb_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{
	u16 wvalue = (u16)(addr & 0x0000ffff);

	return usbctrl_vendorreq(pintfhdl, wvalue, &val, 1, REALTEK_USB_VENQT_WRITE);
}

static int usb_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{
	u16 wvalue = (u16)(addr & 0x0000ffff);
	__le32 data = cpu_to_le32(val & 0x0000ffff);

	return usbctrl_vendorreq(pintfhdl, wvalue, &data, 2, REALTEK_USB_VENQT_WRITE);
}

static int usb_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	u16 wvalue = (u16)(addr & 0x0000ffff);
	__le32 data = cpu_to_le32(val);

	return usbctrl_vendorreq(pintfhdl, wvalue, &data, 4, REALTEK_USB_VENQT_WRITE);
}

static int usb_writeN(struct intf_hdl *pintfhdl, u32 addr, u32 length, u8 *pdata)
{
	u16 wvalue = (u16)(addr & 0x0000ffff);
	u8 buf[VENDOR_CMD_MAX_DATA_LEN] = {0};

	if (length > VENDOR_CMD_MAX_DATA_LEN)
		return -EINVAL;

	memcpy(buf, pdata, length);

	return usbctrl_vendorreq(pintfhdl, wvalue, buf, (length & 0xffff), REALTEK_USB_VENQT_WRITE);
}

static void interrupt_handler_8188eu(struct adapter *adapt, u16 pkt_len, u8 *pbuf)
{
	struct hal_data_8188e	*haldata = GET_HAL_DATA(adapt);

	if (pkt_len != INTERRUPT_MSG_FORMAT_LEN) {
		DBG_88E("%s Invalid interrupt content length (%d)!\n", __func__, pkt_len);
		return;
	}

	/*  HISR */
	memcpy(&haldata->IntArray[0], &pbuf[USB_INTR_CONTENT_HISR_OFFSET], 4);
	memcpy(&haldata->IntArray[1], &pbuf[USB_INTR_CONTENT_HISRE_OFFSET], 4);

	/*  C2H Event */
	if (pbuf[0] != 0)
		memcpy(&haldata->C2hArray[0], &pbuf[USB_INTR_CONTENT_C2H_OFFSET], 16);
}

static int recvbuf2recvframe(struct adapter *adapt, struct sk_buff *pskb)
{
	u8	*pbuf;
	u8	shift_sz = 0;
	u16	pkt_cnt;
	u32	pkt_offset, skb_len, alloc_sz;
	s32	transfer_len;
	struct recv_stat	*prxstat;
	struct phy_stat	*pphy_status = NULL;
	struct sk_buff *pkt_copy = NULL;
	struct recv_frame	*precvframe = NULL;
	struct rx_pkt_attrib	*pattrib = NULL;
	struct hal_data_8188e	*haldata = GET_HAL_DATA(adapt);
	struct recv_priv	*precvpriv = &adapt->recvpriv;
	struct __queue *pfree_recv_queue = &precvpriv->free_recv_queue;

	transfer_len = (s32)pskb->len;
	pbuf = pskb->data;

	prxstat = (struct recv_stat *)pbuf;
	pkt_cnt = (le32_to_cpu(prxstat->rxdw2) >> 16) & 0xff;

	do {
		prxstat = (struct recv_stat *)pbuf;

		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if (!precvframe) {
			DBG_88E("%s()-%d: rtw_alloc_recvframe() failed! RX Drop!\n", __func__, __LINE__);
			goto _exit_recvbuf2recvframe;
		}

		INIT_LIST_HEAD(&precvframe->list);
		precvframe->precvbuf = NULL;	/* can't access the precvbuf for new arch. */
		precvframe->len = 0;

		update_recvframe_attrib_88e(precvframe, prxstat);

		pattrib = &precvframe->attrib;

		if ((pattrib->crc_err) || (pattrib->icv_err)) {
			DBG_88E("%s: RX Warning! crc_err=%d icv_err=%d, skip!\n", __func__, pattrib->crc_err, pattrib->icv_err);

			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		if ((pattrib->physt) && (pattrib->pkt_rpt_type == NORMAL_RX))
			pphy_status = (struct phy_stat *)(pbuf + RXDESC_OFFSET);

		pkt_offset = RXDESC_SIZE + pattrib->drvinfo_sz + pattrib->shift_sz + pattrib->pkt_len;

		if ((pattrib->pkt_len <= 0) || (pkt_offset > transfer_len)) {
			DBG_88E("%s()-%d: RX Warning!,pkt_len<=0 or pkt_offset> transfoer_len\n", __func__, __LINE__);
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		/*	Modified by Albert 20101213 */
		/*	For 8 bytes IP header alignment. */
		if (pattrib->qos)	/*	Qos data, wireless lan header length is 26 */
			shift_sz = 6;
		else
			shift_sz = 0;

		skb_len = pattrib->pkt_len;

		/*  for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet. */
		/*  modify alloc_sz for recvive crc error packet by thomas 2011-06-02 */
		if ((pattrib->mfrag == 1) && (pattrib->frag_num == 0)) {
			if (skb_len <= 1650)
				alloc_sz = 1664;
			else
				alloc_sz = skb_len + 14;
		} else {
			alloc_sz = skb_len;
			/*	6 is for IP header 8 bytes alignment in QoS packet case. */
			/*	8 is for skb->data 4 bytes alignment. */
			alloc_sz += 14;
		}

		pkt_copy = netdev_alloc_skb(adapt->pnetdev, alloc_sz);
		if (pkt_copy) {
			pkt_copy->dev = adapt->pnetdev;
			precvframe->pkt = pkt_copy;
			precvframe->rx_head = pkt_copy->data;
			precvframe->rx_end = pkt_copy->data + alloc_sz;
			skb_reserve(pkt_copy, 8 - ((size_t)(pkt_copy->data) & 7));/* force pkt_copy->data at 8-byte alignment address */
			skb_reserve(pkt_copy, shift_sz);/* force ip_hdr at 8-byte alignment address according to shift_sz. */
			memcpy(pkt_copy->data, (pbuf + pattrib->drvinfo_sz + RXDESC_SIZE), skb_len);
			precvframe->rx_tail = pkt_copy->data;
			precvframe->rx_data = pkt_copy->data;
		} else {
			if ((pattrib->mfrag == 1) && (pattrib->frag_num == 0)) {
				DBG_88E("recvbuf2recvframe: alloc_skb fail , drop frag frame\n");
				rtw_free_recvframe(precvframe, pfree_recv_queue);
				goto _exit_recvbuf2recvframe;
			}
			precvframe->pkt = skb_clone(pskb, GFP_ATOMIC);
			if (precvframe->pkt) {
				precvframe->rx_tail = pbuf + pattrib->drvinfo_sz + RXDESC_SIZE;
				precvframe->rx_head = precvframe->rx_tail;
				precvframe->rx_data = precvframe->rx_tail;
				precvframe->rx_end =  pbuf + pattrib->drvinfo_sz + RXDESC_SIZE + alloc_sz;
			} else {
				DBG_88E("recvbuf2recvframe: skb_clone fail\n");
				rtw_free_recvframe(precvframe, pfree_recv_queue);
				goto _exit_recvbuf2recvframe;
			}
		}

		recvframe_put(precvframe, skb_len);

		switch (haldata->UsbRxAggMode) {
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
		if (pattrib->pkt_rpt_type == NORMAL_RX) { /* Normal rx packet */
			if (pattrib->physt)
				update_recvframe_phyinfo_88e(precvframe, (struct phy_stat *)pphy_status);
			rtw_recv_entry(precvframe);
		} else {
			/* enqueue recvframe to txrtp queue */
			if (pattrib->pkt_rpt_type == TX_REPORT1) {
				/* CCX-TXRPT ack for xmit mgmt frames. */
				handle_txrpt_ccx_88e(adapt, precvframe->rx_data);
			} else if (pattrib->pkt_rpt_type == TX_REPORT2) {
				ODM_RA_TxRPT2Handle_8188E(
							&haldata->odmpriv,
							precvframe->rx_data,
							pattrib->pkt_len,
							pattrib->MacIDValidEntry[0],
							pattrib->MacIDValidEntry[1]
							);
			} else if (pattrib->pkt_rpt_type == HIS_REPORT) {
				interrupt_handler_8188eu(adapt, pattrib->pkt_len, precvframe->rx_data);
			}
			rtw_free_recvframe(precvframe, pfree_recv_queue);
		}
		pkt_cnt--;
		transfer_len -= pkt_offset;
		pbuf += pkt_offset;
		precvframe = NULL;
		pkt_copy = NULL;

		if (transfer_len > 0 && pkt_cnt == 0)
			pkt_cnt = (le32_to_cpu(prxstat->rxdw2) >> 16) & 0xff;

	} while ((transfer_len > 0) && (pkt_cnt > 0));

_exit_recvbuf2recvframe:

	return _SUCCESS;
}

void rtl8188eu_recv_tasklet(unsigned long priv)
{
	struct sk_buff *pskb;
	struct adapter *adapt = (struct adapter *)priv;
	struct recv_priv *precvpriv = &adapt->recvpriv;

	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue))) {
		if ((adapt->bDriverStopped) || (adapt->bSurpriseRemoved)) {
			DBG_88E("recv_tasklet => bDriverStopped or bSurpriseRemoved\n");
			dev_kfree_skb_any(pskb);
			break;
		}
		recvbuf2recvframe(adapt, pskb);
		skb_reset_tail_pointer(pskb);
		pskb->len = 0;
		skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
	}
}

static void usb_read_port_complete(struct urb *purb, struct pt_regs *regs)
{
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;
	struct adapter	*adapt = (struct adapter *)precvbuf->adapter;
	struct recv_priv *precvpriv = &adapt->recvpriv;

	precvpriv->rx_pending_cnt--;

	if (adapt->bSurpriseRemoved || adapt->bDriverStopped || adapt->bReadPortCancel) {
		precvbuf->reuse = true;
		DBG_88E("%s() RX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) bReadPortCancel(%d)\n",
			__func__, adapt->bDriverStopped,
			adapt->bSurpriseRemoved, adapt->bReadPortCancel);
		return;
	}

	if (purb->status == 0) { /* SUCCESS */
		if ((purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE)) {
			precvbuf->reuse = true;
			rtw_read_port(adapt, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
			DBG_88E("%s()-%d: RX Warning!\n", __func__, __LINE__);
		} else {
			rtw_reset_continual_urb_error(adapter_to_dvobj(adapt));

			precvbuf->transfer_len = purb->actual_length;
			skb_put(precvbuf->pskb, purb->actual_length);
			skb_queue_tail(&precvpriv->rx_skb_queue, precvbuf->pskb);

			if (skb_queue_len(&precvpriv->rx_skb_queue) <= 1)
				tasklet_schedule(&precvpriv->recv_tasklet);

			precvbuf->pskb = NULL;
			precvbuf->reuse = false;
			rtw_read_port(adapt, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		}
	} else {
		DBG_88E("###=> usb_read_port_complete => urb status(%d)\n", purb->status);
		skb_put(precvbuf->pskb, purb->actual_length);
		precvbuf->pskb = NULL;

		if (rtw_inc_and_chk_continual_urb_error(adapter_to_dvobj(adapt)))
			adapt->bSurpriseRemoved = true;

		switch (purb->status) {
		case -EINVAL:
		case -EPIPE:
		case -ENODEV:
		case -ESHUTDOWN:
		case -ENOENT:
			adapt->bDriverStopped = true;
			break;
		case -EPROTO:
		case -EOVERFLOW:
			{
				struct hal_data_8188e	*haldata = GET_HAL_DATA(adapt);
				haldata->srestpriv.wifi_error_status = USB_READ_PORT_FAIL;
			}
			precvbuf->reuse = true;
			rtw_read_port(adapt, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
			break;
		case -EINPROGRESS:
			DBG_88E("ERROR: URB IS IN PROGRESS!/n");
			break;
		default:
			break;
		}
	}
}

static u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	struct urb *purb = NULL;
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	struct adapter		*adapter = pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(adapter);
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;
	int err;
	unsigned int pipe;
	size_t tmpaddr = 0;
	size_t alignment = 0;
	u32 ret = _SUCCESS;

	if (adapter->bDriverStopped || adapter->bSurpriseRemoved ||
	    adapter->pwrctrlpriv.pnp_bstop_trx)
		return _FAIL;

	if (!precvbuf)
		return _FAIL;

	if (!precvbuf->reuse || !precvbuf->pskb) {
		precvbuf->pskb = skb_dequeue(&precvpriv->free_recv_skb_queue);
		if (precvbuf->pskb)
			precvbuf->reuse = true;
	}

	rtl8188eu_init_recvbuf(adapter, precvbuf);

	/* re-assign for linux based on skb */
	if (!precvbuf->reuse || !precvbuf->pskb) {
		precvbuf->pskb = netdev_alloc_skb(adapter->pnetdev, MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
		if (!precvbuf->pskb) {
			DBG_88E("#### usb_read_port() alloc_skb fail!#####\n");
			return _FAIL;
		}

		tmpaddr = (size_t)precvbuf->pskb->data;
		alignment = tmpaddr & (RECVBUFF_ALIGN_SZ - 1);
		skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));

		precvbuf->phead = precvbuf->pskb->head;
		precvbuf->pdata = precvbuf->pskb->data;
		precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
		precvbuf->pend = skb_end_pointer(precvbuf->pskb);
		precvbuf->pbuf = precvbuf->pskb->data;
	} else { /* reuse skb */
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
		DBG_88E("cannot submit rx in-token(err = 0x%08x),urb_status = %d\n",
			err, purb->status);
		ret = _FAIL;
	}

	return ret;
}

void rtl8188eu_xmit_tasklet(unsigned long priv)
{
	int ret = false;
	struct adapter *adapt = (struct adapter *)priv;
	struct xmit_priv *pxmitpriv = &adapt->xmitpriv;

	if (check_fwstate(&adapt->mlmepriv, _FW_UNDER_SURVEY))
		return;

	while (1) {
		if ((adapt->bDriverStopped) ||
		    (adapt->bSurpriseRemoved) ||
		    (adapt->bWritePortCancel)) {
			DBG_88E("xmit_tasklet => bDriverStopped or bSurpriseRemoved or bWritePortCancel\n");
			break;
		}

		ret = rtl8188eu_xmitframe_complete(adapt, pxmitpriv, NULL);

		if (!ret)
			break;
	}
}

void rtl8188eu_set_intf_ops(struct _io_ops	*pops)
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

void rtl8188eu_set_hw_type(struct adapter *adapt)
{
	adapt->chip_type = RTL8188E;
	adapt->HardwareType = HARDWARE_TYPE_RTL8188EU;
	DBG_88E("CHIP TYPE: RTL8188E\n");
}
