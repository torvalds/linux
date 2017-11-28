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
#define _HAL_USB_C_

#include <drv_types.h>
#include <hal_data.h>

int	usb_init_recv_priv(_adapter *padapter, u16 ini_in_buf_sz)
{
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	int	i, res = _SUCCESS;
	struct recv_buf *precvbuf;

#ifdef PLATFORM_LINUX
	tasklet_init(&precvpriv->recv_tasklet,
		     (void(*)(unsigned long))usb_recv_tasklet,
		     (unsigned long)padapter);
#endif /* PLATFORM_LINUX */

#ifdef PLATFORM_FREEBSD
#ifdef CONFIG_RX_INDICATE_QUEUE
	TASK_INIT(&precvpriv->rx_indicate_tasklet, 0, rtw_rx_indicate_tasklet, padapter);
#endif /* CONFIG_RX_INDICATE_QUEUE */
#endif /* PLATFORM_FREEBSD */

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
#ifdef PLATFORM_LINUX
	precvpriv->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (precvpriv->int_in_urb == NULL) {
		res = _FAIL;
		RTW_INFO("alloc_urb for interrupt in endpoint fail !!!!\n");
		goto exit;
	}
#endif /* PLATFORM_LINUX */
	precvpriv->int_in_buf = rtw_zmalloc(ini_in_buf_sz);
	if (precvpriv->int_in_buf == NULL) {
		res = _FAIL;
		RTW_INFO("alloc_mem for interrupt in endpoint fail !!!!\n");
		goto exit;
	}
#endif /* CONFIG_USB_INTERRUPT_IN_PIPE */

	/* init recv_buf */
	_rtw_init_queue(&precvpriv->free_recv_buf_queue);
	_rtw_init_queue(&precvpriv->recv_buf_pending_queue);
#ifndef CONFIG_USE_USB_BUFFER_ALLOC_RX
	/* this is used only when RX_IOBUF is sk_buff */
	skb_queue_head_init(&precvpriv->free_recv_skb_queue);
#endif

	RTW_INFO("NR_RECVBUFF: %d\n", NR_RECVBUFF);
	RTW_INFO("MAX_RECVBUF_SZ: %d\n", MAX_RECVBUF_SZ);
	precvpriv->pallocated_recv_buf = rtw_zmalloc(NR_RECVBUFF * sizeof(struct recv_buf) + 4);
	if (precvpriv->pallocated_recv_buf == NULL) {
		res = _FAIL;
		goto exit;
	}

	precvpriv->precv_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_recv_buf), 4);

	precvbuf = (struct recv_buf *)precvpriv->precv_buf;

	for (i = 0; i < NR_RECVBUFF ; i++) {
		_rtw_init_listhead(&precvbuf->list);

		_rtw_spinlock_init(&precvbuf->recvbuf_lock);

		precvbuf->alloc_sz = MAX_RECVBUF_SZ;

		res = rtw_os_recvbuf_resource_alloc(padapter, precvbuf);
		if (res == _FAIL)
			break;

		precvbuf->ref_cnt = 0;
		precvbuf->adapter = padapter;

		/* rtw_list_insert_tail(&precvbuf->list, &(precvpriv->free_recv_buf_queue.queue)); */

		precvbuf++;
	}

	precvpriv->free_recv_buf_queue_cnt = NR_RECVBUFF;

#if defined(PLATFORM_LINUX) || defined(PLATFORM_FREEBSD)

	skb_queue_head_init(&precvpriv->rx_skb_queue);

#ifdef CONFIG_RX_INDICATE_QUEUE
	memset(&precvpriv->rx_indicate_queue, 0, sizeof(struct ifqueue));
	mtx_init(&precvpriv->rx_indicate_queue.ifq_mtx, "rx_indicate_queue", NULL, MTX_DEF);
#endif /* CONFIG_RX_INDICATE_QUEUE */

#ifdef CONFIG_PREALLOC_RECV_SKB
	{
		int i;
		SIZE_PTR tmpaddr = 0;
		SIZE_PTR alignment = 0;
		struct sk_buff *pskb = NULL;

		RTW_INFO("NR_PREALLOC_RECV_SKB: %d\n", NR_PREALLOC_RECV_SKB);
#ifdef CONFIG_FIX_NR_BULKIN_BUFFER
		RTW_INFO("Enable CONFIG_FIX_NR_BULKIN_BUFFER\n");
#endif

		for (i = 0; i < NR_PREALLOC_RECV_SKB; i++) {
#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
			pskb = rtw_alloc_skb_premem(MAX_RECVBUF_SZ);
#else
			pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
#endif /* CONFIG_PREALLOC_RX_SKB_BUFFER */

			if (pskb) {
#ifdef PLATFORM_FREEBSD
				pskb->dev = padapter->pifp;
#else
				pskb->dev = padapter->pnetdev;
#endif /* PLATFORM_FREEBSD */

#ifndef CONFIG_PREALLOC_RX_SKB_BUFFER
				tmpaddr = (SIZE_PTR)pskb->data;
				alignment = tmpaddr & (RECVBUFF_ALIGN_SZ - 1);
				skb_reserve(pskb, (RECVBUFF_ALIGN_SZ - alignment));
#endif
				skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
			}
		}
	}
#endif /* CONFIG_PREALLOC_RECV_SKB */

#endif /* defined(PLATFORM_LINUX) || defined(PLATFORM_FREEBSD) */

exit:

	return res;
}

void usb_free_recv_priv(_adapter *padapter, u16 ini_in_buf_sz)
{
	int i;
	struct recv_buf *precvbuf;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	precvbuf = (struct recv_buf *)precvpriv->precv_buf;

	for (i = 0; i < NR_RECVBUFF ; i++) {
		rtw_os_recvbuf_resource_free(padapter, precvbuf);
		precvbuf++;
	}

	if (precvpriv->pallocated_recv_buf)
		rtw_mfree(precvpriv->pallocated_recv_buf, NR_RECVBUFF * sizeof(struct recv_buf) + 4);

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
#ifdef PLATFORM_LINUX
	if (precvpriv->int_in_urb)
		usb_free_urb(precvpriv->int_in_urb);
#endif
	if (precvpriv->int_in_buf)
		rtw_mfree(precvpriv->int_in_buf, ini_in_buf_sz);
#endif /* CONFIG_USB_INTERRUPT_IN_PIPE */

#ifdef PLATFORM_LINUX

	if (skb_queue_len(&precvpriv->rx_skb_queue))
		RTW_WARN("rx_skb_queue not empty\n");

	rtw_skb_queue_purge(&precvpriv->rx_skb_queue);

	if (skb_queue_len(&precvpriv->free_recv_skb_queue))
		RTW_WARN("free_recv_skb_queue not empty, %d\n", skb_queue_len(&precvpriv->free_recv_skb_queue));

#if !defined(CONFIG_USE_USB_BUFFER_ALLOC_RX)
#if defined(CONFIG_PREALLOC_RECV_SKB) && defined(CONFIG_PREALLOC_RX_SKB_BUFFER)
	{
		struct sk_buff *skb;

		while ((skb = skb_dequeue(&precvpriv->free_recv_skb_queue)) != NULL) {
			if (rtw_free_skb_premem(skb) != 0)
				rtw_skb_free(skb);
		}
	}
#else
	rtw_skb_queue_purge(&precvpriv->free_recv_skb_queue);
#endif /* defined(CONFIG_PREALLOC_RX_SKB_BUFFER) && defined(CONFIG_PREALLOC_RECV_SKB) */
#endif /* !defined(CONFIG_USE_USB_BUFFER_ALLOC_RX) */

#endif /* PLATFORM_LINUX */

#ifdef PLATFORM_FREEBSD
	struct sk_buff  *pskb;
	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue)))
		rtw_skb_free(pskb);

#if !defined(CONFIG_USE_USB_BUFFER_ALLOC_RX)
	rtw_skb_queue_purge(&precvpriv->free_recv_skb_queue);
#endif

#ifdef CONFIG_RX_INDICATE_QUEUE
	struct mbuf *m;
	for (;;) {
		IF_DEQUEUE(&precvpriv->rx_indicate_queue, m);
		if (m == NULL)
			break;
		m_freem(m);
	}
	mtx_destroy(&precvpriv->rx_indicate_queue.ifq_mtx);
#endif /* CONFIG_RX_INDICATE_QUEUE */

#endif /* PLATFORM_FREEBSD */
}

#ifdef CONFIG_FW_C2H_REG
void usb_c2h_hisr_hdl(_adapter *adapter, u8 *buf)
{
	u8 *c2h_evt = buf;
	u8 id, seq, plen;
	u8 *payload;

	if (rtw_hal_c2h_reg_hdr_parse(adapter, buf, &id, &seq, &plen, &payload) != _SUCCESS)
		return;

	if (0)
		RTW_PRINT("%s C2H == %d\n", __func__, id);

	if (rtw_hal_c2h_id_handle_directly(adapter, id, seq, plen, payload)) {
		/* Handle directly */
		rtw_hal_c2h_handler(adapter, id, seq, plen, payload);

		/* Replace with special pointer to trigger c2h_evt_clear only */
		if (rtw_cbuf_push(adapter->evtpriv.c2h_queue, (void*)&adapter->evtpriv) != _SUCCESS)
			RTW_ERR("%s rtw_cbuf_push fail\n", __func__);
	} else {
		c2h_evt = rtw_malloc(C2H_REG_LEN);
		if (c2h_evt != NULL) {
			_rtw_memcpy(c2h_evt, buf, C2H_REG_LEN);
			if (rtw_cbuf_push(adapter->evtpriv.c2h_queue, (void*)c2h_evt) != _SUCCESS)
				RTW_ERR("%s rtw_cbuf_push fail\n", __func__);
		} else {
			/* Error handling for malloc fail */
			if (rtw_cbuf_push(adapter->evtpriv.c2h_queue, (void*)NULL) != _SUCCESS)
				RTW_ERR("%s rtw_cbuf_push fail\n", __func__);
		}
	}
	_set_workitem(&adapter->evtpriv.c2h_wk);
}
#endif

#ifdef CONFIG_USB_SUPPORT_ASYNC_VDN_REQ
int usb_write_async(struct usb_device *udev, u32 addr, void *pdata, u16 len)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	int ret;

	requesttype = VENDOR_WRITE;/* write_out */
	request = REALTEK_USB_VENQT_CMD_REQ;
	index = REALTEK_USB_VENQT_CMD_IDX;/* n/a */

	wvalue = (u16)(addr & 0x0000ffff);

	ret = _usbctrl_vendorreq_async_write(udev, request, wvalue, index, pdata, len, requesttype);

	return ret;
}

int usb_async_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{
	u8 data;
	int ret;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv *)pintfhdl->pintf_dev;
	struct usb_device *udev = pdvobjpriv->pusbdev;

	data = val;
	ret = usb_write_async(udev, addr, &data, 1);

	return ret;
}

int usb_async_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{
	u16 data;
	int ret;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv *)pintfhdl->pintf_dev;
	struct usb_device *udev = pdvobjpriv->pusbdev;

	data = val;
	ret = usb_write_async(udev, addr, &data, 2);

	return ret;
}

int usb_async_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	u32 data;
	int ret;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv *)pintfhdl->pintf_dev;
	struct usb_device *udev = pdvobjpriv->pusbdev;

	data = val;
	ret = usb_write_async(udev, addr, &data, 4);

	return ret;
}
#endif /* CONFIG_USB_SUPPORT_ASYNC_VDN_REQ */

u8 usb_read8(struct intf_hdl *pintfhdl, u32 addr)
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

	wvalue = (u16)(addr & 0x0000ffff);
	len = 1;
	usbctrl_vendorreq(pintfhdl, request, wvalue, index,
			  &data, len, requesttype);


	return data;
}

u16 usb_read16(struct intf_hdl *pintfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u16 data = 0;


	request = 0x05;
	requesttype = 0x01;/* read_in */
	index = 0;/* n/a */

	wvalue = (u16)(addr & 0x0000ffff);
	len = 2;
	usbctrl_vendorreq(pintfhdl, request, wvalue, index,
			  &data, len, requesttype);


	return data;

}

u32 usb_read32(struct intf_hdl *pintfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data = 0;


	request = 0x05;
	requesttype = 0x01;/* read_in */
	index = 0;/* n/a */

	wvalue = (u16)(addr & 0x0000ffff);
	len = 4;
	usbctrl_vendorreq(pintfhdl, request, wvalue, index,
			  &data, len, requesttype);


	return data;
}

int usb_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
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

	wvalue = (u16)(addr & 0x0000ffff);
	len = 1;

	data = val;
	ret = usbctrl_vendorreq(pintfhdl, request, wvalue, index,
				&data, len, requesttype);


	return ret;
}

int usb_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u16 data;
	int ret;


	request = 0x05;
	requesttype = 0x00;/* write_out */
	index = 0;/* n/a */

	wvalue = (u16)(addr & 0x0000ffff);
	len = 2;

	data = val;
	ret = usbctrl_vendorreq(pintfhdl, request, wvalue, index,
				&data, len, requesttype);


	return ret;

}

int usb_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	int ret;


	request = 0x05;
	requesttype = 0x00;/* write_out */
	index = 0;/* n/a */

	wvalue = (u16)(addr & 0x0000ffff);
	len = 4;
	data = val;
	ret = usbctrl_vendorreq(pintfhdl, request, wvalue, index,
				&data, len, requesttype);


	return ret;

}

int usb_writeN(struct intf_hdl *pintfhdl, u32 addr, u32 length, u8 *pdata)
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

	wvalue = (u16)(addr & 0x0000ffff);
	len = length;
	_rtw_memcpy(buf, pdata, len);
	ret = usbctrl_vendorreq(pintfhdl, request, wvalue, index,
				buf, len, requesttype);


	return ret;
}

void usb_set_intf_ops(_adapter *padapter, struct _io_ops *pops)
{
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
	pops->_write8_async = &usb_async_write8;
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

}
