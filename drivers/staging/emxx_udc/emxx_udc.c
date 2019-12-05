// SPDX-License-Identifier: GPL-2.0
/*
 *  drivers/usb/gadget/emxx_udc.c
 *     EMXX FCD (Function Controller Driver) for USB.
 *
 *  Copyright (C) 2010 Renesas Electronics Corporation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/device.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include <linux/irq.h>
#include <linux/gpio/consumer.h>

#include "emxx_udc.h"

#define	DRIVER_DESC	"EMXX UDC driver"
#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

static const char	driver_name[] = "emxx_udc";
static const char	driver_desc[] = DRIVER_DESC;

/*===========================================================================*/
/* Prototype */
static void _nbu2ss_ep_dma_abort(struct nbu2ss_udc *, struct nbu2ss_ep *);
static void _nbu2ss_ep0_enable(struct nbu2ss_udc *);
/*static void _nbu2ss_ep0_disable(struct nbu2ss_udc *);*/
static void _nbu2ss_ep_done(struct nbu2ss_ep *, struct nbu2ss_req *, int);
static void _nbu2ss_set_test_mode(struct nbu2ss_udc *, u32 mode);
static void _nbu2ss_endpoint_toggle_reset(struct nbu2ss_udc *udc, u8 ep_adrs);

static int _nbu2ss_pullup(struct nbu2ss_udc *, int);
static void _nbu2ss_fifo_flush(struct nbu2ss_udc *, struct nbu2ss_ep *);

/*===========================================================================*/
/* Macro */
#define	_nbu2ss_zero_len_pkt(udc, epnum)	\
	_nbu2ss_ep_in_end(udc, epnum, 0, 0)

/*===========================================================================*/
/* Global */
static struct nbu2ss_udc udc_controller;

/*-------------------------------------------------------------------------*/
/* Read */
static inline u32 _nbu2ss_readl(void __iomem *address)
{
	return __raw_readl(address);
}

/*-------------------------------------------------------------------------*/
/* Write */
static inline void _nbu2ss_writel(void __iomem *address, u32 udata)
{
	__raw_writel(udata, address);
}

/*-------------------------------------------------------------------------*/
/* Set Bit */
static inline void _nbu2ss_bitset(void __iomem *address, u32 udata)
{
	u32	reg_dt = __raw_readl(address) | (udata);

	__raw_writel(reg_dt, address);
}

/*-------------------------------------------------------------------------*/
/* Clear Bit */
static inline void _nbu2ss_bitclr(void __iomem *address, u32 udata)
{
	u32	reg_dt = __raw_readl(address) & ~(udata);

	__raw_writel(reg_dt, address);
}

#ifdef UDC_DEBUG_DUMP
/*-------------------------------------------------------------------------*/
static void _nbu2ss_dump_register(struct nbu2ss_udc *udc)
{
	int		i;
	u32 reg_data;

	pr_info("=== %s()\n", __func__);

	if (!udc) {
		pr_err("%s udc == NULL\n", __func__);
		return;
	}

	spin_unlock(&udc->lock);

	dev_dbg(&udc->dev, "\n-USB REG-\n");
	for (i = 0x0 ; i < USB_BASE_SIZE ; i += 16) {
		reg_data = _nbu2ss_readl(IO_ADDRESS(USB_BASE_ADDRESS + i));
		dev_dbg(&udc->dev, "USB%04x =%08x", i, (int)reg_data);

		reg_data = _nbu2ss_readl(IO_ADDRESS(USB_BASE_ADDRESS + i + 4));
		dev_dbg(&udc->dev, " %08x", (int)reg_data);

		reg_data = _nbu2ss_readl(IO_ADDRESS(USB_BASE_ADDRESS + i + 8));
		dev_dbg(&udc->dev, " %08x", (int)reg_data);

		reg_data = _nbu2ss_readl(IO_ADDRESS(USB_BASE_ADDRESS + i + 12));
		dev_dbg(&udc->dev, " %08x\n", (int)reg_data);
	}

	spin_lock(&udc->lock);
}
#endif /* UDC_DEBUG_DUMP */

/*-------------------------------------------------------------------------*/
/* Endpoint 0 Callback (Complete) */
static void _nbu2ss_ep0_complete(struct usb_ep *_ep, struct usb_request *_req)
{
	u8		recipient;
	u16		selector;
	u16		wIndex;
	u32		test_mode;
	struct usb_ctrlrequest	*p_ctrl;
	struct nbu2ss_udc *udc;

	if (!_ep || !_req)
		return;

	udc = (struct nbu2ss_udc *)_req->context;
	p_ctrl = &udc->ctrl;
	if ((p_ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		if (p_ctrl->bRequest == USB_REQ_SET_FEATURE) {
			/*-------------------------------------------------*/
			/* SET_FEATURE */
			recipient = (u8)(p_ctrl->bRequestType & USB_RECIP_MASK);
			selector  = le16_to_cpu(p_ctrl->wValue);
			if ((recipient == USB_RECIP_DEVICE) &&
			    (selector == USB_DEVICE_TEST_MODE)) {
				wIndex = le16_to_cpu(p_ctrl->wIndex);
				test_mode = (u32)(wIndex >> 8);
				_nbu2ss_set_test_mode(udc, test_mode);
			}
		}
	}
}

/*-------------------------------------------------------------------------*/
/* Initialization usb_request */
static void _nbu2ss_create_ep0_packet(struct nbu2ss_udc *udc,
				      void *p_buf, unsigned int length)
{
	udc->ep0_req.req.buf		= p_buf;
	udc->ep0_req.req.length		= length;
	udc->ep0_req.req.dma		= 0;
	udc->ep0_req.req.zero		= true;
	udc->ep0_req.req.complete	= _nbu2ss_ep0_complete;
	udc->ep0_req.req.status		= -EINPROGRESS;
	udc->ep0_req.req.context	= udc;
	udc->ep0_req.req.actual		= 0;
}

/*-------------------------------------------------------------------------*/
/* Acquisition of the first address of RAM(FIFO) */
static u32 _nbu2ss_get_begin_ram_address(struct nbu2ss_udc *udc)
{
	u32		num, buf_type;
	u32		data, last_ram_adr, use_ram_size;

	struct ep_regs __iomem *p_ep_regs;

	last_ram_adr = (D_RAM_SIZE_CTRL / sizeof(u32)) * 2;
	use_ram_size = 0;

	for (num = 0; num < NUM_ENDPOINTS - 1; num++) {
		p_ep_regs = &udc->p_regs->EP_REGS[num];
		data = _nbu2ss_readl(&p_ep_regs->EP_PCKT_ADRS);
		buf_type = _nbu2ss_readl(&p_ep_regs->EP_CONTROL) & EPN_BUF_TYPE;
		if (buf_type == 0) {
			/* Single Buffer */
			use_ram_size += (data & EPN_MPKT) / sizeof(u32);
		} else {
			/* Double Buffer */
			use_ram_size += ((data & EPN_MPKT) / sizeof(u32)) * 2;
		}

		if ((data >> 16) > last_ram_adr)
			last_ram_adr = data >> 16;
	}

	return last_ram_adr + use_ram_size;
}

/*-------------------------------------------------------------------------*/
/* Construction of Endpoint */
static int _nbu2ss_ep_init(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep)
{
	u32		num;
	u32		data;
	u32		begin_adrs;

	if (ep->epnum == 0)
		return	-EINVAL;

	num = ep->epnum - 1;

	/*-------------------------------------------------------------*/
	/* RAM Transfer Address */
	begin_adrs = _nbu2ss_get_begin_ram_address(udc);
	data = (begin_adrs << 16) | ep->ep.maxpacket;
	_nbu2ss_writel(&udc->p_regs->EP_REGS[num].EP_PCKT_ADRS, data);

	/*-------------------------------------------------------------*/
	/* Interrupt Enable */
	data = 1 << (ep->epnum + 8);
	_nbu2ss_bitset(&udc->p_regs->USB_INT_ENA, data);

	/*-------------------------------------------------------------*/
	/* Endpoint Type(Mode) */
	/*   Bulk, Interrupt, ISO */
	switch (ep->ep_type) {
	case USB_ENDPOINT_XFER_BULK:
		data = EPN_BULK;
		break;

	case USB_ENDPOINT_XFER_INT:
		data = EPN_BUF_SINGLE | EPN_INTERRUPT;
		break;

	case USB_ENDPOINT_XFER_ISOC:
		data = EPN_ISO;
		break;

	default:
		data = 0;
		break;
	}

	_nbu2ss_bitset(&udc->p_regs->EP_REGS[num].EP_CONTROL, data);
	_nbu2ss_endpoint_toggle_reset(udc, (ep->epnum | ep->direct));

	if (ep->direct == USB_DIR_OUT) {
		/*---------------------------------------------------------*/
		/* OUT */
		data = EPN_EN | EPN_BCLR | EPN_DIR0;
		_nbu2ss_bitset(&udc->p_regs->EP_REGS[num].EP_CONTROL, data);

		data = EPN_ONAK | EPN_OSTL_EN | EPN_OSTL;
		_nbu2ss_bitclr(&udc->p_regs->EP_REGS[num].EP_CONTROL, data);

		data = EPN_OUT_EN | EPN_OUT_END_EN;
		_nbu2ss_bitset(&udc->p_regs->EP_REGS[num].EP_INT_ENA, data);
	} else {
		/*---------------------------------------------------------*/
		/* IN */
		data = EPN_EN | EPN_BCLR | EPN_AUTO;
		_nbu2ss_bitset(&udc->p_regs->EP_REGS[num].EP_CONTROL, data);

		data = EPN_ISTL;
		_nbu2ss_bitclr(&udc->p_regs->EP_REGS[num].EP_CONTROL, data);

		data = EPN_IN_EN | EPN_IN_END_EN;
		_nbu2ss_bitset(&udc->p_regs->EP_REGS[num].EP_INT_ENA, data);
	}

	return 0;
}

/*-------------------------------------------------------------------------*/
/* Release of Endpoint */
static int _nbu2ss_epn_exit(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep)
{
	u32		num;
	u32		data;

	if ((ep->epnum == 0) || (udc->vbus_active == 0))
		return	-EINVAL;

	num = ep->epnum - 1;

	/*-------------------------------------------------------------*/
	/* RAM Transfer Address */
	_nbu2ss_writel(&udc->p_regs->EP_REGS[num].EP_PCKT_ADRS, 0);

	/*-------------------------------------------------------------*/
	/* Interrupt Disable */
	data = 1 << (ep->epnum + 8);
	_nbu2ss_bitclr(&udc->p_regs->USB_INT_ENA, data);

	if (ep->direct == USB_DIR_OUT) {
		/*---------------------------------------------------------*/
		/* OUT */
		data = EPN_ONAK | EPN_BCLR;
		_nbu2ss_bitset(&udc->p_regs->EP_REGS[num].EP_CONTROL, data);

		data = EPN_EN | EPN_DIR0;
		_nbu2ss_bitclr(&udc->p_regs->EP_REGS[num].EP_CONTROL, data);

		data = EPN_OUT_EN | EPN_OUT_END_EN;
		_nbu2ss_bitclr(&udc->p_regs->EP_REGS[num].EP_INT_ENA, data);
	} else {
		/*---------------------------------------------------------*/
		/* IN */
		data = EPN_BCLR;
		_nbu2ss_bitset(&udc->p_regs->EP_REGS[num].EP_CONTROL, data);

		data = EPN_EN | EPN_AUTO;
		_nbu2ss_bitclr(&udc->p_regs->EP_REGS[num].EP_CONTROL, data);

		data = EPN_IN_EN | EPN_IN_END_EN;
		_nbu2ss_bitclr(&udc->p_regs->EP_REGS[num].EP_INT_ENA, data);
	}

	return 0;
}

/*-------------------------------------------------------------------------*/
/* DMA setting (without Endpoint 0) */
static void _nbu2ss_ep_dma_init(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep)
{
	u32		num;
	u32		data;

	data = _nbu2ss_readl(&udc->p_regs->USBSSCONF);
	if (((ep->epnum == 0) || (data & (1 << ep->epnum)) == 0))
		return;		/* Not Support DMA */

	num = ep->epnum - 1;

	if (ep->direct == USB_DIR_OUT) {
		/*---------------------------------------------------------*/
		/* OUT */
		data = ep->ep.maxpacket;
		_nbu2ss_writel(&udc->p_regs->EP_DCR[num].EP_DCR2, data);

		/*---------------------------------------------------------*/
		/* Transfer Direct */
		data = DCR1_EPN_DIR0;
		_nbu2ss_bitset(&udc->p_regs->EP_DCR[num].EP_DCR1, data);

		/*---------------------------------------------------------*/
		/* DMA Mode etc. */
		data = EPN_STOP_MODE | EPN_STOP_SET  | EPN_DMAMODE0;
		_nbu2ss_writel(&udc->p_regs->EP_REGS[num].EP_DMA_CTRL, data);
	} else {
		/*---------------------------------------------------------*/
		/* IN */
		_nbu2ss_bitset(&udc->p_regs->EP_REGS[num].EP_CONTROL, EPN_AUTO);

		/*---------------------------------------------------------*/
		/* DMA Mode etc. */
		data = EPN_BURST_SET | EPN_DMAMODE0;
		_nbu2ss_writel(&udc->p_regs->EP_REGS[num].EP_DMA_CTRL, data);
	}
}

/*-------------------------------------------------------------------------*/
/* DMA setting release */
static void _nbu2ss_ep_dma_exit(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep)
{
	u32		num;
	u32		data;
	struct fc_regs __iomem *preg = udc->p_regs;

	if (udc->vbus_active == 0)
		return;		/* VBUS OFF */

	data = _nbu2ss_readl(&preg->USBSSCONF);
	if ((ep->epnum == 0) || ((data & (1 << ep->epnum)) == 0))
		return;		/* Not Support DMA */

	num = ep->epnum - 1;

	_nbu2ss_ep_dma_abort(udc, ep);

	if (ep->direct == USB_DIR_OUT) {
		/*---------------------------------------------------------*/
		/* OUT */
		_nbu2ss_writel(&preg->EP_DCR[num].EP_DCR2, 0);
		_nbu2ss_bitclr(&preg->EP_DCR[num].EP_DCR1, DCR1_EPN_DIR0);
		_nbu2ss_writel(&preg->EP_REGS[num].EP_DMA_CTRL, 0);
	} else {
		/*---------------------------------------------------------*/
		/* IN */
		_nbu2ss_bitclr(&preg->EP_REGS[num].EP_CONTROL, EPN_AUTO);
		_nbu2ss_writel(&preg->EP_REGS[num].EP_DMA_CTRL, 0);
	}
}

/*-------------------------------------------------------------------------*/
/* Abort DMA */
static void _nbu2ss_ep_dma_abort(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep)
{
	struct fc_regs __iomem *preg = udc->p_regs;

	_nbu2ss_bitclr(&preg->EP_DCR[ep->epnum - 1].EP_DCR1, DCR1_EPN_REQEN);
	mdelay(DMA_DISABLE_TIME);	/* DCR1_EPN_REQEN Clear */
	_nbu2ss_bitclr(&preg->EP_REGS[ep->epnum - 1].EP_DMA_CTRL, EPN_DMA_EN);
}

/*-------------------------------------------------------------------------*/
/* Start IN Transfer */
static void _nbu2ss_ep_in_end(struct nbu2ss_udc *udc,
			      u32 epnum, u32 data32, u32 length)
{
	u32		data;
	u32		num;
	struct fc_regs __iomem *preg = udc->p_regs;

	if (length >= sizeof(u32))
		return;

	if (epnum == 0) {
		_nbu2ss_bitclr(&preg->EP0_CONTROL, EP0_AUTO);

		/* Writing of 1-4 bytes */
		if (length)
			_nbu2ss_writel(&preg->EP0_WRITE, data32);

		data = ((length << 5) & EP0_DW) | EP0_DEND;
		_nbu2ss_writel(&preg->EP0_CONTROL, data);

		_nbu2ss_bitset(&preg->EP0_CONTROL, EP0_AUTO);
	} else {
		num = epnum - 1;

		_nbu2ss_bitclr(&preg->EP_REGS[num].EP_CONTROL, EPN_AUTO);

		/* Writing of 1-4 bytes */
		if (length)
			_nbu2ss_writel(&preg->EP_REGS[num].EP_WRITE, data32);

		data = (((length) << 5) & EPN_DW) | EPN_DEND;
		_nbu2ss_bitset(&preg->EP_REGS[num].EP_CONTROL, data);

		_nbu2ss_bitset(&preg->EP_REGS[num].EP_CONTROL, EPN_AUTO);
	}
}

#ifdef USE_DMA
/*-------------------------------------------------------------------------*/
static void _nbu2ss_dma_map_single(struct nbu2ss_udc *udc,
				   struct nbu2ss_ep *ep,
				   struct nbu2ss_req *req, u8 direct)
{
	if (req->req.dma == DMA_ADDR_INVALID) {
		if (req->unaligned) {
			req->req.dma = ep->phys_buf;
		} else {
			req->req.dma = dma_map_single(udc->gadget.dev.parent,
						      req->req.buf,
						      req->req.length,
						      (direct == USB_DIR_IN)
						      ? DMA_TO_DEVICE
						      : DMA_FROM_DEVICE);
		}
		req->mapped = 1;
	} else {
		if (!req->unaligned)
			dma_sync_single_for_device(udc->gadget.dev.parent,
						   req->req.dma,
						   req->req.length,
						   (direct == USB_DIR_IN)
						   ? DMA_TO_DEVICE
						   : DMA_FROM_DEVICE);

		req->mapped = 0;
	}
}

/*-------------------------------------------------------------------------*/
static void _nbu2ss_dma_unmap_single(struct nbu2ss_udc *udc,
				     struct nbu2ss_ep *ep,
				     struct nbu2ss_req *req, u8 direct)
{
	u8		data[4];
	u8		*p;
	u32		count = 0;

	if (direct == USB_DIR_OUT) {
		count = req->req.actual % 4;
		if (count) {
			p = req->req.buf;
			p += (req->req.actual - count);
			memcpy(data, p, count);
		}
	}

	if (req->mapped) {
		if (req->unaligned) {
			if (direct == USB_DIR_OUT)
				memcpy(req->req.buf, ep->virt_buf,
				       req->req.actual & 0xfffffffc);
		} else {
			dma_unmap_single(udc->gadget.dev.parent,
					 req->req.dma, req->req.length,
				(direct == USB_DIR_IN)
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE);
		}
		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
	} else {
		if (!req->unaligned)
			dma_sync_single_for_cpu(udc->gadget.dev.parent,
						req->req.dma, req->req.length,
				(direct == USB_DIR_IN)
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE);
	}

	if (count) {
		p = req->req.buf;
		p += (req->req.actual - count);
		memcpy(p, data, count);
	}
}
#endif

/*-------------------------------------------------------------------------*/
/* Endpoint 0 OUT Transfer (PIO) */
static int ep0_out_pio(struct nbu2ss_udc *udc, u8 *buf, u32 length)
{
	u32		i;
	u32 numreads = length / sizeof(u32);
	union usb_reg_access *buf32 = (union usb_reg_access *)buf;

	if (!numreads)
		return 0;

	/* PIO Read */
	for (i = 0; i < numreads; i++) {
		buf32->dw = _nbu2ss_readl(&udc->p_regs->EP0_READ);
		buf32++;
	}

	return  numreads * sizeof(u32);
}

/*-------------------------------------------------------------------------*/
/* Endpoint 0 OUT Transfer (PIO, OverBytes) */
static int ep0_out_overbytes(struct nbu2ss_udc *udc, u8 *p_buf, u32 length)
{
	u32		i;
	u32		i_read_size = 0;
	union usb_reg_access  temp_32;
	union usb_reg_access  *p_buf_32 = (union usb_reg_access *)p_buf;

	if ((length > 0) && (length < sizeof(u32))) {
		temp_32.dw = _nbu2ss_readl(&udc->p_regs->EP0_READ);
		for (i = 0 ; i < length ; i++)
			p_buf_32->byte.DATA[i] = temp_32.byte.DATA[i];
		i_read_size += length;
	}

	return i_read_size;
}

/*-------------------------------------------------------------------------*/
/* Endpoint 0 IN Transfer (PIO) */
static int EP0_in_PIO(struct nbu2ss_udc *udc, u8 *p_buf, u32 length)
{
	u32		i;
	u32		i_max_length   = EP0_PACKETSIZE;
	u32		i_word_length  = 0;
	u32		i_write_length = 0;
	union usb_reg_access  *p_buf_32 = (union usb_reg_access *)p_buf;

	/*------------------------------------------------------------*/
	/* Transfer Length */
	if (i_max_length < length)
		i_word_length = i_max_length / sizeof(u32);
	else
		i_word_length = length / sizeof(u32);

	/*------------------------------------------------------------*/
	/* PIO */
	for (i = 0; i < i_word_length; i++) {
		_nbu2ss_writel(&udc->p_regs->EP0_WRITE, p_buf_32->dw);
		p_buf_32++;
		i_write_length += sizeof(u32);
	}

	return i_write_length;
}

/*-------------------------------------------------------------------------*/
/* Endpoint 0 IN Transfer (PIO, OverBytes) */
static int ep0_in_overbytes(struct nbu2ss_udc *udc,
			    u8 *p_buf,
			    u32 i_remain_size)
{
	u32		i;
	union usb_reg_access  temp_32;
	union usb_reg_access  *p_buf_32 = (union usb_reg_access *)p_buf;

	if ((i_remain_size > 0) && (i_remain_size < sizeof(u32))) {
		for (i = 0 ; i < i_remain_size ; i++)
			temp_32.byte.DATA[i] = p_buf_32->byte.DATA[i];
		_nbu2ss_ep_in_end(udc, 0, temp_32.dw, i_remain_size);

		return i_remain_size;
	}

	return 0;
}

/*-------------------------------------------------------------------------*/
/* Transfer NULL Packet (Epndoint 0) */
static int EP0_send_NULL(struct nbu2ss_udc *udc, bool pid_flag)
{
	u32		data;

	data = _nbu2ss_readl(&udc->p_regs->EP0_CONTROL);
	data &= ~(u32)EP0_INAK;

	if (pid_flag)
		data |= (EP0_INAK_EN | EP0_PIDCLR | EP0_DEND);
	else
		data |= (EP0_INAK_EN | EP0_DEND);

	_nbu2ss_writel(&udc->p_regs->EP0_CONTROL, data);

	return 0;
}

/*-------------------------------------------------------------------------*/
/* Receive NULL Packet (Endpoint 0) */
static int EP0_receive_NULL(struct nbu2ss_udc *udc, bool pid_flag)
{
	u32		data;

	data = _nbu2ss_readl(&udc->p_regs->EP0_CONTROL);
	data &= ~(u32)EP0_ONAK;

	if (pid_flag)
		data |= EP0_PIDCLR;

	_nbu2ss_writel(&udc->p_regs->EP0_CONTROL, data);

	return 0;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_ep0_in_transfer(struct nbu2ss_udc *udc,
				   struct nbu2ss_req *req)
{
	u8		*p_buffer;			/* IN Data Buffer */
	u32		data;
	u32		i_remain_size = 0;
	int		result = 0;

	/*-------------------------------------------------------------*/
	/* End confirmation */
	if (req->req.actual == req->req.length) {
		if ((req->req.actual % EP0_PACKETSIZE) == 0) {
			if (req->zero) {
				req->zero = false;
				EP0_send_NULL(udc, false);
				return 1;
			}
		}

		return 0;		/* Transfer End */
	}

	/*-------------------------------------------------------------*/
	/* NAK release */
	data = _nbu2ss_readl(&udc->p_regs->EP0_CONTROL);
	data |= EP0_INAK_EN;
	data &= ~(u32)EP0_INAK;
	_nbu2ss_writel(&udc->p_regs->EP0_CONTROL, data);

	i_remain_size = req->req.length - req->req.actual;
	p_buffer = (u8 *)req->req.buf;
	p_buffer += req->req.actual;

	/*-------------------------------------------------------------*/
	/* Data transfer */
	result = EP0_in_PIO(udc, p_buffer, i_remain_size);

	req->div_len = result;
	i_remain_size -= result;

	if (i_remain_size == 0) {
		EP0_send_NULL(udc, false);
		return result;
	}

	if ((i_remain_size < sizeof(u32)) && (result != EP0_PACKETSIZE)) {
		p_buffer += result;
		result += ep0_in_overbytes(udc, p_buffer, i_remain_size);
		req->div_len = result;
	}

	return result;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_ep0_out_transfer(struct nbu2ss_udc *udc,
				    struct nbu2ss_req *req)
{
	u8		*p_buffer;
	u32		i_remain_size;
	u32		i_recv_length;
	int		result = 0;
	int		f_rcv_zero;

	/*-------------------------------------------------------------*/
	/* Receive data confirmation */
	i_recv_length = _nbu2ss_readl(&udc->p_regs->EP0_LENGTH) & EP0_LDATA;
	if (i_recv_length != 0) {
		f_rcv_zero = 0;

		i_remain_size = req->req.length - req->req.actual;
		p_buffer = (u8 *)req->req.buf;
		p_buffer += req->req.actual;

		result = ep0_out_pio(udc, p_buffer
					, min(i_remain_size, i_recv_length));
		if (result < 0)
			return result;

		req->req.actual += result;
		i_recv_length -= result;

		if ((i_recv_length > 0) && (i_recv_length < sizeof(u32))) {
			p_buffer += result;
			i_remain_size -= result;

			result = ep0_out_overbytes(udc, p_buffer
					, min(i_remain_size, i_recv_length));
			req->req.actual += result;
		}
	} else {
		f_rcv_zero = 1;
	}

	/*-------------------------------------------------------------*/
	/* End confirmation */
	if (req->req.actual == req->req.length) {
		if ((req->req.actual % EP0_PACKETSIZE) == 0) {
			if (req->zero) {
				req->zero = false;
				EP0_receive_NULL(udc, false);
				return 1;
			}
		}

		return 0;		/* Transfer End */
	}

	if ((req->req.actual % EP0_PACKETSIZE) != 0)
		return 0;		/* Short Packet Transfer End */

	if (req->req.actual > req->req.length) {
		dev_err(udc->dev, " *** Overrun Error\n");
		return -EOVERFLOW;
	}

	if (f_rcv_zero != 0) {
		i_remain_size = _nbu2ss_readl(&udc->p_regs->EP0_CONTROL);
		if (i_remain_size & EP0_ONAK) {
			/*---------------------------------------------------*/
			/* NACK release */
			_nbu2ss_bitclr(&udc->p_regs->EP0_CONTROL, EP0_ONAK);
		}
		result = 1;
	}

	return result;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_out_dma(struct nbu2ss_udc *udc, struct nbu2ss_req *req,
			   u32 num, u32 length)
{
	dma_addr_t	p_buffer;
	u32		mpkt;
	u32		lmpkt;
	u32		dmacnt;
	u32		burst = 1;
	u32		data;
	int		result = -EINVAL;
	struct fc_regs __iomem *preg = udc->p_regs;

	if (req->dma_flag)
		return 1;		/* DMA is forwarded */

	req->dma_flag = true;
	p_buffer = req->req.dma;
	p_buffer += req->req.actual;

	/* DMA Address */
	_nbu2ss_writel(&preg->EP_DCR[num].EP_TADR, (u32)p_buffer);

	/* Number of transfer packets */
	mpkt = _nbu2ss_readl(&preg->EP_REGS[num].EP_PCKT_ADRS) & EPN_MPKT;
	dmacnt = length / mpkt;
	lmpkt = (length % mpkt) & ~(u32)0x03;

	if (dmacnt > DMA_MAX_COUNT) {
		dmacnt = DMA_MAX_COUNT;
		lmpkt = 0;
	} else if (lmpkt != 0) {
		if (dmacnt == 0)
			burst = 0;	/* Burst OFF */
		dmacnt++;
	}

	data = mpkt | (lmpkt << 16);
	_nbu2ss_writel(&preg->EP_DCR[num].EP_DCR2, data);

	data = ((dmacnt & 0xff) << 16) | DCR1_EPN_DIR0 | DCR1_EPN_REQEN;
	_nbu2ss_writel(&preg->EP_DCR[num].EP_DCR1, data);

	if (burst == 0) {
		_nbu2ss_writel(&preg->EP_REGS[num].EP_LEN_DCNT, 0);
		_nbu2ss_bitclr(&preg->EP_REGS[num].EP_DMA_CTRL, EPN_BURST_SET);
	} else {
		_nbu2ss_writel(&preg->EP_REGS[num].EP_LEN_DCNT
				, (dmacnt << 16));
		_nbu2ss_bitset(&preg->EP_REGS[num].EP_DMA_CTRL, EPN_BURST_SET);
	}
	_nbu2ss_bitset(&preg->EP_REGS[num].EP_DMA_CTRL, EPN_DMA_EN);

	result = length & ~(u32)0x03;
	req->div_len = result;

	return result;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_epn_out_pio(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep,
			       struct nbu2ss_req *req, u32 length)
{
	u8		*p_buffer;
	u32		i;
	u32		data;
	u32		i_word_length;
	union usb_reg_access	temp_32;
	union usb_reg_access	*p_buf_32;
	int		result = 0;
	struct fc_regs __iomem *preg = udc->p_regs;

	if (req->dma_flag)
		return 1;		/* DMA is forwarded */

	if (length == 0)
		return 0;

	p_buffer = (u8 *)req->req.buf;
	p_buf_32 = (union usb_reg_access *)(p_buffer + req->req.actual);

	i_word_length = length / sizeof(u32);
	if (i_word_length > 0) {
		/*---------------------------------------------------------*/
		/* Copy of every four bytes */
		for (i = 0; i < i_word_length; i++) {
			p_buf_32->dw =
			_nbu2ss_readl(&preg->EP_REGS[ep->epnum - 1].EP_READ);
			p_buf_32++;
		}
		result = i_word_length * sizeof(u32);
	}

	data = length - result;
	if (data > 0) {
		/*---------------------------------------------------------*/
		/* Copy of fraction byte */
		temp_32.dw =
			_nbu2ss_readl(&preg->EP_REGS[ep->epnum - 1].EP_READ);
		for (i = 0 ; i < data ; i++)
			p_buf_32->byte.DATA[i] = temp_32.byte.DATA[i];
		result += data;
	}

	req->req.actual += result;

	if ((req->req.actual == req->req.length) ||
	    ((req->req.actual % ep->ep.maxpacket) != 0)) {
		result = 0;
	}

	return result;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_epn_out_data(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep,
				struct nbu2ss_req *req, u32 data_size)
{
	u32		num;
	u32		i_buf_size;
	int		nret = 1;

	if (ep->epnum == 0)
		return -EINVAL;

	num = ep->epnum - 1;

	i_buf_size = min((req->req.length - req->req.actual), data_size);

	if ((ep->ep_type != USB_ENDPOINT_XFER_INT) && (req->req.dma != 0) &&
	    (i_buf_size  >= sizeof(u32))) {
		nret = _nbu2ss_out_dma(udc, req, num, i_buf_size);
	} else {
		i_buf_size = min_t(u32, i_buf_size, ep->ep.maxpacket);
		nret = _nbu2ss_epn_out_pio(udc, ep, req, i_buf_size);
	}

	return nret;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_epn_out_transfer(struct nbu2ss_udc *udc,
				    struct nbu2ss_ep *ep,
				    struct nbu2ss_req *req)
{
	u32		num;
	u32		i_recv_length;
	int		result = 1;
	struct fc_regs __iomem *preg = udc->p_regs;

	if (ep->epnum == 0)
		return -EINVAL;

	num = ep->epnum - 1;

	/*-------------------------------------------------------------*/
	/* Receive Length */
	i_recv_length =
		_nbu2ss_readl(&preg->EP_REGS[num].EP_LEN_DCNT) & EPN_LDATA;

	if (i_recv_length != 0) {
		result = _nbu2ss_epn_out_data(udc, ep, req, i_recv_length);
		if (i_recv_length < ep->ep.maxpacket) {
			if (i_recv_length == result) {
				req->req.actual += result;
				result = 0;
			}
		}
	} else {
		if ((req->req.actual == req->req.length) ||
		    ((req->req.actual % ep->ep.maxpacket) != 0)) {
			result = 0;
		}
	}

	if (result == 0) {
		if ((req->req.actual % ep->ep.maxpacket) == 0) {
			if (req->zero) {
				req->zero = false;
				return 1;
			}
		}
	}

	if (req->req.actual > req->req.length) {
		dev_err(udc->dev, " Overrun Error\n");
		dev_err(udc->dev, " actual = %d, length = %d\n",
			req->req.actual, req->req.length);
		result = -EOVERFLOW;
	}

	return result;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_in_dma(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep,
			  struct nbu2ss_req *req, u32 num, u32 length)
{
	dma_addr_t	p_buffer;
	u32		mpkt;		/* MaxPacketSize */
	u32		lmpkt;		/* Last Packet Data Size */
	u32		dmacnt;		/* IN Data Size */
	u32		i_write_length;
	u32		data;
	int		result = -EINVAL;
	struct fc_regs __iomem *preg = udc->p_regs;

	if (req->dma_flag)
		return 1;		/* DMA is forwarded */

#ifdef USE_DMA
	if (req->req.actual == 0)
		_nbu2ss_dma_map_single(udc, ep, req, USB_DIR_IN);
#endif
	req->dma_flag = true;

	/* MAX Packet Size */
	mpkt = _nbu2ss_readl(&preg->EP_REGS[num].EP_PCKT_ADRS) & EPN_MPKT;

	if ((DMA_MAX_COUNT * mpkt) < length)
		i_write_length = DMA_MAX_COUNT * mpkt;
	else
		i_write_length = length;

	/*------------------------------------------------------------*/
	/* Number of transmission packets */
	if (mpkt < i_write_length) {
		dmacnt = i_write_length / mpkt;
		lmpkt  = (i_write_length % mpkt) & ~(u32)0x3;
		if (lmpkt != 0)
			dmacnt++;
		else
			lmpkt = mpkt & ~(u32)0x3;

	} else {
		dmacnt = 1;
		lmpkt  = i_write_length & ~(u32)0x3;
	}

	/* Packet setting */
	data = mpkt | (lmpkt << 16);
	_nbu2ss_writel(&preg->EP_DCR[num].EP_DCR2, data);

	/* Address setting */
	p_buffer = req->req.dma;
	p_buffer += req->req.actual;
	_nbu2ss_writel(&preg->EP_DCR[num].EP_TADR, (u32)p_buffer);

	/* Packet and DMA setting */
	data = ((dmacnt & 0xff) << 16) | DCR1_EPN_REQEN;
	_nbu2ss_writel(&preg->EP_DCR[num].EP_DCR1, data);

	/* Packet setting of EPC */
	data = dmacnt << 16;
	_nbu2ss_writel(&preg->EP_REGS[num].EP_LEN_DCNT, data);

	/*DMA setting of EPC */
	_nbu2ss_bitset(&preg->EP_REGS[num].EP_DMA_CTRL, EPN_DMA_EN);

	result = i_write_length & ~(u32)0x3;
	req->div_len = result;

	return result;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_epn_in_pio(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep,
			      struct nbu2ss_req *req, u32 length)
{
	u8		*p_buffer;
	u32		i;
	u32		data;
	u32		i_word_length;
	union usb_reg_access	temp_32;
	union usb_reg_access	*p_buf_32 = NULL;
	int		result = 0;
	struct fc_regs __iomem *preg = udc->p_regs;

	if (req->dma_flag)
		return 1;		/* DMA is forwarded */

	if (length > 0) {
		p_buffer = (u8 *)req->req.buf;
		p_buf_32 = (union usb_reg_access *)(p_buffer + req->req.actual);

		i_word_length = length / sizeof(u32);
		if (i_word_length > 0) {
			for (i = 0; i < i_word_length; i++) {
				_nbu2ss_writel(
					&preg->EP_REGS[ep->epnum - 1].EP_WRITE,
					p_buf_32->dw);

				p_buf_32++;
			}
			result = i_word_length * sizeof(u32);
		}
	}

	if (result != ep->ep.maxpacket) {
		data = length - result;
		temp_32.dw = 0;
		for (i = 0 ; i < data ; i++)
			temp_32.byte.DATA[i] = p_buf_32->byte.DATA[i];

		_nbu2ss_ep_in_end(udc, ep->epnum, temp_32.dw, data);
		result += data;
	}

	req->div_len = result;

	return result;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_epn_in_data(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep,
			       struct nbu2ss_req *req, u32 data_size)
{
	u32		num;
	int		nret = 1;

	if (ep->epnum == 0)
		return -EINVAL;

	num = ep->epnum - 1;

	if ((ep->ep_type != USB_ENDPOINT_XFER_INT) && (req->req.dma != 0) &&
	    (data_size >= sizeof(u32))) {
		nret = _nbu2ss_in_dma(udc, ep, req, num, data_size);
	} else {
		data_size = min_t(u32, data_size, ep->ep.maxpacket);
		nret = _nbu2ss_epn_in_pio(udc, ep, req, data_size);
	}

	return nret;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_epn_in_transfer(struct nbu2ss_udc *udc,
				   struct nbu2ss_ep *ep, struct nbu2ss_req *req)
{
	u32		num;
	u32		i_buf_size;
	int		result = 0;
	u32		status;

	if (ep->epnum == 0)
		return -EINVAL;

	num = ep->epnum - 1;

	status = _nbu2ss_readl(&udc->p_regs->EP_REGS[num].EP_STATUS);

	/*-------------------------------------------------------------*/
	/* State confirmation of FIFO */
	if (req->req.actual == 0) {
		if ((status & EPN_IN_EMPTY) == 0)
			return 1;	/* Not Empty */

	} else {
		if ((status & EPN_IN_FULL) != 0)
			return 1;	/* Not Empty */
	}

	/*-------------------------------------------------------------*/
	/* Start transfer */
	i_buf_size = req->req.length - req->req.actual;
	if (i_buf_size > 0)
		result = _nbu2ss_epn_in_data(udc, ep, req, i_buf_size);
	else if (req->req.length == 0)
		_nbu2ss_zero_len_pkt(udc, ep->epnum);

	return result;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_start_transfer(struct nbu2ss_udc *udc,
				  struct nbu2ss_ep *ep,
				  struct nbu2ss_req *req,
				  bool	bflag)
{
	int		nret = -EINVAL;

	req->dma_flag = false;
	req->div_len = 0;

	if (req->req.length == 0) {
		req->zero = false;
	} else {
		if ((req->req.length % ep->ep.maxpacket) == 0)
			req->zero = req->req.zero;
		else
			req->zero = false;
	}

	if (ep->epnum == 0) {
		/* EP0 */
		switch (udc->ep0state) {
		case EP0_IN_DATA_PHASE:
			nret = _nbu2ss_ep0_in_transfer(udc, req);
			break;

		case EP0_OUT_DATA_PHASE:
			nret = _nbu2ss_ep0_out_transfer(udc, req);
			break;

		case EP0_IN_STATUS_PHASE:
			nret = EP0_send_NULL(udc, true);
			break;

		default:
			break;
		}

	} else {
		/* EPN */
		if (ep->direct == USB_DIR_OUT) {
			/* OUT */
			if (!bflag)
				nret = _nbu2ss_epn_out_transfer(udc, ep, req);
		} else {
			/* IN */
			nret = _nbu2ss_epn_in_transfer(udc, ep, req);
		}
	}

	return nret;
}

/*-------------------------------------------------------------------------*/
static void _nbu2ss_restert_transfer(struct nbu2ss_ep *ep)
{
	u32		length;
	bool	bflag = false;
	struct nbu2ss_req *req;

	req = list_first_entry_or_null(&ep->queue, struct nbu2ss_req, queue);
	if (!req)
		return;

	if (ep->epnum > 0) {
		length = _nbu2ss_readl(
			&ep->udc->p_regs->EP_REGS[ep->epnum - 1].EP_LEN_DCNT);

		length &= EPN_LDATA;
		if (length < ep->ep.maxpacket)
			bflag = true;
	}

	_nbu2ss_start_transfer(ep->udc, ep, req, bflag);
}

/*-------------------------------------------------------------------------*/
/*	Endpoint Toggle Reset */
static void _nbu2ss_endpoint_toggle_reset(struct nbu2ss_udc *udc, u8 ep_adrs)
{
	u8		num;
	u32		data;

	if ((ep_adrs == 0) || (ep_adrs == 0x80))
		return;

	num = (ep_adrs & 0x7F) - 1;

	if (ep_adrs & USB_DIR_IN)
		data = EPN_IPIDCLR;
	else
		data = EPN_BCLR | EPN_OPIDCLR;

	_nbu2ss_bitset(&udc->p_regs->EP_REGS[num].EP_CONTROL, data);
}

/*-------------------------------------------------------------------------*/
/*	Endpoint STALL set */
static void _nbu2ss_set_endpoint_stall(struct nbu2ss_udc *udc,
				       u8 ep_adrs, bool bstall)
{
	u8		num, epnum;
	u32		data;
	struct nbu2ss_ep *ep;
	struct fc_regs __iomem *preg = udc->p_regs;

	if ((ep_adrs == 0) || (ep_adrs == 0x80)) {
		if (bstall) {
			/* Set STALL */
			_nbu2ss_bitset(&preg->EP0_CONTROL, EP0_STL);
		} else {
			/* Clear STALL */
			_nbu2ss_bitclr(&preg->EP0_CONTROL, EP0_STL);
		}
	} else {
		epnum = ep_adrs & USB_ENDPOINT_NUMBER_MASK;
		num = epnum - 1;
		ep = &udc->ep[epnum];

		if (bstall) {
			/* Set STALL */
			ep->halted = true;

			if (ep_adrs & USB_DIR_IN)
				data = EPN_BCLR | EPN_ISTL;
			else
				data = EPN_OSTL_EN | EPN_OSTL;

			_nbu2ss_bitset(&preg->EP_REGS[num].EP_CONTROL, data);
		} else {
			/* Clear STALL */
			ep->stalled = false;
			if (ep_adrs & USB_DIR_IN) {
				_nbu2ss_bitclr(&preg->EP_REGS[num].EP_CONTROL
						, EPN_ISTL);
			} else {
				data =
				_nbu2ss_readl(&preg->EP_REGS[num].EP_CONTROL);

				data &= ~EPN_OSTL;
				data |= EPN_OSTL_EN;

				_nbu2ss_writel(&preg->EP_REGS[num].EP_CONTROL
						, data);
			}

			ep->stalled = false;
			if (ep->halted) {
				ep->halted = false;
				_nbu2ss_restert_transfer(ep);
			}
		}
	}
}

/*-------------------------------------------------------------------------*/
static void _nbu2ss_set_test_mode(struct nbu2ss_udc *udc, u32 mode)
{
	u32		data;

	if (mode > MAX_TEST_MODE_NUM)
		return;

	dev_info(udc->dev, "SET FEATURE : test mode = %d\n", mode);

	data = _nbu2ss_readl(&udc->p_regs->USB_CONTROL);
	data &= ~TEST_FORCE_ENABLE;
	data |= mode << TEST_MODE_SHIFT;

	_nbu2ss_writel(&udc->p_regs->USB_CONTROL, data);
	_nbu2ss_bitset(&udc->p_regs->TEST_CONTROL, CS_TESTMODEEN);
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_set_feature_device(struct nbu2ss_udc *udc,
				      u16 selector, u16 wIndex)
{
	int	result = -EOPNOTSUPP;

	switch (selector) {
	case USB_DEVICE_REMOTE_WAKEUP:
		if (wIndex == 0x0000) {
			udc->remote_wakeup = U2F_ENABLE;
			result = 0;
		}
		break;

	case USB_DEVICE_TEST_MODE:
		wIndex >>= 8;
		if (wIndex <= MAX_TEST_MODE_NUM)
			result = 0;
		break;

	default:
		break;
	}

	return result;
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_get_ep_stall(struct nbu2ss_udc *udc, u8 ep_adrs)
{
	u8		epnum;
	u32		data = 0, bit_data;
	struct fc_regs __iomem *preg = udc->p_regs;

	epnum = ep_adrs & ~USB_ENDPOINT_DIR_MASK;
	if (epnum == 0) {
		data = _nbu2ss_readl(&preg->EP0_CONTROL);
		bit_data = EP0_STL;

	} else {
		data = _nbu2ss_readl(&preg->EP_REGS[epnum - 1].EP_CONTROL);
		if ((data & EPN_EN) == 0)
			return -1;

		if (ep_adrs & USB_ENDPOINT_DIR_MASK)
			bit_data = EPN_ISTL;
		else
			bit_data = EPN_OSTL;
	}

	if ((data & bit_data) == 0)
		return 0;
	return 1;
}

/*-------------------------------------------------------------------------*/
static inline int _nbu2ss_req_feature(struct nbu2ss_udc *udc, bool bset)
{
	u8	recipient = (u8)(udc->ctrl.bRequestType & USB_RECIP_MASK);
	u8	direction = (u8)(udc->ctrl.bRequestType & USB_DIR_IN);
	u16	selector  = le16_to_cpu(udc->ctrl.wValue);
	u16	wIndex    = le16_to_cpu(udc->ctrl.wIndex);
	u8	ep_adrs;
	int	result = -EOPNOTSUPP;

	if ((udc->ctrl.wLength != 0x0000) ||
	    (direction != USB_DIR_OUT)) {
		return -EINVAL;
	}

	switch (recipient) {
	case USB_RECIP_DEVICE:
		if (bset)
			result =
			_nbu2ss_set_feature_device(udc, selector, wIndex);
		break;

	case USB_RECIP_ENDPOINT:
		if (0x0000 == (wIndex & 0xFF70)) {
			if (selector == USB_ENDPOINT_HALT) {
				ep_adrs = wIndex & 0xFF;
				if (!bset) {
					_nbu2ss_endpoint_toggle_reset(udc,
								      ep_adrs);
				}

				_nbu2ss_set_endpoint_stall(udc, ep_adrs, bset);

				result = 0;
			}
		}
		break;

	default:
		break;
	}

	if (result >= 0)
		_nbu2ss_create_ep0_packet(udc, udc->ep0_buf, 0);

	return result;
}

/*-------------------------------------------------------------------------*/
static inline enum usb_device_speed _nbu2ss_get_speed(struct nbu2ss_udc *udc)
{
	u32		data;
	enum usb_device_speed speed = USB_SPEED_FULL;

	data = _nbu2ss_readl(&udc->p_regs->USB_STATUS);
	if (data & HIGH_SPEED)
		speed = USB_SPEED_HIGH;

	return speed;
}

/*-------------------------------------------------------------------------*/
static void _nbu2ss_epn_set_stall(struct nbu2ss_udc *udc,
				  struct nbu2ss_ep *ep)
{
	u8	ep_adrs;
	u32	regdata;
	int	limit_cnt = 0;

	struct fc_regs __iomem *preg = udc->p_regs;

	if (ep->direct == USB_DIR_IN) {
		for (limit_cnt = 0
			; limit_cnt < IN_DATA_EMPTY_COUNT
			; limit_cnt++) {
			regdata = _nbu2ss_readl(
				&preg->EP_REGS[ep->epnum - 1].EP_STATUS);

			if ((regdata & EPN_IN_DATA) == 0)
				break;

			mdelay(1);
		}
	}

	ep_adrs = ep->epnum | ep->direct;
	_nbu2ss_set_endpoint_stall(udc, ep_adrs, 1);
}

/*-------------------------------------------------------------------------*/
static int std_req_get_status(struct nbu2ss_udc *udc)
{
	u32	length;
	u16	status_data = 0;
	u8	recipient = (u8)(udc->ctrl.bRequestType & USB_RECIP_MASK);
	u8	direction = (u8)(udc->ctrl.bRequestType & USB_DIR_IN);
	u8	ep_adrs;
	int	result = -EINVAL;

	if ((udc->ctrl.wValue != 0x0000) || (direction != USB_DIR_IN))
		return result;

	length =
		min_t(u16, le16_to_cpu(udc->ctrl.wLength), sizeof(status_data));
	switch (recipient) {
	case USB_RECIP_DEVICE:
		if (udc->ctrl.wIndex == 0x0000) {
			if (udc->gadget.is_selfpowered)
				status_data |= BIT(USB_DEVICE_SELF_POWERED);

			if (udc->remote_wakeup)
				status_data |= BIT(USB_DEVICE_REMOTE_WAKEUP);

			result = 0;
		}
		break;

	case USB_RECIP_ENDPOINT:
		if (0x0000 == (le16_to_cpu(udc->ctrl.wIndex) & 0xFF70)) {
			ep_adrs = (u8)(le16_to_cpu(udc->ctrl.wIndex) & 0xFF);
			result = _nbu2ss_get_ep_stall(udc, ep_adrs);

			if (result > 0)
				status_data |= BIT(USB_ENDPOINT_HALT);
		}
		break;

	default:
		break;
	}

	if (result >= 0) {
		memcpy(udc->ep0_buf, &status_data, length);
		_nbu2ss_create_ep0_packet(udc, udc->ep0_buf, length);
		_nbu2ss_ep0_in_transfer(udc, &udc->ep0_req);

	} else {
		dev_err(udc->dev, " Error GET_STATUS\n");
	}

	return result;
}

/*-------------------------------------------------------------------------*/
static int std_req_clear_feature(struct nbu2ss_udc *udc)
{
	return _nbu2ss_req_feature(udc, false);
}

/*-------------------------------------------------------------------------*/
static int std_req_set_feature(struct nbu2ss_udc *udc)
{
	return _nbu2ss_req_feature(udc, true);
}

/*-------------------------------------------------------------------------*/
static int std_req_set_address(struct nbu2ss_udc *udc)
{
	int		result = 0;
	u32		wValue = le16_to_cpu(udc->ctrl.wValue);

	if ((udc->ctrl.bRequestType != 0x00)	||
	    (udc->ctrl.wIndex != 0x0000)	||
		(udc->ctrl.wLength != 0x0000)) {
		return -EINVAL;
	}

	if (wValue != (wValue & 0x007F))
		return -EINVAL;

	wValue <<= USB_ADRS_SHIFT;

	_nbu2ss_writel(&udc->p_regs->USB_ADDRESS, wValue);
	_nbu2ss_create_ep0_packet(udc, udc->ep0_buf, 0);

	return result;
}

/*-------------------------------------------------------------------------*/
static int std_req_set_configuration(struct nbu2ss_udc *udc)
{
	u32 config_value = (u32)(le16_to_cpu(udc->ctrl.wValue) & 0x00ff);

	if ((udc->ctrl.wIndex != 0x0000)	||
	    (udc->ctrl.wLength != 0x0000)	||
		(udc->ctrl.bRequestType != 0x00)) {
		return -EINVAL;
	}

	udc->curr_config = config_value;

	if (config_value > 0) {
		_nbu2ss_bitset(&udc->p_regs->USB_CONTROL, CONF);
		udc->devstate = USB_STATE_CONFIGURED;

	} else {
		_nbu2ss_bitclr(&udc->p_regs->USB_CONTROL, CONF);
		udc->devstate = USB_STATE_ADDRESS;
	}

	return 0;
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_read_request_data(struct nbu2ss_udc *udc, u32 *pdata)
{
	*pdata = _nbu2ss_readl(&udc->p_regs->SETUP_DATA0);
	pdata++;
	*pdata = _nbu2ss_readl(&udc->p_regs->SETUP_DATA1);
}

/*-------------------------------------------------------------------------*/
static inline int _nbu2ss_decode_request(struct nbu2ss_udc *udc)
{
	bool			bcall_back = true;
	int			nret = -EINVAL;
	struct usb_ctrlrequest	*p_ctrl;

	p_ctrl = &udc->ctrl;
	_nbu2ss_read_request_data(udc, (u32 *)p_ctrl);

	/* ep0 state control */
	if (p_ctrl->wLength == 0) {
		udc->ep0state = EP0_IN_STATUS_PHASE;

	} else {
		if (p_ctrl->bRequestType & USB_DIR_IN)
			udc->ep0state = EP0_IN_DATA_PHASE;
		else
			udc->ep0state = EP0_OUT_DATA_PHASE;
	}

	if ((p_ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (p_ctrl->bRequest) {
		case USB_REQ_GET_STATUS:
			nret = std_req_get_status(udc);
			bcall_back = false;
			break;

		case USB_REQ_CLEAR_FEATURE:
			nret = std_req_clear_feature(udc);
			bcall_back = false;
			break;

		case USB_REQ_SET_FEATURE:
			nret = std_req_set_feature(udc);
			bcall_back = false;
			break;

		case USB_REQ_SET_ADDRESS:
			nret = std_req_set_address(udc);
			bcall_back = false;
			break;

		case USB_REQ_SET_CONFIGURATION:
			nret = std_req_set_configuration(udc);
			break;

		default:
			break;
		}
	}

	if (!bcall_back) {
		if (udc->ep0state == EP0_IN_STATUS_PHASE) {
			if (nret >= 0) {
				/*--------------------------------------*/
				/* Status Stage */
				nret = EP0_send_NULL(udc, true);
			}
		}

	} else {
		spin_unlock(&udc->lock);
		nret = udc->driver->setup(&udc->gadget, &udc->ctrl);
		spin_lock(&udc->lock);
	}

	if (nret < 0)
		udc->ep0state = EP0_IDLE;

	return nret;
}

/*-------------------------------------------------------------------------*/
static inline int _nbu2ss_ep0_in_data_stage(struct nbu2ss_udc *udc)
{
	int			nret;
	struct nbu2ss_req	*req;
	struct nbu2ss_ep	*ep = &udc->ep[0];

	req = list_first_entry_or_null(&ep->queue, struct nbu2ss_req, queue);
	if (!req)
		req = &udc->ep0_req;

	req->req.actual += req->div_len;
	req->div_len = 0;

	nret = _nbu2ss_ep0_in_transfer(udc, req);
	if (nret == 0) {
		udc->ep0state = EP0_OUT_STATUS_PAHSE;
		EP0_receive_NULL(udc, true);
	}

	return 0;
}

/*-------------------------------------------------------------------------*/
static inline int _nbu2ss_ep0_out_data_stage(struct nbu2ss_udc *udc)
{
	int			nret;
	struct nbu2ss_req	*req;
	struct nbu2ss_ep	*ep = &udc->ep[0];

	req = list_first_entry_or_null(&ep->queue, struct nbu2ss_req, queue);
	if (!req)
		req = &udc->ep0_req;

	nret = _nbu2ss_ep0_out_transfer(udc, req);
	if (nret == 0) {
		udc->ep0state = EP0_IN_STATUS_PHASE;
		EP0_send_NULL(udc, true);

	} else if (nret < 0) {
		_nbu2ss_bitset(&udc->p_regs->EP0_CONTROL, EP0_BCLR);
		req->req.status = nret;
	}

	return 0;
}

/*-------------------------------------------------------------------------*/
static inline int _nbu2ss_ep0_status_stage(struct nbu2ss_udc *udc)
{
	struct nbu2ss_req	*req;
	struct nbu2ss_ep	*ep = &udc->ep[0];

	req = list_first_entry_or_null(&ep->queue, struct nbu2ss_req, queue);
	if (!req) {
		req = &udc->ep0_req;
		if (req->req.complete)
			req->req.complete(&ep->ep, &req->req);

	} else {
		if (req->req.complete)
			_nbu2ss_ep_done(ep, req, 0);
	}

	udc->ep0state = EP0_IDLE;

	return 0;
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_ep0_int(struct nbu2ss_udc *udc)
{
	int		i;
	u32		status;
	u32		intr;
	int		nret = -1;

	status = _nbu2ss_readl(&udc->p_regs->EP0_STATUS);
	intr = status & EP0_STATUS_RW_BIT;
	_nbu2ss_writel(&udc->p_regs->EP0_STATUS, ~intr);

	status &= (SETUP_INT | EP0_IN_INT | EP0_OUT_INT
			| STG_END_INT | EP0_OUT_NULL_INT);

	if (status == 0) {
		dev_info(udc->dev, "%s Not Decode Interrupt\n", __func__);
		dev_info(udc->dev, "EP0_STATUS = 0x%08x\n", intr);
		return;
	}

	if (udc->gadget.speed == USB_SPEED_UNKNOWN)
		udc->gadget.speed = _nbu2ss_get_speed(udc);

	for (i = 0; i < EP0_END_XFER; i++) {
		switch (udc->ep0state) {
		case EP0_IDLE:
			if (status & SETUP_INT) {
				status = 0;
				nret = _nbu2ss_decode_request(udc);
			}
			break;

		case EP0_IN_DATA_PHASE:
			if (status & EP0_IN_INT) {
				status &= ~EP0_IN_INT;
				nret = _nbu2ss_ep0_in_data_stage(udc);
			}
			break;

		case EP0_OUT_DATA_PHASE:
			if (status & EP0_OUT_INT) {
				status &= ~EP0_OUT_INT;
				nret = _nbu2ss_ep0_out_data_stage(udc);
			}
			break;

		case EP0_IN_STATUS_PHASE:
			if ((status & STG_END_INT) || (status & SETUP_INT)) {
				status &= ~(STG_END_INT | EP0_IN_INT);
				nret = _nbu2ss_ep0_status_stage(udc);
			}
			break;

		case EP0_OUT_STATUS_PAHSE:
			if ((status & STG_END_INT) || (status & SETUP_INT) ||
			    (status & EP0_OUT_NULL_INT)) {
				status &= ~(STG_END_INT
						| EP0_OUT_INT
						| EP0_OUT_NULL_INT);

				nret = _nbu2ss_ep0_status_stage(udc);
			}

			break;

		default:
			status = 0;
			break;
		}

		if (status == 0)
			break;
	}

	if (nret < 0) {
		/* Send Stall */
		_nbu2ss_set_endpoint_stall(udc, 0, true);
	}
}

/*-------------------------------------------------------------------------*/
static void _nbu2ss_ep_done(struct nbu2ss_ep *ep,
			    struct nbu2ss_req *req,
			    int status)
{
	struct nbu2ss_udc *udc = ep->udc;

	list_del_init(&req->queue);

	if (status == -ECONNRESET)
		_nbu2ss_fifo_flush(udc, ep);

	if (likely(req->req.status == -EINPROGRESS))
		req->req.status = status;

	if (ep->stalled) {
		_nbu2ss_epn_set_stall(udc, ep);
	} else {
		if (!list_empty(&ep->queue))
			_nbu2ss_restert_transfer(ep);
	}

#ifdef USE_DMA
	if ((ep->direct == USB_DIR_OUT) && (ep->epnum > 0) &&
	    (req->req.dma != 0))
		_nbu2ss_dma_unmap_single(udc, ep, req, USB_DIR_OUT);
#endif

	spin_unlock(&udc->lock);
	req->req.complete(&ep->ep, &req->req);
	spin_lock(&udc->lock);
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_epn_in_int(struct nbu2ss_udc *udc,
				      struct nbu2ss_ep *ep,
				      struct nbu2ss_req *req)
{
	int	result = 0;
	u32	status;

	struct fc_regs __iomem *preg = udc->p_regs;

	if (req->dma_flag)
		return;		/* DMA is forwarded */

	req->req.actual += req->div_len;
	req->div_len = 0;

	if (req->req.actual != req->req.length) {
		/*---------------------------------------------------------*/
		/* remainder of data */
		result = _nbu2ss_epn_in_transfer(udc, ep, req);

	} else {
		if (req->zero && ((req->req.actual % ep->ep.maxpacket) == 0)) {
			status =
			_nbu2ss_readl(&preg->EP_REGS[ep->epnum - 1].EP_STATUS);

			if ((status & EPN_IN_FULL) == 0) {
				/*-----------------------------------------*/
				/* 0 Length Packet */
				req->zero = false;
				_nbu2ss_zero_len_pkt(udc, ep->epnum);
			}
			return;
		}
	}

	if (result <= 0) {
		/*---------------------------------------------------------*/
		/* Complete */
		_nbu2ss_ep_done(ep, req, result);
	}
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_epn_out_int(struct nbu2ss_udc *udc,
				       struct nbu2ss_ep *ep,
				       struct nbu2ss_req *req)
{
	int	result;

	result = _nbu2ss_epn_out_transfer(udc, ep, req);
	if (result <= 0)
		_nbu2ss_ep_done(ep, req, result);
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_epn_in_dma_int(struct nbu2ss_udc *udc,
					  struct nbu2ss_ep *ep,
					  struct nbu2ss_req *req)
{
	u32		mpkt;
	u32		size;
	struct usb_request *preq;

	preq = &req->req;

	if (!req->dma_flag)
		return;

	preq->actual += req->div_len;
	req->div_len = 0;
	req->dma_flag = false;

#ifdef USE_DMA
	_nbu2ss_dma_unmap_single(udc, ep, req, USB_DIR_IN);
#endif

	if (preq->actual != preq->length) {
		_nbu2ss_epn_in_transfer(udc, ep, req);
	} else {
		mpkt = ep->ep.maxpacket;
		size = preq->actual % mpkt;
		if (size > 0) {
			if (((preq->actual & 0x03) == 0) && (size < mpkt))
				_nbu2ss_ep_in_end(udc, ep->epnum, 0, 0);
		} else {
			_nbu2ss_epn_in_int(udc, ep, req);
		}
	}
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_epn_out_dma_int(struct nbu2ss_udc *udc,
					   struct nbu2ss_ep *ep,
					   struct nbu2ss_req *req)
{
	int		i;
	u32		num;
	u32		dmacnt, ep_dmacnt;
	u32		mpkt;
	struct fc_regs __iomem *preg = udc->p_regs;

	num = ep->epnum - 1;

	if (req->req.actual == req->req.length) {
		if ((req->req.length % ep->ep.maxpacket) && !req->zero) {
			req->div_len = 0;
			req->dma_flag = false;
			_nbu2ss_ep_done(ep, req, 0);
			return;
		}
	}

	ep_dmacnt = _nbu2ss_readl(&preg->EP_REGS[num].EP_LEN_DCNT)
		 & EPN_DMACNT;
	ep_dmacnt >>= 16;

	for (i = 0; i < EPC_PLL_LOCK_COUNT; i++) {
		dmacnt = _nbu2ss_readl(&preg->EP_DCR[num].EP_DCR1)
			 & DCR1_EPN_DMACNT;
		dmacnt >>= 16;
		if (ep_dmacnt == dmacnt)
			break;
	}

	_nbu2ss_bitclr(&preg->EP_DCR[num].EP_DCR1, DCR1_EPN_REQEN);

	if (dmacnt != 0) {
		mpkt = ep->ep.maxpacket;
		if ((req->div_len % mpkt) == 0)
			req->div_len -= mpkt * dmacnt;
	}

	if ((req->req.actual % ep->ep.maxpacket) > 0) {
		if (req->req.actual == req->div_len) {
			req->div_len = 0;
			req->dma_flag = false;
			_nbu2ss_ep_done(ep, req, 0);
			return;
		}
	}

	req->req.actual += req->div_len;
	req->div_len = 0;
	req->dma_flag = false;

	_nbu2ss_epn_out_int(udc, ep, req);
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_epn_int(struct nbu2ss_udc *udc, u32 epnum)
{
	u32	num;
	u32	status;

	struct nbu2ss_req	*req;
	struct nbu2ss_ep	*ep = &udc->ep[epnum];

	num = epnum - 1;

	/* Interrupt Status */
	status = _nbu2ss_readl(&udc->p_regs->EP_REGS[num].EP_STATUS);

	/* Interrupt Clear */
	_nbu2ss_writel(&udc->p_regs->EP_REGS[num].EP_STATUS, ~status);

	req = list_first_entry_or_null(&ep->queue, struct nbu2ss_req, queue);
	if (!req) {
		/* pr_warn("=== %s(%d) req == NULL\n", __func__, epnum); */
		return;
	}

	if (status & EPN_OUT_END_INT) {
		status &= ~EPN_OUT_INT;
		_nbu2ss_epn_out_dma_int(udc, ep, req);
	}

	if (status & EPN_OUT_INT)
		_nbu2ss_epn_out_int(udc, ep, req);

	if (status & EPN_IN_END_INT) {
		status &= ~EPN_IN_INT;
		_nbu2ss_epn_in_dma_int(udc, ep, req);
	}

	if (status & EPN_IN_INT)
		_nbu2ss_epn_in_int(udc, ep, req);
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_ep_int(struct nbu2ss_udc *udc, u32 epnum)
{
	if (epnum == 0)
		_nbu2ss_ep0_int(udc);
	else
		_nbu2ss_epn_int(udc, epnum);
}

/*-------------------------------------------------------------------------*/
static void _nbu2ss_ep0_enable(struct nbu2ss_udc *udc)
{
	_nbu2ss_bitset(&udc->p_regs->EP0_CONTROL, (EP0_AUTO | EP0_BCLR));
	_nbu2ss_writel(&udc->p_regs->EP0_INT_ENA, EP0_INT_EN_BIT);
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_nuke(struct nbu2ss_udc *udc,
			struct nbu2ss_ep *ep,
			int status)
{
	struct nbu2ss_req *req;

	/* Endpoint Disable */
	_nbu2ss_epn_exit(udc, ep);

	/* DMA Disable */
	_nbu2ss_ep_dma_exit(udc, ep);

	if (list_empty(&ep->queue))
		return 0;

	/* called with irqs blocked */
	list_for_each_entry(req, &ep->queue, queue) {
		_nbu2ss_ep_done(ep, req, status);
	}

	return 0;
}

/*-------------------------------------------------------------------------*/
static void _nbu2ss_quiesce(struct nbu2ss_udc *udc)
{
	struct nbu2ss_ep	*ep;

	udc->gadget.speed = USB_SPEED_UNKNOWN;

	_nbu2ss_nuke(udc, &udc->ep[0], -ESHUTDOWN);

	/* Endpoint n */
	list_for_each_entry(ep, &udc->gadget.ep_list, ep.ep_list) {
		_nbu2ss_nuke(udc, ep, -ESHUTDOWN);
	}
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_pullup(struct nbu2ss_udc *udc, int is_on)
{
	u32	reg_dt;

	if (udc->vbus_active == 0)
		return -ESHUTDOWN;

	if (is_on) {
		/* D+ Pullup */
		if (udc->driver) {
			reg_dt = (_nbu2ss_readl(&udc->p_regs->USB_CONTROL)
				| PUE2) & ~(u32)CONNECTB;

			_nbu2ss_writel(&udc->p_regs->USB_CONTROL, reg_dt);
		}

	} else {
		/* D+ Pulldown */
		reg_dt = (_nbu2ss_readl(&udc->p_regs->USB_CONTROL) | CONNECTB)
			& ~(u32)PUE2;

		_nbu2ss_writel(&udc->p_regs->USB_CONTROL, reg_dt);
		udc->gadget.speed = USB_SPEED_UNKNOWN;
	}

	return 0;
}

/*-------------------------------------------------------------------------*/
static void _nbu2ss_fifo_flush(struct nbu2ss_udc *udc, struct nbu2ss_ep *ep)
{
	struct fc_regs __iomem *p = udc->p_regs;

	if (udc->vbus_active == 0)
		return;

	if (ep->epnum == 0) {
		/* EP0 */
		_nbu2ss_bitset(&p->EP0_CONTROL, EP0_BCLR);

	} else {
		/* EPN */
		_nbu2ss_ep_dma_abort(udc, ep);
		_nbu2ss_bitset(&p->EP_REGS[ep->epnum - 1].EP_CONTROL, EPN_BCLR);
	}
}

/*-------------------------------------------------------------------------*/
static int _nbu2ss_enable_controller(struct nbu2ss_udc *udc)
{
	int	waitcnt = 0;

	if (udc->udc_enabled)
		return 0;

	/* Reset */
	_nbu2ss_bitset(&udc->p_regs->EPCTR, (DIRPD | EPC_RST));
	udelay(EPC_RST_DISABLE_TIME);	/* 1us wait */

	_nbu2ss_bitclr(&udc->p_regs->EPCTR, DIRPD);
	mdelay(EPC_DIRPD_DISABLE_TIME);	/* 1ms wait */

	_nbu2ss_bitclr(&udc->p_regs->EPCTR, EPC_RST);

	_nbu2ss_writel(&udc->p_regs->AHBSCTR, WAIT_MODE);

		_nbu2ss_writel(&udc->p_regs->AHBMCTR,
			       HBUSREQ_MODE | HTRANS_MODE | WBURST_TYPE);

	while (!(_nbu2ss_readl(&udc->p_regs->EPCTR) & PLL_LOCK)) {
		waitcnt++;
		udelay(1);	/* 1us wait */
		if (waitcnt == EPC_PLL_LOCK_COUNT) {
			dev_err(udc->dev, "*** Reset Cancel failed\n");
			return -EINVAL;
		}
	}

		_nbu2ss_bitset(&udc->p_regs->UTMI_CHARACTER_1, USB_SQUSET);

	_nbu2ss_bitset(&udc->p_regs->USB_CONTROL, (INT_SEL | SOF_RCV));

	/* EP0 */
	_nbu2ss_ep0_enable(udc);

	/* USB Interrupt Enable */
	_nbu2ss_bitset(&udc->p_regs->USB_INT_ENA, USB_INT_EN_BIT);

	udc->udc_enabled = true;

	return 0;
}

/*-------------------------------------------------------------------------*/
static void _nbu2ss_reset_controller(struct nbu2ss_udc *udc)
{
	_nbu2ss_bitset(&udc->p_regs->EPCTR, EPC_RST);
	_nbu2ss_bitclr(&udc->p_regs->EPCTR, EPC_RST);
}

/*-------------------------------------------------------------------------*/
static void _nbu2ss_disable_controller(struct nbu2ss_udc *udc)
{
	if (udc->udc_enabled) {
		udc->udc_enabled = false;
		_nbu2ss_reset_controller(udc);
		_nbu2ss_bitset(&udc->p_regs->EPCTR, (DIRPD | EPC_RST));
	}
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_check_vbus(struct nbu2ss_udc *udc)
{
	int	nret;
	u32	reg_dt;

	/* chattering */
	mdelay(VBUS_CHATTERING_MDELAY);		/* wait (ms) */

	/* VBUS ON Check*/
	reg_dt = gpiod_get_value(vbus_gpio);
	if (reg_dt == 0) {
		udc->linux_suspended = 0;

		_nbu2ss_reset_controller(udc);
		dev_info(udc->dev, " ----- VBUS OFF\n");

		if (udc->vbus_active == 1) {
			/* VBUS OFF */
			udc->vbus_active = 0;
			if (udc->usb_suspended) {
				udc->usb_suspended = 0;
				/* _nbu2ss_reset_controller(udc); */
			}
			udc->devstate = USB_STATE_NOTATTACHED;

			_nbu2ss_quiesce(udc);
			if (udc->driver) {
				spin_unlock(&udc->lock);
				udc->driver->disconnect(&udc->gadget);
				spin_lock(&udc->lock);
			}

			_nbu2ss_disable_controller(udc);
		}
	} else {
		mdelay(5);		/* wait (5ms) */
		reg_dt = gpiod_get_value(vbus_gpio);
		if (reg_dt == 0)
			return;

		dev_info(udc->dev, " ----- VBUS ON\n");

		if (udc->linux_suspended)
			return;

		if (udc->vbus_active == 0) {
			/* VBUS ON */
			udc->vbus_active = 1;
			udc->devstate = USB_STATE_POWERED;

			nret = _nbu2ss_enable_controller(udc);
			if (nret < 0) {
				_nbu2ss_disable_controller(udc);
				udc->vbus_active = 0;
				return;
			}

			_nbu2ss_pullup(udc, 1);

#ifdef UDC_DEBUG_DUMP
			_nbu2ss_dump_register(udc);
#endif /* UDC_DEBUG_DUMP */

		} else {
			if (udc->devstate == USB_STATE_POWERED)
				_nbu2ss_pullup(udc, 1);
		}
	}
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_int_bus_reset(struct nbu2ss_udc *udc)
{
	udc->devstate		= USB_STATE_DEFAULT;
	udc->remote_wakeup	= 0;

	_nbu2ss_quiesce(udc);

	udc->ep0state = EP0_IDLE;
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_int_usb_resume(struct nbu2ss_udc *udc)
{
	if (udc->usb_suspended == 1) {
		udc->usb_suspended = 0;
		if (udc->driver && udc->driver->resume) {
			spin_unlock(&udc->lock);
			udc->driver->resume(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}
}

/*-------------------------------------------------------------------------*/
static inline void _nbu2ss_int_usb_suspend(struct nbu2ss_udc *udc)
{
	u32	reg_dt;

	if (udc->usb_suspended == 0) {
		reg_dt = gpiod_get_value(vbus_gpio);

		if (reg_dt == 0)
			return;

		udc->usb_suspended = 1;
		if (udc->driver && udc->driver->suspend) {
			spin_unlock(&udc->lock);
			udc->driver->suspend(&udc->gadget);
			spin_lock(&udc->lock);
		}

		_nbu2ss_bitset(&udc->p_regs->USB_CONTROL, SUSPEND);
	}
}

/*-------------------------------------------------------------------------*/
/* VBUS (GPIO153) Interrupt */
static irqreturn_t _nbu2ss_vbus_irq(int irq, void *_udc)
{
	struct nbu2ss_udc	*udc = (struct nbu2ss_udc *)_udc;

	spin_lock(&udc->lock);
	_nbu2ss_check_vbus(udc);
	spin_unlock(&udc->lock);

	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/
/* Interrupt (udc) */
static irqreturn_t _nbu2ss_udc_irq(int irq, void *_udc)
{
	u8	suspend_flag = 0;
	u32	status;
	u32	epnum, int_bit;

	struct nbu2ss_udc	*udc = (struct nbu2ss_udc *)_udc;
	struct fc_regs __iomem *preg = udc->p_regs;

	if (gpiod_get_value(vbus_gpio) == 0) {
		_nbu2ss_writel(&preg->USB_INT_STA, ~USB_INT_STA_RW);
		_nbu2ss_writel(&preg->USB_INT_ENA, 0);
		return IRQ_HANDLED;
	}

	spin_lock(&udc->lock);

	for (;;) {
		if (gpiod_get_value(vbus_gpio) == 0) {
			_nbu2ss_writel(&preg->USB_INT_STA, ~USB_INT_STA_RW);
			_nbu2ss_writel(&preg->USB_INT_ENA, 0);
			status = 0;
		} else {
			status = _nbu2ss_readl(&preg->USB_INT_STA);
		}

		if (status == 0)
			break;

		_nbu2ss_writel(&preg->USB_INT_STA, ~(status & USB_INT_STA_RW));

		if (status & USB_RST_INT) {
			/* USB Reset */
			_nbu2ss_int_bus_reset(udc);
		}

		if (status & RSUM_INT) {
			/* Resume */
			_nbu2ss_int_usb_resume(udc);
		}

		if (status & SPND_INT) {
			/* Suspend */
			suspend_flag = 1;
		}

		if (status & EPN_INT) {
			/* EP INT */
			int_bit = status >> 8;

			for (epnum = 0; epnum < NUM_ENDPOINTS; epnum++) {
				if (0x01 & int_bit)
					_nbu2ss_ep_int(udc, epnum);

				int_bit >>= 1;

				if (int_bit == 0)
					break;
			}
		}
	}

	if (suspend_flag)
		_nbu2ss_int_usb_suspend(udc);

	spin_unlock(&udc->lock);

	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/
/* usb_ep_ops */
static int nbu2ss_ep_enable(struct usb_ep *_ep,
			    const struct usb_endpoint_descriptor *desc)
{
	u8		ep_type;
	unsigned long	flags;

	struct nbu2ss_ep	*ep;
	struct nbu2ss_udc	*udc;

	if (!_ep || !desc) {
		pr_err(" *** %s, bad param\n", __func__);
		return -EINVAL;
	}

	ep = container_of(_ep, struct nbu2ss_ep, ep);
	if (!ep->udc) {
		pr_err(" *** %s, ep == NULL !!\n", __func__);
		return -EINVAL;
	}

	ep_type = usb_endpoint_type(desc);
	if ((ep_type == USB_ENDPOINT_XFER_CONTROL) ||
	    (ep_type == USB_ENDPOINT_XFER_ISOC)) {
		pr_err(" *** %s, bat bmAttributes\n", __func__);
		return -EINVAL;
	}

	udc = ep->udc;
	if (udc->vbus_active == 0)
		return -ESHUTDOWN;

	if ((!udc->driver) || (udc->gadget.speed == USB_SPEED_UNKNOWN)) {
		dev_err(ep->udc->dev, " *** %s, udc !!\n", __func__);
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&udc->lock, flags);

	ep->desc = desc;
	ep->epnum = usb_endpoint_num(desc);
	ep->direct = desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK;
	ep->ep_type = ep_type;
	ep->wedged = 0;
	ep->halted = false;
	ep->stalled = false;

	ep->ep.maxpacket = le16_to_cpu(desc->wMaxPacketSize);

	/* DMA setting */
	_nbu2ss_ep_dma_init(udc, ep);

	/* Endpoint setting */
	_nbu2ss_ep_init(udc, ep);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_ep_disable(struct usb_ep *_ep)
{
	struct nbu2ss_ep	*ep;
	struct nbu2ss_udc	*udc;
	unsigned long		flags;

	if (!_ep) {
		pr_err(" *** %s, bad param\n", __func__);
		return -EINVAL;
	}

	ep = container_of(_ep, struct nbu2ss_ep, ep);
	if (!ep->udc) {
		pr_err("udc: *** %s, ep == NULL !!\n", __func__);
		return -EINVAL;
	}

	udc = ep->udc;
	if (udc->vbus_active == 0)
		return -ESHUTDOWN;

	spin_lock_irqsave(&udc->lock, flags);
	_nbu2ss_nuke(udc, ep, -EINPROGRESS);		/* dequeue request */
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/*-------------------------------------------------------------------------*/
static struct usb_request *nbu2ss_ep_alloc_request(struct usb_ep *ep,
						   gfp_t gfp_flags)
{
	struct nbu2ss_req *req;

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

#ifdef USE_DMA
	req->req.dma = DMA_ADDR_INVALID;
#endif
	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

/*-------------------------------------------------------------------------*/
static void nbu2ss_ep_free_request(struct usb_ep *_ep,
				   struct usb_request *_req)
{
	struct nbu2ss_req *req;

	if (_req) {
		req = container_of(_req, struct nbu2ss_req, req);

		kfree(req);
	}
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_ep_queue(struct usb_ep *_ep,
			   struct usb_request *_req, gfp_t gfp_flags)
{
	struct nbu2ss_req	*req;
	struct nbu2ss_ep	*ep;
	struct nbu2ss_udc	*udc;
	unsigned long		flags;
	bool			bflag;
	int			result = -EINVAL;

	/* catch various bogus parameters */
	if (!_ep || !_req) {
		if (!_ep)
			pr_err("udc: %s --- _ep == NULL\n", __func__);

		if (!_req)
			pr_err("udc: %s --- _req == NULL\n", __func__);

		return -EINVAL;
	}

	req = container_of(_req, struct nbu2ss_req, req);
	if (unlikely(!_req->complete ||
		     !_req->buf ||
		     !list_empty(&req->queue))) {
		if (!_req->complete)
			pr_err("udc: %s --- !_req->complete\n", __func__);

		if (!_req->buf)
			pr_err("udc:%s --- !_req->buf\n", __func__);

		if (!list_empty(&req->queue))
			pr_err("%s --- !list_empty(&req->queue)\n", __func__);

		return -EINVAL;
	}

	ep = container_of(_ep, struct nbu2ss_ep, ep);
	udc = ep->udc;

	if (udc->vbus_active == 0) {
		dev_info(udc->dev, "Can't ep_queue (VBUS OFF)\n");
		return -ESHUTDOWN;
	}

	if (unlikely(!udc->driver)) {
		dev_err(udc->dev, "%s, bogus device state %p\n", __func__,
			udc->driver);
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&udc->lock, flags);

#ifdef USE_DMA
	if ((uintptr_t)req->req.buf & 0x3)
		req->unaligned = true;
	else
		req->unaligned = false;

	if (req->unaligned) {
		if (!ep->virt_buf)
			ep->virt_buf = dma_alloc_coherent(NULL, PAGE_SIZE,
							  &ep->phys_buf,
							  GFP_ATOMIC | GFP_DMA);
		if (ep->epnum > 0)  {
			if (ep->direct == USB_DIR_IN)
				memcpy(ep->virt_buf, req->req.buf,
				       req->req.length);
		}
	}

	if ((ep->epnum > 0) && (ep->direct == USB_DIR_OUT) &&
	    (req->req.dma != 0))
		_nbu2ss_dma_map_single(udc, ep, req, USB_DIR_OUT);
#endif

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	bflag = list_empty(&ep->queue);
	list_add_tail(&req->queue, &ep->queue);

	if (bflag && !ep->stalled) {
		result = _nbu2ss_start_transfer(udc, ep, req, false);
		if (result < 0) {
			dev_err(udc->dev, " *** %s, result = %d\n", __func__,
				result);
			list_del(&req->queue);
		} else if ((ep->epnum > 0) && (ep->direct == USB_DIR_OUT)) {
#ifdef USE_DMA
			if (req->req.length < 4 &&
			    req->req.length == req->req.actual)
#else
			if (req->req.length == req->req.actual)
#endif
				_nbu2ss_ep_done(ep, req, result);
		}
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct nbu2ss_req	*req;
	struct nbu2ss_ep	*ep;
	struct nbu2ss_udc	*udc;
	unsigned long flags;

	/* catch various bogus parameters */
	if (!_ep || !_req) {
		/* pr_err("%s, bad param(1)\n", __func__); */
		return -EINVAL;
	}

	ep = container_of(_ep, struct nbu2ss_ep, ep);

	udc = ep->udc;
	if (!udc)
		return -EINVAL;

	spin_lock_irqsave(&udc->lock, flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req) {
			_nbu2ss_ep_done(ep, req, -ECONNRESET);
			spin_unlock_irqrestore(&udc->lock, flags);
			return 0;
		}
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	pr_debug("%s no queue(EINVAL)\n", __func__);

	return -EINVAL;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_ep_set_halt(struct usb_ep *_ep, int value)
{
	u8		ep_adrs;
	unsigned long	flags;

	struct nbu2ss_ep	*ep;
	struct nbu2ss_udc	*udc;

	if (!_ep) {
		pr_err("%s, bad param\n", __func__);
		return -EINVAL;
	}

	ep = container_of(_ep, struct nbu2ss_ep, ep);

	udc = ep->udc;
	if (!udc) {
		dev_err(ep->udc->dev, " *** %s, bad udc\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&udc->lock, flags);

	ep_adrs = ep->epnum | ep->direct;
	if (value == 0) {
		_nbu2ss_set_endpoint_stall(udc, ep_adrs, value);
		ep->stalled = false;
	} else {
		if (list_empty(&ep->queue))
			_nbu2ss_epn_set_stall(udc, ep);
		else
			ep->stalled = true;
	}

	if (value == 0)
		ep->wedged = 0;

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int nbu2ss_ep_set_wedge(struct usb_ep *_ep)
{
	return nbu2ss_ep_set_halt(_ep, 1);
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_ep_fifo_status(struct usb_ep *_ep)
{
	u32		data;
	struct nbu2ss_ep	*ep;
	struct nbu2ss_udc	*udc;
	unsigned long		flags;
	struct fc_regs	__iomem *preg;

	if (!_ep) {
		pr_err("%s, bad param\n", __func__);
		return -EINVAL;
	}

	ep = container_of(_ep, struct nbu2ss_ep, ep);

	udc = ep->udc;
	if (!udc) {
		dev_err(ep->udc->dev, "%s, bad udc\n", __func__);
		return -EINVAL;
	}

	preg = udc->p_regs;

	data = gpiod_get_value(vbus_gpio);
	if (data == 0)
		return -EINVAL;

	spin_lock_irqsave(&udc->lock, flags);

	if (ep->epnum == 0) {
		data = _nbu2ss_readl(&preg->EP0_LENGTH) & EP0_LDATA;

	} else {
		data = _nbu2ss_readl(&preg->EP_REGS[ep->epnum - 1].EP_LEN_DCNT)
			& EPN_LDATA;
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/*-------------------------------------------------------------------------*/
static void  nbu2ss_ep_fifo_flush(struct usb_ep *_ep)
{
	u32			data;
	struct nbu2ss_ep	*ep;
	struct nbu2ss_udc	*udc;
	unsigned long		flags;

	if (!_ep) {
		pr_err("udc: %s, bad param\n", __func__);
		return;
	}

	ep = container_of(_ep, struct nbu2ss_ep, ep);

	udc = ep->udc;
	if (!udc) {
		dev_err(ep->udc->dev, "%s, bad udc\n", __func__);
		return;
	}

	data = gpiod_get_value(vbus_gpio);
	if (data == 0)
		return;

	spin_lock_irqsave(&udc->lock, flags);
	_nbu2ss_fifo_flush(udc, ep);
	spin_unlock_irqrestore(&udc->lock, flags);
}

/*-------------------------------------------------------------------------*/
static const struct usb_ep_ops nbu2ss_ep_ops = {
	.enable		= nbu2ss_ep_enable,
	.disable	= nbu2ss_ep_disable,

	.alloc_request	= nbu2ss_ep_alloc_request,
	.free_request	= nbu2ss_ep_free_request,

	.queue		= nbu2ss_ep_queue,
	.dequeue	= nbu2ss_ep_dequeue,

	.set_halt	= nbu2ss_ep_set_halt,
	.set_wedge	= nbu2ss_ep_set_wedge,

	.fifo_status	= nbu2ss_ep_fifo_status,
	.fifo_flush	= nbu2ss_ep_fifo_flush,
};

/*-------------------------------------------------------------------------*/
/* usb_gadget_ops */

/*-------------------------------------------------------------------------*/
static int nbu2ss_gad_get_frame(struct usb_gadget *pgadget)
{
	u32			data;
	struct nbu2ss_udc	*udc;

	if (!pgadget) {
		pr_err("udc: %s, bad param\n", __func__);
		return -EINVAL;
	}

	udc = container_of(pgadget, struct nbu2ss_udc, gadget);
	data = gpiod_get_value(vbus_gpio);
	if (data == 0)
		return -EINVAL;

	return _nbu2ss_readl(&udc->p_regs->USB_ADDRESS) & FRAME;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_gad_wakeup(struct usb_gadget *pgadget)
{
	int	i;
	u32	data;

	struct nbu2ss_udc	*udc;

	if (!pgadget) {
		pr_err("%s, bad param\n", __func__);
		return -EINVAL;
	}

	udc = container_of(pgadget, struct nbu2ss_udc, gadget);

	data = gpiod_get_value(vbus_gpio);
	if (data == 0) {
		dev_warn(&pgadget->dev, "VBUS LEVEL = %d\n", data);
		return -EINVAL;
	}

	_nbu2ss_bitset(&udc->p_regs->EPCTR, PLL_RESUME);

	for (i = 0; i < EPC_PLL_LOCK_COUNT; i++) {
		data = _nbu2ss_readl(&udc->p_regs->EPCTR);

		if (data & PLL_LOCK)
			break;
	}

	_nbu2ss_bitclr(&udc->p_regs->EPCTR, PLL_RESUME);

	return 0;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_gad_set_selfpowered(struct usb_gadget *pgadget,
				      int is_selfpowered)
{
	struct nbu2ss_udc       *udc;
	unsigned long		flags;

	if (!pgadget) {
		pr_err("%s, bad param\n", __func__);
		return -EINVAL;
	}

	udc = container_of(pgadget, struct nbu2ss_udc, gadget);

	spin_lock_irqsave(&udc->lock, flags);
	pgadget->is_selfpowered = (is_selfpowered != 0);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_gad_vbus_session(struct usb_gadget *pgadget, int is_active)
{
	return 0;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_gad_vbus_draw(struct usb_gadget *pgadget, unsigned int mA)
{
	struct nbu2ss_udc	*udc;
	unsigned long		flags;

	if (!pgadget) {
		pr_err("%s, bad param\n", __func__);
		return -EINVAL;
	}

	udc = container_of(pgadget, struct nbu2ss_udc, gadget);

	spin_lock_irqsave(&udc->lock, flags);
	udc->mA = mA;
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_gad_pullup(struct usb_gadget *pgadget, int is_on)
{
	struct nbu2ss_udc	*udc;
	unsigned long		flags;

	if (!pgadget) {
		pr_err("%s, bad param\n", __func__);
		return -EINVAL;
	}

	udc = container_of(pgadget, struct nbu2ss_udc, gadget);

	if (!udc->driver) {
		pr_warn("%s, Not Regist Driver\n", __func__);
		return -EINVAL;
	}

	if (udc->vbus_active == 0)
		return -ESHUTDOWN;

	spin_lock_irqsave(&udc->lock, flags);
	_nbu2ss_pullup(udc, is_on);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_gad_ioctl(struct usb_gadget *pgadget,
			    unsigned int code, unsigned long param)
{
	return 0;
}

static const struct usb_gadget_ops nbu2ss_gadget_ops = {
	.get_frame		= nbu2ss_gad_get_frame,
	.wakeup			= nbu2ss_gad_wakeup,
	.set_selfpowered	= nbu2ss_gad_set_selfpowered,
	.vbus_session		= nbu2ss_gad_vbus_session,
	.vbus_draw		= nbu2ss_gad_vbus_draw,
	.pullup			= nbu2ss_gad_pullup,
	.ioctl			= nbu2ss_gad_ioctl,
};

static const struct {
	const char *name;
	const struct usb_ep_caps caps;
} ep_info[NUM_ENDPOINTS] = {
#define EP_INFO(_name, _caps) \
	{ \
		.name = _name, \
		.caps = _caps, \
	}

	EP_INFO("ep0",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_CONTROL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep1-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep2-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep3in-int",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_INT, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep4-iso",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ISO, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep5-iso",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ISO, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep6-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep7-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep8in-int",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_INT, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep9-iso",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ISO, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("epa-iso",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_ISO, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("epb-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("epc-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("epdin-int",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_INT, USB_EP_CAPS_DIR_IN)),

#undef EP_INFO
};

/*-------------------------------------------------------------------------*/
static void nbu2ss_drv_ep_init(struct nbu2ss_udc *udc)
{
	int	i;

	INIT_LIST_HEAD(&udc->gadget.ep_list);
	udc->gadget.ep0 = &udc->ep[0].ep;

	for (i = 0; i < NUM_ENDPOINTS; i++) {
		struct nbu2ss_ep *ep = &udc->ep[i];

		ep->udc = udc;
		ep->desc = NULL;

		ep->ep.driver_data = NULL;
		ep->ep.name = ep_info[i].name;
		ep->ep.caps = ep_info[i].caps;
		ep->ep.ops = &nbu2ss_ep_ops;

		usb_ep_set_maxpacket_limit(&ep->ep,
					   i == 0 ? EP0_PACKETSIZE
					   : EP_PACKETSIZE);

		list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);
		INIT_LIST_HEAD(&ep->queue);
	}

	list_del_init(&udc->ep[0].ep.ep_list);
}

/*-------------------------------------------------------------------------*/
/* platform_driver */
static int nbu2ss_drv_contest_init(struct platform_device *pdev,
				   struct nbu2ss_udc *udc)
{
	spin_lock_init(&udc->lock);
	udc->dev = &pdev->dev;

	udc->gadget.is_selfpowered = 1;
	udc->devstate = USB_STATE_NOTATTACHED;
	udc->pdev = pdev;
	udc->mA = 0;

	udc->pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	/* init Endpoint */
	nbu2ss_drv_ep_init(udc);

	/* init Gadget */
	udc->gadget.ops = &nbu2ss_gadget_ops;
	udc->gadget.ep0 = &udc->ep[0].ep;
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->gadget.name = driver_name;
	/* udc->gadget.is_dualspeed = 1; */

	device_initialize(&udc->gadget.dev);

	dev_set_name(&udc->gadget.dev, "gadget");
	udc->gadget.dev.parent = &pdev->dev;
	udc->gadget.dev.dma_mask = pdev->dev.dma_mask;

	return 0;
}

/*
 *	probe - binds to the platform device
 */
static int nbu2ss_drv_probe(struct platform_device *pdev)
{
	int	status = -ENODEV;
	struct nbu2ss_udc	*udc;
	int irq;
	void __iomem *mmio_base;

	udc = &udc_controller;
	memset(udc, 0, sizeof(struct nbu2ss_udc));

	platform_set_drvdata(pdev, udc);

	/* require I/O memory and IRQ to be provided as resources */
	mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmio_base))
		return PTR_ERR(mmio_base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	status = devm_request_irq(&pdev->dev, irq, _nbu2ss_udc_irq,
				  0, driver_name, udc);

	/* IO Memory */
	udc->p_regs = (struct fc_regs __iomem *)mmio_base;

	/* USB Function Controller Interrupt */
	if (status != 0) {
		dev_err(udc->dev, "request_irq(USB_UDC_IRQ_1) failed\n");
		return status;
	}

	/* Driver Initialization */
	status = nbu2ss_drv_contest_init(pdev, udc);
	if (status < 0) {
		/* Error */
		return status;
	}

	/* VBUS Interrupt */
	vbus_irq = gpiod_to_irq(vbus_gpio);
	irq_set_irq_type(vbus_irq, IRQ_TYPE_EDGE_BOTH);
	status = request_irq(vbus_irq,
			     _nbu2ss_vbus_irq, IRQF_SHARED, driver_name, udc);

	if (status != 0) {
		dev_err(udc->dev, "request_irq(vbus_irq) failed\n");
		return status;
	}

	return status;
}

/*-------------------------------------------------------------------------*/
static void nbu2ss_drv_shutdown(struct platform_device *pdev)
{
	struct nbu2ss_udc	*udc;

	udc = platform_get_drvdata(pdev);
	if (!udc)
		return;

	_nbu2ss_disable_controller(udc);
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_drv_remove(struct platform_device *pdev)
{
	struct nbu2ss_udc	*udc;
	struct nbu2ss_ep	*ep;
	int	i;

	udc = &udc_controller;

	for (i = 0; i < NUM_ENDPOINTS; i++) {
		ep = &udc->ep[i];
		if (ep->virt_buf)
			dma_free_coherent(NULL, PAGE_SIZE, (void *)ep->virt_buf,
					  ep->phys_buf);
	}

	/* Interrupt Handler - Release */
	free_irq(vbus_irq, udc);

	return 0;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct nbu2ss_udc	*udc;

	udc = platform_get_drvdata(pdev);
	if (!udc)
		return 0;

	if (udc->vbus_active) {
		udc->vbus_active = 0;
		udc->devstate = USB_STATE_NOTATTACHED;
		udc->linux_suspended = 1;

		if (udc->usb_suspended) {
			udc->usb_suspended = 0;
			_nbu2ss_reset_controller(udc);
		}

		_nbu2ss_quiesce(udc);
	}
	_nbu2ss_disable_controller(udc);

	return 0;
}

/*-------------------------------------------------------------------------*/
static int nbu2ss_drv_resume(struct platform_device *pdev)
{
	u32	data;
	struct nbu2ss_udc	*udc;

	udc = platform_get_drvdata(pdev);
	if (!udc)
		return 0;

	data = gpiod_get_value(vbus_gpio);
	if (data) {
		udc->vbus_active = 1;
		udc->devstate = USB_STATE_POWERED;
		_nbu2ss_enable_controller(udc);
		_nbu2ss_pullup(udc, 1);
	}

	udc->linux_suspended = 0;

	return 0;
}

static struct platform_driver udc_driver = {
	.probe		= nbu2ss_drv_probe,
	.shutdown	= nbu2ss_drv_shutdown,
	.remove		= nbu2ss_drv_remove,
	.suspend	= nbu2ss_drv_suspend,
	.resume		= nbu2ss_drv_resume,
	.driver		= {
		.name	= driver_name,
	},
};

module_platform_driver(udc_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_LICENSE("GPL");
