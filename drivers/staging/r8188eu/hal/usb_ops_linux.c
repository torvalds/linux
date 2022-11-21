// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/osdep_intf.h"
#include "../include/usb_ops.h"
#include "../include/recv_osdep.h"
#include "../include/rtl8188e_hal.h"

static int usb_read(struct intf_hdl *intf, u16 value, void *data, u8 size)
{
	struct adapter *adapt = intf->padapter;
	struct dvobj_priv *dvobjpriv = adapter_to_dvobj(adapt);
	struct usb_device *udev = dvobjpriv->pusbdev;
	int status;
	u8 io_buf[4];

	if (adapt->bSurpriseRemoved || adapt->pwrctrlpriv.pnp_bstop_trx)
		return -EPERM;

	status = usb_control_msg_recv(udev, 0, REALTEK_USB_VENQT_CMD_REQ,
				      REALTEK_USB_VENQT_READ, value,
				      REALTEK_USB_VENQT_CMD_IDX, io_buf,
				      size, RTW_USB_CONTROL_MSG_TIMEOUT,
				      GFP_KERNEL);

	if (status == -ESHUTDOWN ||
	    status == -ENODEV ||
	    status == -ENOENT) {
		/*
		 * device or controller has been disabled due to
		 * some problem that could not be worked around,
		 * device or bus doesn’t exist, endpoint does not
		 * exist or is not enabled.
		 */
		adapt->bSurpriseRemoved = true;
		return status;
	}

	if (status < 0) {
		if (rtw_inc_and_chk_continual_urb_error(dvobjpriv))
			adapt->bSurpriseRemoved = true;

		return status;
	}

	rtw_reset_continual_urb_error(dvobjpriv);
	memcpy(data, io_buf, size);

	return status;
}

static int usb_write(struct intf_hdl *intf, u16 value, void *data, u8 size)
{
	struct adapter *adapt = intf->padapter;
	struct dvobj_priv *dvobjpriv = adapter_to_dvobj(adapt);
	struct usb_device *udev = dvobjpriv->pusbdev;
	int status;
	u8 io_buf[VENDOR_CMD_MAX_DATA_LEN];

	if (adapt->bSurpriseRemoved || adapt->pwrctrlpriv.pnp_bstop_trx)
		return -EPERM;

	memcpy(io_buf, data, size);
	status = usb_control_msg_send(udev, 0, REALTEK_USB_VENQT_CMD_REQ,
				      REALTEK_USB_VENQT_WRITE, value,
				      REALTEK_USB_VENQT_CMD_IDX, io_buf,
				      size, RTW_USB_CONTROL_MSG_TIMEOUT,
				      GFP_KERNEL);

	if (status == -ESHUTDOWN ||
	    status == -ENODEV ||
	    status == -ENOENT) {
		/*
		 * device or controller has been disabled due to
		 * some problem that could not be worked around,
		 * device or bus doesn’t exist, endpoint does not
		 * exist or is not enabled.
		 */
		adapt->bSurpriseRemoved = true;
		return status;
	}

	if (status < 0) {
		if (rtw_inc_and_chk_continual_urb_error(dvobjpriv))
			adapt->bSurpriseRemoved = true;

		return status;
	}

	rtw_reset_continual_urb_error(dvobjpriv);

	return status;
}

u8 rtw_read8(struct adapter *adapter, u32 addr)
{
	struct io_priv *io_priv = &adapter->iopriv;
	struct intf_hdl *intf = &io_priv->intf;
	u16 value = addr & 0xffff;
	u8 data;

	usb_read(intf, value, &data, 1);

	return data;
}

u16 rtw_read16(struct adapter *adapter, u32 addr)
{
	struct io_priv *io_priv = &adapter->iopriv;
	struct intf_hdl *intf = &io_priv->intf;
	u16 value = addr & 0xffff;
	__le16 data;

	usb_read(intf, value, &data, 2);

	return le16_to_cpu(data);
}

u32 rtw_read32(struct adapter *adapter, u32 addr)
{
	struct io_priv *io_priv = &adapter->iopriv;
	struct intf_hdl *intf = &io_priv->intf;
	u16 value = addr & 0xffff;
	__le32 data;

	usb_read(intf, value, &data, 4);

	return le32_to_cpu(data);
}

int rtw_write8(struct adapter *adapter, u32 addr, u8 val)
{
	struct io_priv *io_priv = &adapter->iopriv;
	struct intf_hdl *intf = &io_priv->intf;
	u16 value = addr & 0xffff;
	int ret;

	ret = usb_write(intf, value, &val, 1);

	return RTW_STATUS_CODE(ret);
}

int rtw_write16(struct adapter *adapter, u32 addr, u16 val)
{
	struct io_priv *io_priv = &adapter->iopriv;
	struct intf_hdl *intf = &io_priv->intf;
	u16 value = addr & 0xffff;
	__le16 data = cpu_to_le16(val);
	int ret;

	ret = usb_write(intf, value, &data, 2);

	return RTW_STATUS_CODE(ret);
}

int rtw_write32(struct adapter *adapter, u32 addr, u32 val)
{
	struct io_priv *io_priv = &adapter->iopriv;
	struct intf_hdl *intf = &io_priv->intf;
	u16 value = addr & 0xffff;
	__le32 data = cpu_to_le32(val);
	int ret;

	ret = usb_write(intf, value, &data, 4);

	return RTW_STATUS_CODE(ret);
}

int rtw_writeN(struct adapter *adapter, u32 addr, u32 length, u8 *data)
{
	struct io_priv *io_priv = &adapter->iopriv;
	struct intf_hdl *intf = &io_priv->intf;
	u16 value = addr & 0xffff;
	int ret;

	if (length > VENDOR_CMD_MAX_DATA_LEN)
		return _FAIL;

	ret = usb_write(intf, value, data, length);

	return RTW_STATUS_CODE(ret);
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
	struct hal_data_8188e *haldata = &adapt->haldata;
	struct recv_priv	*precvpriv = &adapt->recvpriv;
	struct __queue *pfree_recv_queue = &precvpriv->free_recv_queue;

	transfer_len = (s32)pskb->len;
	pbuf = pskb->data;

	prxstat = (struct recv_stat *)pbuf;
	pkt_cnt = (le32_to_cpu(prxstat->rxdw2) >> 16) & 0xff;

	do {
		prxstat = (struct recv_stat *)pbuf;

		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if (!precvframe)
			goto _exit_recvbuf2recvframe;

		INIT_LIST_HEAD(&precvframe->list);
		precvframe->precvbuf = NULL;	/* can't access the precvbuf for new arch. */
		precvframe->len = 0;

		update_recvframe_attrib_88e(precvframe, prxstat);

		pattrib = &precvframe->attrib;

		if ((pattrib->crc_err) || (pattrib->icv_err)) {
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		if ((pattrib->physt) && (pattrib->pkt_rpt_type == NORMAL_RX))
			pphy_status = (struct phy_stat *)(pbuf + RXDESC_OFFSET);

		pkt_offset = RXDESC_SIZE + pattrib->drvinfo_sz + pattrib->shift_sz + pattrib->pkt_len;

		if ((pattrib->pkt_len <= 0) || (pkt_offset > transfer_len)) {
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
				rtw_free_recvframe(precvframe, pfree_recv_queue);
				goto _exit_recvbuf2recvframe;
			}
		}

		recvframe_put(precvframe, skb_len);

		pkt_offset = (u16)_RND128(pkt_offset);

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
		return;
	}

	if (purb->status == 0) { /* SUCCESS */
		if ((purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE)) {
			precvbuf->reuse = true;
			rtw_read_port(adapt, (unsigned char *)precvbuf);
		} else {
			rtw_reset_continual_urb_error(adapter_to_dvobj(adapt));

			skb_put(precvbuf->pskb, purb->actual_length);
			skb_queue_tail(&precvpriv->rx_skb_queue, precvbuf->pskb);

			if (skb_queue_len(&precvpriv->rx_skb_queue) <= 1)
				tasklet_schedule(&precvpriv->recv_tasklet);

			precvbuf->pskb = NULL;
			precvbuf->reuse = false;
			rtw_read_port(adapt, (unsigned char *)precvbuf);
		}
	} else {
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
			precvbuf->reuse = true;
			rtw_read_port(adapt, (unsigned char *)precvbuf);
			break;
		case -EINPROGRESS:
			break;
		default:
			break;
		}
	}
}

u32 rtw_read_port(struct adapter *adapter, u8 *rmem)
{
	struct urb *purb = NULL;
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
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

	/* re-assign for linux based on skb */
	if (!precvbuf->reuse || !precvbuf->pskb) {
		precvbuf->pskb = netdev_alloc_skb(adapter->pnetdev, MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
		if (!precvbuf->pskb)
			return _FAIL;

		tmpaddr = (size_t)precvbuf->pskb->data;
		alignment = tmpaddr & (RECVBUFF_ALIGN_SZ - 1);
		skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));
	} else { /* reuse skb */
		precvbuf->reuse = false;
	}

	precvpriv->rx_pending_cnt++;

	purb = precvbuf->purb;

	/* translate DMA FIFO addr to pipehandle */
	pipe = usb_rcvbulkpipe(pusbd, pdvobj->RtInPipe);

	usb_fill_bulk_urb(purb, pusbd, pipe,
			  precvbuf->pskb->data,
			  MAX_RECVBUF_SZ,
			  usb_read_port_complete,
			  precvbuf);/* context is precvbuf */

	err = usb_submit_urb(purb, GFP_ATOMIC);
	if ((err) && (err != (-EPERM)))
		ret = _FAIL;

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
		    (adapt->bWritePortCancel))
			break;

		ret = rtl8188eu_xmitframe_complete(adapt, pxmitpriv, NULL);

		if (!ret)
			break;
	}
}
