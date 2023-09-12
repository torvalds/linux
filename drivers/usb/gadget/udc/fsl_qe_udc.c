// SPDX-License-Identifier: GPL-2.0+
/*
 * driver/usb/gadget/fsl_qe_udc.c
 *
 * Copyright (c) 2006-2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * 	Xie Xiaobo <X.Xie@freescale.com>
 * 	Li Yang <leoli@freescale.com>
 * 	Based on bareboard code from Shlomi Gridish.
 *
 * Description:
 * Freescle QE/CPM USB Pheripheral Controller Driver
 * The controller can be found on MPC8360, MPC8272, and etc.
 * MPC8360 Rev 1.1 may need QE mircocode update
 */

#undef USB_TRACE

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <soc/fsl/qe/qe.h>
#include <asm/cpm.h>
#include <asm/dma.h>
#include <asm/reg.h>
#include "fsl_qe_udc.h"

#define DRIVER_DESC     "Freescale QE/CPM USB Device Controller driver"
#define DRIVER_AUTHOR   "Xie XiaoBo"
#define DRIVER_VERSION  "1.0"

#define DMA_ADDR_INVALID        (~(dma_addr_t)0)

static const char driver_name[] = "fsl_qe_udc";
static const char driver_desc[] = DRIVER_DESC;

/*ep name is important in gadget, it should obey the convention of ep_match()*/
static const char *const ep_name[] = {
	"ep0-control", /* everyone has ep0 */
	/* 3 configurable endpoints */
	"ep1",
	"ep2",
	"ep3",
};

static const struct usb_endpoint_descriptor qe_ep0_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	0,
	.bmAttributes =		USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize =	USB_MAX_CTRL_PAYLOAD,
};

/********************************************************************
 *      Internal Used Function Start
********************************************************************/
/*-----------------------------------------------------------------
 * done() - retire a request; caller blocked irqs
 *--------------------------------------------------------------*/
static void done(struct qe_ep *ep, struct qe_req *req, int status)
{
	struct qe_udc *udc = ep->udc;
	unsigned char stopped = ep->stopped;

	/* the req->queue pointer is used by ep_queue() func, in which
	 * the request will be added into a udc_ep->queue 'd tail
	 * so here the req will be dropped from the ep->queue
	 */
	list_del_init(&req->queue);

	/* req.status should be set as -EINPROGRESS in ep_queue() */
	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	if (req->mapped) {
		dma_unmap_single(udc->gadget.dev.parent,
			req->req.dma, req->req.length,
			ep_is_in(ep)
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE);
		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
	} else
		dma_sync_single_for_cpu(udc->gadget.dev.parent,
			req->req.dma, req->req.length,
			ep_is_in(ep)
				? DMA_TO_DEVICE
				: DMA_FROM_DEVICE);

	if (status && (status != -ESHUTDOWN))
		dev_vdbg(udc->dev, "complete %s req %p stat %d len %u/%u\n",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;
	spin_unlock(&udc->lock);

	usb_gadget_giveback_request(&ep->ep, &req->req);

	spin_lock(&udc->lock);

	ep->stopped = stopped;
}

/*-----------------------------------------------------------------
 * nuke(): delete all requests related to this ep
 *--------------------------------------------------------------*/
static void nuke(struct qe_ep *ep, int status)
{
	/* Whether this eq has request linked */
	while (!list_empty(&ep->queue)) {
		struct qe_req *req = NULL;
		req = list_entry(ep->queue.next, struct qe_req, queue);

		done(ep, req, status);
	}
}

/*---------------------------------------------------------------------------*
 * USB and Endpoint manipulate process, include parameter and register       *
 *---------------------------------------------------------------------------*/
/* @value: 1--set stall 0--clean stall */
static int qe_eprx_stall_change(struct qe_ep *ep, int value)
{
	u16 tem_usep;
	u8 epnum = ep->epnum;
	struct qe_udc *udc = ep->udc;

	tem_usep = in_be16(&udc->usb_regs->usb_usep[epnum]);
	tem_usep = tem_usep & ~USB_RHS_MASK;
	if (value == 1)
		tem_usep |= USB_RHS_STALL;
	else if (ep->dir == USB_DIR_IN)
		tem_usep |= USB_RHS_IGNORE_OUT;

	out_be16(&udc->usb_regs->usb_usep[epnum], tem_usep);
	return 0;
}

static int qe_eptx_stall_change(struct qe_ep *ep, int value)
{
	u16 tem_usep;
	u8 epnum = ep->epnum;
	struct qe_udc *udc = ep->udc;

	tem_usep = in_be16(&udc->usb_regs->usb_usep[epnum]);
	tem_usep = tem_usep & ~USB_THS_MASK;
	if (value == 1)
		tem_usep |= USB_THS_STALL;
	else if (ep->dir == USB_DIR_OUT)
		tem_usep |= USB_THS_IGNORE_IN;

	out_be16(&udc->usb_regs->usb_usep[epnum], tem_usep);

	return 0;
}

static int qe_ep0_stall(struct qe_udc *udc)
{
	qe_eptx_stall_change(&udc->eps[0], 1);
	qe_eprx_stall_change(&udc->eps[0], 1);
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->ep0_dir = 0;
	return 0;
}

static int qe_eprx_nack(struct qe_ep *ep)
{
	u8 epnum = ep->epnum;
	struct qe_udc *udc = ep->udc;

	if (ep->state == EP_STATE_IDLE) {
		/* Set the ep's nack */
		clrsetbits_be16(&udc->usb_regs->usb_usep[epnum],
				USB_RHS_MASK, USB_RHS_NACK);

		/* Mask Rx and Busy interrupts */
		clrbits16(&udc->usb_regs->usb_usbmr,
				(USB_E_RXB_MASK | USB_E_BSY_MASK));

		ep->state = EP_STATE_NACK;
	}
	return 0;
}

static int qe_eprx_normal(struct qe_ep *ep)
{
	struct qe_udc *udc = ep->udc;

	if (ep->state == EP_STATE_NACK) {
		clrsetbits_be16(&udc->usb_regs->usb_usep[ep->epnum],
				USB_RTHS_MASK, USB_THS_IGNORE_IN);

		/* Unmask RX interrupts */
		out_be16(&udc->usb_regs->usb_usber,
				USB_E_BSY_MASK | USB_E_RXB_MASK);
		setbits16(&udc->usb_regs->usb_usbmr,
				(USB_E_RXB_MASK | USB_E_BSY_MASK));

		ep->state = EP_STATE_IDLE;
		ep->has_data = 0;
	}

	return 0;
}

static int qe_ep_cmd_stoptx(struct qe_ep *ep)
{
	if (ep->udc->soc_type == PORT_CPM)
		cpm_command(CPM_USB_STOP_TX | (ep->epnum << CPM_USB_EP_SHIFT),
				CPM_USB_STOP_TX_OPCODE);
	else
		qe_issue_cmd(QE_USB_STOP_TX, QE_CR_SUBBLOCK_USB,
				ep->epnum, 0);

	return 0;
}

static int qe_ep_cmd_restarttx(struct qe_ep *ep)
{
	if (ep->udc->soc_type == PORT_CPM)
		cpm_command(CPM_USB_RESTART_TX | (ep->epnum <<
				CPM_USB_EP_SHIFT), CPM_USB_RESTART_TX_OPCODE);
	else
		qe_issue_cmd(QE_USB_RESTART_TX, QE_CR_SUBBLOCK_USB,
				ep->epnum, 0);

	return 0;
}

static int qe_ep_flushtxfifo(struct qe_ep *ep)
{
	struct qe_udc *udc = ep->udc;
	int i;

	i = (int)ep->epnum;

	qe_ep_cmd_stoptx(ep);
	out_8(&udc->usb_regs->usb_uscom,
		USB_CMD_FLUSH_FIFO | (USB_CMD_EP_MASK & (ep->epnum)));
	out_be16(&udc->ep_param[i]->tbptr, in_be16(&udc->ep_param[i]->tbase));
	out_be32(&udc->ep_param[i]->tstate, 0);
	out_be16(&udc->ep_param[i]->tbcnt, 0);

	ep->c_txbd = ep->txbase;
	ep->n_txbd = ep->txbase;
	qe_ep_cmd_restarttx(ep);
	return 0;
}

static int qe_ep_filltxfifo(struct qe_ep *ep)
{
	struct qe_udc *udc = ep->udc;

	out_8(&udc->usb_regs->usb_uscom,
			USB_CMD_STR_FIFO | (USB_CMD_EP_MASK & (ep->epnum)));
	return 0;
}

static int qe_epbds_reset(struct qe_udc *udc, int pipe_num)
{
	struct qe_ep *ep;
	u32 bdring_len;
	struct qe_bd __iomem *bd;
	int i;

	ep = &udc->eps[pipe_num];

	if (ep->dir == USB_DIR_OUT)
		bdring_len = USB_BDRING_LEN_RX;
	else
		bdring_len = USB_BDRING_LEN;

	bd = ep->rxbase;
	for (i = 0; i < (bdring_len - 1); i++) {
		out_be32((u32 __iomem *)bd, R_E | R_I);
		bd++;
	}
	out_be32((u32 __iomem *)bd, R_E | R_I | R_W);

	bd = ep->txbase;
	for (i = 0; i < USB_BDRING_LEN_TX - 1; i++) {
		out_be32(&bd->buf, 0);
		out_be32((u32 __iomem *)bd, 0);
		bd++;
	}
	out_be32((u32 __iomem *)bd, T_W);

	return 0;
}

static int qe_ep_reset(struct qe_udc *udc, int pipe_num)
{
	struct qe_ep *ep;
	u16 tmpusep;

	ep = &udc->eps[pipe_num];
	tmpusep = in_be16(&udc->usb_regs->usb_usep[pipe_num]);
	tmpusep &= ~USB_RTHS_MASK;

	switch (ep->dir) {
	case USB_DIR_BOTH:
		qe_ep_flushtxfifo(ep);
		break;
	case USB_DIR_OUT:
		tmpusep |= USB_THS_IGNORE_IN;
		break;
	case USB_DIR_IN:
		qe_ep_flushtxfifo(ep);
		tmpusep |= USB_RHS_IGNORE_OUT;
		break;
	default:
		break;
	}
	out_be16(&udc->usb_regs->usb_usep[pipe_num], tmpusep);

	qe_epbds_reset(udc, pipe_num);

	return 0;
}

static int qe_ep_toggledata01(struct qe_ep *ep)
{
	ep->data01 ^= 0x1;
	return 0;
}

static int qe_ep_bd_init(struct qe_udc *udc, unsigned char pipe_num)
{
	struct qe_ep *ep = &udc->eps[pipe_num];
	unsigned long tmp_addr = 0;
	struct usb_ep_para __iomem *epparam;
	int i;
	struct qe_bd __iomem *bd;
	int bdring_len;

	if (ep->dir == USB_DIR_OUT)
		bdring_len = USB_BDRING_LEN_RX;
	else
		bdring_len = USB_BDRING_LEN;

	epparam = udc->ep_param[pipe_num];
	/* alloc multi-ram for BD rings and set the ep parameters */
	tmp_addr = cpm_muram_alloc(sizeof(struct qe_bd) * (bdring_len +
				USB_BDRING_LEN_TX), QE_ALIGNMENT_OF_BD);
	if (IS_ERR_VALUE(tmp_addr))
		return -ENOMEM;

	out_be16(&epparam->rbase, (u16)tmp_addr);
	out_be16(&epparam->tbase, (u16)(tmp_addr +
				(sizeof(struct qe_bd) * bdring_len)));

	out_be16(&epparam->rbptr, in_be16(&epparam->rbase));
	out_be16(&epparam->tbptr, in_be16(&epparam->tbase));

	ep->rxbase = cpm_muram_addr(tmp_addr);
	ep->txbase = cpm_muram_addr(tmp_addr + (sizeof(struct qe_bd)
				* bdring_len));
	ep->n_rxbd = ep->rxbase;
	ep->e_rxbd = ep->rxbase;
	ep->n_txbd = ep->txbase;
	ep->c_txbd = ep->txbase;
	ep->data01 = 0; /* data0 */

	/* Init TX and RX bds */
	bd = ep->rxbase;
	for (i = 0; i < bdring_len - 1; i++) {
		out_be32(&bd->buf, 0);
		out_be32((u32 __iomem *)bd, 0);
		bd++;
	}
	out_be32(&bd->buf, 0);
	out_be32((u32 __iomem *)bd, R_W);

	bd = ep->txbase;
	for (i = 0; i < USB_BDRING_LEN_TX - 1; i++) {
		out_be32(&bd->buf, 0);
		out_be32((u32 __iomem *)bd, 0);
		bd++;
	}
	out_be32(&bd->buf, 0);
	out_be32((u32 __iomem *)bd, T_W);

	return 0;
}

static int qe_ep_rxbd_update(struct qe_ep *ep)
{
	unsigned int size;
	int i;
	unsigned int tmp;
	struct qe_bd __iomem *bd;
	unsigned int bdring_len;

	if (ep->rxbase == NULL)
		return -EINVAL;

	bd = ep->rxbase;

	ep->rxframe = kmalloc(sizeof(*ep->rxframe), GFP_ATOMIC);
	if (!ep->rxframe)
		return -ENOMEM;

	qe_frame_init(ep->rxframe);

	if (ep->dir == USB_DIR_OUT)
		bdring_len = USB_BDRING_LEN_RX;
	else
		bdring_len = USB_BDRING_LEN;

	size = (ep->ep.maxpacket + USB_CRC_SIZE + 2) * (bdring_len + 1);
	ep->rxbuffer = kzalloc(size, GFP_ATOMIC);
	if (!ep->rxbuffer) {
		kfree(ep->rxframe);
		return -ENOMEM;
	}

	ep->rxbuf_d = virt_to_phys((void *)ep->rxbuffer);
	if (ep->rxbuf_d == DMA_ADDR_INVALID) {
		ep->rxbuf_d = dma_map_single(ep->udc->gadget.dev.parent,
					ep->rxbuffer,
					size,
					DMA_FROM_DEVICE);
		ep->rxbufmap = 1;
	} else {
		dma_sync_single_for_device(ep->udc->gadget.dev.parent,
					ep->rxbuf_d, size,
					DMA_FROM_DEVICE);
		ep->rxbufmap = 0;
	}

	size = ep->ep.maxpacket + USB_CRC_SIZE + 2;
	tmp = ep->rxbuf_d;
	tmp = (u32)(((tmp >> 2) << 2) + 4);

	for (i = 0; i < bdring_len - 1; i++) {
		out_be32(&bd->buf, tmp);
		out_be32((u32 __iomem *)bd, (R_E | R_I));
		tmp = tmp + size;
		bd++;
	}
	out_be32(&bd->buf, tmp);
	out_be32((u32 __iomem *)bd, (R_E | R_I | R_W));

	return 0;
}

static int qe_ep_register_init(struct qe_udc *udc, unsigned char pipe_num)
{
	struct qe_ep *ep = &udc->eps[pipe_num];
	struct usb_ep_para __iomem *epparam;
	u16 usep, logepnum;
	u16 tmp;
	u8 rtfcr = 0;

	epparam = udc->ep_param[pipe_num];

	usep = 0;
	logepnum = (ep->ep.desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	usep |= (logepnum << USB_EPNUM_SHIFT);

	switch (ep->ep.desc->bmAttributes & 0x03) {
	case USB_ENDPOINT_XFER_BULK:
		usep |= USB_TRANS_BULK;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		usep |=  USB_TRANS_ISO;
		break;
	case USB_ENDPOINT_XFER_INT:
		usep |= USB_TRANS_INT;
		break;
	default:
		usep |= USB_TRANS_CTR;
		break;
	}

	switch (ep->dir) {
	case USB_DIR_OUT:
		usep |= USB_THS_IGNORE_IN;
		break;
	case USB_DIR_IN:
		usep |= USB_RHS_IGNORE_OUT;
		break;
	default:
		break;
	}
	out_be16(&udc->usb_regs->usb_usep[pipe_num], usep);

	rtfcr = 0x30;
	out_8(&epparam->rbmr, rtfcr);
	out_8(&epparam->tbmr, rtfcr);

	tmp = (u16)(ep->ep.maxpacket + USB_CRC_SIZE);
	/* MRBLR must be divisble by 4 */
	tmp = (u16)(((tmp >> 2) << 2) + 4);
	out_be16(&epparam->mrblr, tmp);

	return 0;
}

static int qe_ep_init(struct qe_udc *udc,
		      unsigned char pipe_num,
		      const struct usb_endpoint_descriptor *desc)
{
	struct qe_ep *ep = &udc->eps[pipe_num];
	unsigned long flags;
	int reval = 0;
	u16 max = 0;

	max = usb_endpoint_maxp(desc);

	/* check the max package size validate for this endpoint */
	/* Refer to USB2.0 spec table 9-13,
	*/
	if (pipe_num != 0) {
		switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
		case USB_ENDPOINT_XFER_BULK:
			if (strstr(ep->ep.name, "-iso")
					|| strstr(ep->ep.name, "-int"))
				goto en_done;
			switch (udc->gadget.speed) {
			case USB_SPEED_HIGH:
			if ((max == 128) || (max == 256) || (max == 512))
				break;
			fallthrough;
			default:
				switch (max) {
				case 4:
				case 8:
				case 16:
				case 32:
				case 64:
					break;
				default:
				case USB_SPEED_LOW:
					goto en_done;
				}
			}
			break;
		case USB_ENDPOINT_XFER_INT:
			if (strstr(ep->ep.name, "-iso"))	/* bulk is ok */
				goto en_done;
			switch (udc->gadget.speed) {
			case USB_SPEED_HIGH:
				if (max <= 1024)
					break;
				fallthrough;
			case USB_SPEED_FULL:
				if (max <= 64)
					break;
				fallthrough;
			default:
				if (max <= 8)
					break;
				goto en_done;
			}
			break;
		case USB_ENDPOINT_XFER_ISOC:
			if (strstr(ep->ep.name, "-bulk")
				|| strstr(ep->ep.name, "-int"))
				goto en_done;
			switch (udc->gadget.speed) {
			case USB_SPEED_HIGH:
				if (max <= 1024)
					break;
				fallthrough;
			case USB_SPEED_FULL:
				if (max <= 1023)
					break;
				fallthrough;
			default:
				goto en_done;
			}
			break;
		case USB_ENDPOINT_XFER_CONTROL:
			if (strstr(ep->ep.name, "-iso")
				|| strstr(ep->ep.name, "-int"))
				goto en_done;
			switch (udc->gadget.speed) {
			case USB_SPEED_HIGH:
			case USB_SPEED_FULL:
				switch (max) {
				case 1:
				case 2:
				case 4:
				case 8:
				case 16:
				case 32:
				case 64:
					break;
				default:
					goto en_done;
				}
				fallthrough;
			case USB_SPEED_LOW:
				switch (max) {
				case 1:
				case 2:
				case 4:
				case 8:
					break;
				default:
					goto en_done;
				}
			default:
				goto en_done;
			}
			break;

		default:
			goto en_done;
		}
	} /* if ep0*/

	spin_lock_irqsave(&udc->lock, flags);

	/* initialize ep structure */
	ep->ep.maxpacket = max;
	ep->tm = (u8)(desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK);
	ep->ep.desc = desc;
	ep->stopped = 0;
	ep->init = 1;

	if (pipe_num == 0) {
		ep->dir = USB_DIR_BOTH;
		udc->ep0_dir = USB_DIR_OUT;
		udc->ep0_state = WAIT_FOR_SETUP;
	} else	{
		switch (desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) {
		case USB_DIR_OUT:
			ep->dir = USB_DIR_OUT;
			break;
		case USB_DIR_IN:
			ep->dir = USB_DIR_IN;
		default:
			break;
		}
	}

	/* hardware special operation */
	qe_ep_bd_init(udc, pipe_num);
	if ((ep->tm == USBP_TM_CTL) || (ep->dir == USB_DIR_OUT)) {
		reval = qe_ep_rxbd_update(ep);
		if (reval)
			goto en_done1;
	}

	if ((ep->tm == USBP_TM_CTL) || (ep->dir == USB_DIR_IN)) {
		ep->txframe = kmalloc(sizeof(*ep->txframe), GFP_ATOMIC);
		if (!ep->txframe)
			goto en_done2;
		qe_frame_init(ep->txframe);
	}

	qe_ep_register_init(udc, pipe_num);

	/* Now HW will be NAKing transfers to that EP,
	 * until a buffer is queued to it. */
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
en_done2:
	kfree(ep->rxbuffer);
	kfree(ep->rxframe);
en_done1:
	spin_unlock_irqrestore(&udc->lock, flags);
en_done:
	dev_err(udc->dev, "failed to initialize %s\n", ep->ep.name);
	return -ENODEV;
}

static inline void qe_usb_enable(struct qe_udc *udc)
{
	setbits8(&udc->usb_regs->usb_usmod, USB_MODE_EN);
}

static inline void qe_usb_disable(struct qe_udc *udc)
{
	clrbits8(&udc->usb_regs->usb_usmod, USB_MODE_EN);
}

/*----------------------------------------------------------------------------*
 *		USB and EP basic manipulate function end		      *
 *----------------------------------------------------------------------------*/


/******************************************************************************
		UDC transmit and receive process
 ******************************************************************************/
static void recycle_one_rxbd(struct qe_ep *ep)
{
	u32 bdstatus;

	bdstatus = in_be32((u32 __iomem *)ep->e_rxbd);
	bdstatus = R_I | R_E | (bdstatus & R_W);
	out_be32((u32 __iomem *)ep->e_rxbd, bdstatus);

	if (bdstatus & R_W)
		ep->e_rxbd = ep->rxbase;
	else
		ep->e_rxbd++;
}

static void recycle_rxbds(struct qe_ep *ep, unsigned char stopatnext)
{
	u32 bdstatus;
	struct qe_bd __iomem *bd, *nextbd;
	unsigned char stop = 0;

	nextbd = ep->n_rxbd;
	bd = ep->e_rxbd;
	bdstatus = in_be32((u32 __iomem *)bd);

	while (!(bdstatus & R_E) && !(bdstatus & BD_LENGTH_MASK) && !stop) {
		bdstatus = R_E | R_I | (bdstatus & R_W);
		out_be32((u32 __iomem *)bd, bdstatus);

		if (bdstatus & R_W)
			bd = ep->rxbase;
		else
			bd++;

		bdstatus = in_be32((u32 __iomem *)bd);
		if (stopatnext && (bd == nextbd))
			stop = 1;
	}

	ep->e_rxbd = bd;
}

static void ep_recycle_rxbds(struct qe_ep *ep)
{
	struct qe_bd __iomem *bd = ep->n_rxbd;
	u32 bdstatus;
	u8 epnum = ep->epnum;
	struct qe_udc *udc = ep->udc;

	bdstatus = in_be32((u32 __iomem *)bd);
	if (!(bdstatus & R_E) && !(bdstatus & BD_LENGTH_MASK)) {
		bd = ep->rxbase +
				((in_be16(&udc->ep_param[epnum]->rbptr) -
				  in_be16(&udc->ep_param[epnum]->rbase))
				 >> 3);
		bdstatus = in_be32((u32 __iomem *)bd);

		if (bdstatus & R_W)
			bd = ep->rxbase;
		else
			bd++;

		ep->e_rxbd = bd;
		recycle_rxbds(ep, 0);
		ep->e_rxbd = ep->n_rxbd;
	} else
		recycle_rxbds(ep, 1);

	if (in_be16(&udc->usb_regs->usb_usber) & USB_E_BSY_MASK)
		out_be16(&udc->usb_regs->usb_usber, USB_E_BSY_MASK);

	if (ep->has_data <= 0 && (!list_empty(&ep->queue)))
		qe_eprx_normal(ep);

	ep->localnack = 0;
}

static void setup_received_handle(struct qe_udc *udc,
					struct usb_ctrlrequest *setup);
static int qe_ep_rxframe_handle(struct qe_ep *ep);
static void ep0_req_complete(struct qe_udc *udc, struct qe_req *req);
/* when BD PID is setup, handle the packet */
static int ep0_setup_handle(struct qe_udc *udc)
{
	struct qe_ep *ep = &udc->eps[0];
	struct qe_frame *pframe;
	unsigned int fsize;
	u8 *cp;

	pframe = ep->rxframe;
	if ((frame_get_info(pframe) & PID_SETUP)
			&& (udc->ep0_state == WAIT_FOR_SETUP)) {
		fsize = frame_get_length(pframe);
		if (unlikely(fsize != 8))
			return -EINVAL;
		cp = (u8 *)&udc->local_setup_buff;
		memcpy(cp, pframe->data, fsize);
		ep->data01 = 1;

		/* handle the usb command base on the usb_ctrlrequest */
		setup_received_handle(udc, &udc->local_setup_buff);
		return 0;
	}
	return -EINVAL;
}

static int qe_ep0_rx(struct qe_udc *udc)
{
	struct qe_ep *ep = &udc->eps[0];
	struct qe_frame *pframe;
	struct qe_bd __iomem *bd;
	u32 bdstatus, length;
	u32 vaddr;

	pframe = ep->rxframe;

	if (ep->dir == USB_DIR_IN) {
		dev_err(udc->dev, "ep0 not a control endpoint\n");
		return -EINVAL;
	}

	bd = ep->n_rxbd;
	bdstatus = in_be32((u32 __iomem *)bd);
	length = bdstatus & BD_LENGTH_MASK;

	while (!(bdstatus & R_E) && length) {
		if ((bdstatus & R_F) && (bdstatus & R_L)
			&& !(bdstatus & R_ERROR)) {
			if (length == USB_CRC_SIZE) {
				udc->ep0_state = WAIT_FOR_SETUP;
				dev_vdbg(udc->dev,
					"receive a ZLP in status phase\n");
			} else {
				qe_frame_clean(pframe);
				vaddr = (u32)phys_to_virt(in_be32(&bd->buf));
				frame_set_data(pframe, (u8 *)vaddr);
				frame_set_length(pframe,
						(length - USB_CRC_SIZE));
				frame_set_status(pframe, FRAME_OK);
				switch (bdstatus & R_PID) {
				case R_PID_SETUP:
					frame_set_info(pframe, PID_SETUP);
					break;
				case R_PID_DATA1:
					frame_set_info(pframe, PID_DATA1);
					break;
				default:
					frame_set_info(pframe, PID_DATA0);
					break;
				}

				if ((bdstatus & R_PID) == R_PID_SETUP)
					ep0_setup_handle(udc);
				else
					qe_ep_rxframe_handle(ep);
			}
		} else {
			dev_err(udc->dev, "The receive frame with error!\n");
		}

		/* note: don't clear the rxbd's buffer address */
		recycle_one_rxbd(ep);

		/* Get next BD */
		if (bdstatus & R_W)
			bd = ep->rxbase;
		else
			bd++;

		bdstatus = in_be32((u32 __iomem *)bd);
		length = bdstatus & BD_LENGTH_MASK;

	}

	ep->n_rxbd = bd;

	return 0;
}

static int qe_ep_rxframe_handle(struct qe_ep *ep)
{
	struct qe_frame *pframe;
	u8 framepid = 0;
	unsigned int fsize;
	u8 *cp;
	struct qe_req *req;

	pframe = ep->rxframe;

	if (frame_get_info(pframe) & PID_DATA1)
		framepid = 0x1;

	if (framepid != ep->data01) {
		dev_err(ep->udc->dev, "the data01 error!\n");
		return -EIO;
	}

	fsize = frame_get_length(pframe);
	if (list_empty(&ep->queue)) {
		dev_err(ep->udc->dev, "the %s have no requeue!\n", ep->name);
	} else {
		req = list_entry(ep->queue.next, struct qe_req, queue);

		cp = (u8 *)(req->req.buf) + req->req.actual;
		if (cp) {
			memcpy(cp, pframe->data, fsize);
			req->req.actual += fsize;
			if ((fsize < ep->ep.maxpacket) ||
					(req->req.actual >= req->req.length)) {
				if (ep->epnum == 0)
					ep0_req_complete(ep->udc, req);
				else
					done(ep, req, 0);
				if (list_empty(&ep->queue) && ep->epnum != 0)
					qe_eprx_nack(ep);
			}
		}
	}

	qe_ep_toggledata01(ep);

	return 0;
}

static void ep_rx_tasklet(struct tasklet_struct *t)
{
	struct qe_udc *udc = from_tasklet(udc, t, rx_tasklet);
	struct qe_ep *ep;
	struct qe_frame *pframe;
	struct qe_bd __iomem *bd;
	unsigned long flags;
	u32 bdstatus, length;
	u32 vaddr, i;

	spin_lock_irqsave(&udc->lock, flags);

	for (i = 1; i < USB_MAX_ENDPOINTS; i++) {
		ep = &udc->eps[i];

		if (ep->dir == USB_DIR_IN || ep->enable_tasklet == 0) {
			dev_dbg(udc->dev,
				"This is a transmit ep or disable tasklet!\n");
			continue;
		}

		pframe = ep->rxframe;
		bd = ep->n_rxbd;
		bdstatus = in_be32((u32 __iomem *)bd);
		length = bdstatus & BD_LENGTH_MASK;

		while (!(bdstatus & R_E) && length) {
			if (list_empty(&ep->queue)) {
				qe_eprx_nack(ep);
				dev_dbg(udc->dev,
					"The rxep have noreq %d\n",
					ep->has_data);
				break;
			}

			if ((bdstatus & R_F) && (bdstatus & R_L)
				&& !(bdstatus & R_ERROR)) {
				qe_frame_clean(pframe);
				vaddr = (u32)phys_to_virt(in_be32(&bd->buf));
				frame_set_data(pframe, (u8 *)vaddr);
				frame_set_length(pframe,
						(length - USB_CRC_SIZE));
				frame_set_status(pframe, FRAME_OK);
				switch (bdstatus & R_PID) {
				case R_PID_DATA1:
					frame_set_info(pframe, PID_DATA1);
					break;
				case R_PID_SETUP:
					frame_set_info(pframe, PID_SETUP);
					break;
				default:
					frame_set_info(pframe, PID_DATA0);
					break;
				}
				/* handle the rx frame */
				qe_ep_rxframe_handle(ep);
			} else {
				dev_err(udc->dev,
					"error in received frame\n");
			}
			/* note: don't clear the rxbd's buffer address */
			/*clear the length */
			out_be32((u32 __iomem *)bd, bdstatus & BD_STATUS_MASK);
			ep->has_data--;
			if (!(ep->localnack))
				recycle_one_rxbd(ep);

			/* Get next BD */
			if (bdstatus & R_W)
				bd = ep->rxbase;
			else
				bd++;

			bdstatus = in_be32((u32 __iomem *)bd);
			length = bdstatus & BD_LENGTH_MASK;
		}

		ep->n_rxbd = bd;

		if (ep->localnack)
			ep_recycle_rxbds(ep);

		ep->enable_tasklet = 0;
	} /* for i=1 */

	spin_unlock_irqrestore(&udc->lock, flags);
}

static int qe_ep_rx(struct qe_ep *ep)
{
	struct qe_udc *udc;
	struct qe_frame *pframe;
	struct qe_bd __iomem *bd;
	u16 swoffs, ucoffs, emptybds;

	udc = ep->udc;
	pframe = ep->rxframe;

	if (ep->dir == USB_DIR_IN) {
		dev_err(udc->dev, "transmit ep in rx function\n");
		return -EINVAL;
	}

	bd = ep->n_rxbd;

	swoffs = (u16)(bd - ep->rxbase);
	ucoffs = (u16)((in_be16(&udc->ep_param[ep->epnum]->rbptr) -
			in_be16(&udc->ep_param[ep->epnum]->rbase)) >> 3);
	if (swoffs < ucoffs)
		emptybds = USB_BDRING_LEN_RX - ucoffs + swoffs;
	else
		emptybds = swoffs - ucoffs;

	if (emptybds < MIN_EMPTY_BDS) {
		qe_eprx_nack(ep);
		ep->localnack = 1;
		dev_vdbg(udc->dev, "%d empty bds, send NACK\n", emptybds);
	}
	ep->has_data = USB_BDRING_LEN_RX - emptybds;

	if (list_empty(&ep->queue)) {
		qe_eprx_nack(ep);
		dev_vdbg(udc->dev, "The rxep have no req queued with %d BDs\n",
				ep->has_data);
		return 0;
	}

	tasklet_schedule(&udc->rx_tasklet);
	ep->enable_tasklet = 1;

	return 0;
}

/* send data from a frame, no matter what tx_req */
static int qe_ep_tx(struct qe_ep *ep, struct qe_frame *frame)
{
	struct qe_udc *udc = ep->udc;
	struct qe_bd __iomem *bd;
	u16 saveusbmr;
	u32 bdstatus, pidmask;
	u32 paddr;

	if (ep->dir == USB_DIR_OUT) {
		dev_err(udc->dev, "receive ep passed to tx function\n");
		return -EINVAL;
	}

	/* Disable the Tx interrupt */
	saveusbmr = in_be16(&udc->usb_regs->usb_usbmr);
	out_be16(&udc->usb_regs->usb_usbmr,
			saveusbmr & ~(USB_E_TXB_MASK | USB_E_TXE_MASK));

	bd = ep->n_txbd;
	bdstatus = in_be32((u32 __iomem *)bd);

	if (!(bdstatus & (T_R | BD_LENGTH_MASK))) {
		if (frame_get_length(frame) == 0) {
			frame_set_data(frame, udc->nullbuf);
			frame_set_length(frame, 2);
			frame->info |= (ZLP | NO_CRC);
			dev_vdbg(udc->dev, "the frame size = 0\n");
		}
		paddr = virt_to_phys((void *)frame->data);
		out_be32(&bd->buf, paddr);
		bdstatus = (bdstatus&T_W);
		if (!(frame_get_info(frame) & NO_CRC))
			bdstatus |= T_R | T_I | T_L | T_TC
					| frame_get_length(frame);
		else
			bdstatus |= T_R | T_I | T_L | frame_get_length(frame);

		/* if the packet is a ZLP in status phase */
		if ((ep->epnum == 0) && (udc->ep0_state == DATA_STATE_NEED_ZLP))
			ep->data01 = 0x1;

		if (ep->data01) {
			pidmask = T_PID_DATA1;
			frame->info |= PID_DATA1;
		} else {
			pidmask = T_PID_DATA0;
			frame->info |= PID_DATA0;
		}
		bdstatus |= T_CNF;
		bdstatus |= pidmask;
		out_be32((u32 __iomem *)bd, bdstatus);
		qe_ep_filltxfifo(ep);

		/* enable the TX interrupt */
		out_be16(&udc->usb_regs->usb_usbmr, saveusbmr);

		qe_ep_toggledata01(ep);
		if (bdstatus & T_W)
			ep->n_txbd = ep->txbase;
		else
			ep->n_txbd++;

		return 0;
	} else {
		out_be16(&udc->usb_regs->usb_usbmr, saveusbmr);
		dev_vdbg(udc->dev, "The tx bd is not ready!\n");
		return -EBUSY;
	}
}

/* when a bd was transmitted, the function can
 * handle the tx_req, not include ep0           */
static int txcomplete(struct qe_ep *ep, unsigned char restart)
{
	if (ep->tx_req != NULL) {
		struct qe_req *req = ep->tx_req;
		unsigned zlp = 0, last_len = 0;

		last_len = min_t(unsigned, req->req.length - ep->sent,
				ep->ep.maxpacket);

		if (!restart) {
			int asent = ep->last;
			ep->sent += asent;
			ep->last -= asent;
		} else {
			ep->last = 0;
		}

		/* zlp needed when req->re.zero is set */
		if (req->req.zero) {
			if (last_len == 0 ||
				(req->req.length % ep->ep.maxpacket) != 0)
				zlp = 0;
			else
				zlp = 1;
		} else
			zlp = 0;

		/* a request already were transmitted completely */
		if (((ep->tx_req->req.length - ep->sent) <= 0) && !zlp) {
			done(ep, ep->tx_req, 0);
			ep->tx_req = NULL;
			ep->last = 0;
			ep->sent = 0;
		}
	}

	/* we should gain a new tx_req fot this endpoint */
	if (ep->tx_req == NULL) {
		if (!list_empty(&ep->queue)) {
			ep->tx_req = list_entry(ep->queue.next,	struct qe_req,
							queue);
			ep->last = 0;
			ep->sent = 0;
		}
	}

	return 0;
}

/* give a frame and a tx_req, send some data */
static int qe_usb_senddata(struct qe_ep *ep, struct qe_frame *frame)
{
	unsigned int size;
	u8 *buf;

	qe_frame_clean(frame);
	size = min_t(u32, (ep->tx_req->req.length - ep->sent),
				ep->ep.maxpacket);
	buf = (u8 *)ep->tx_req->req.buf + ep->sent;
	if (buf && size) {
		ep->last = size;
		ep->tx_req->req.actual += size;
		frame_set_data(frame, buf);
		frame_set_length(frame, size);
		frame_set_status(frame, FRAME_OK);
		frame_set_info(frame, 0);
		return qe_ep_tx(ep, frame);
	}
	return -EIO;
}

/* give a frame struct,send a ZLP */
static int sendnulldata(struct qe_ep *ep, struct qe_frame *frame, uint infor)
{
	struct qe_udc *udc = ep->udc;

	if (frame == NULL)
		return -ENODEV;

	qe_frame_clean(frame);
	frame_set_data(frame, (u8 *)udc->nullbuf);
	frame_set_length(frame, 2);
	frame_set_status(frame, FRAME_OK);
	frame_set_info(frame, (ZLP | NO_CRC | infor));

	return qe_ep_tx(ep, frame);
}

static int frame_create_tx(struct qe_ep *ep, struct qe_frame *frame)
{
	struct qe_req *req = ep->tx_req;
	int reval;

	if (req == NULL)
		return -ENODEV;

	if ((req->req.length - ep->sent) > 0)
		reval = qe_usb_senddata(ep, frame);
	else
		reval = sendnulldata(ep, frame, 0);

	return reval;
}

/* if direction is DIR_IN, the status is Device->Host
 * if direction is DIR_OUT, the status transaction is Device<-Host
 * in status phase, udc create a request and gain status */
static int ep0_prime_status(struct qe_udc *udc, int direction)
{

	struct qe_ep *ep = &udc->eps[0];

	if (direction == USB_DIR_IN) {
		udc->ep0_state = DATA_STATE_NEED_ZLP;
		udc->ep0_dir = USB_DIR_IN;
		sendnulldata(ep, ep->txframe, SETUP_STATUS | NO_REQ);
	} else {
		udc->ep0_dir = USB_DIR_OUT;
		udc->ep0_state = WAIT_FOR_OUT_STATUS;
	}

	return 0;
}

/* a request complete in ep0, whether gadget request or udc request */
static void ep0_req_complete(struct qe_udc *udc, struct qe_req *req)
{
	struct qe_ep *ep = &udc->eps[0];
	/* because usb and ep's status already been set in ch9setaddress() */

	switch (udc->ep0_state) {
	case DATA_STATE_XMIT:
		done(ep, req, 0);
		/* receive status phase */
		if (ep0_prime_status(udc, USB_DIR_OUT))
			qe_ep0_stall(udc);
		break;

	case DATA_STATE_NEED_ZLP:
		done(ep, req, 0);
		udc->ep0_state = WAIT_FOR_SETUP;
		break;

	case DATA_STATE_RECV:
		done(ep, req, 0);
		/* send status phase */
		if (ep0_prime_status(udc, USB_DIR_IN))
			qe_ep0_stall(udc);
		break;

	case WAIT_FOR_OUT_STATUS:
		done(ep, req, 0);
		udc->ep0_state = WAIT_FOR_SETUP;
		break;

	case WAIT_FOR_SETUP:
		dev_vdbg(udc->dev, "Unexpected interrupt\n");
		break;

	default:
		qe_ep0_stall(udc);
		break;
	}
}

static int ep0_txcomplete(struct qe_ep *ep, unsigned char restart)
{
	struct qe_req *tx_req = NULL;
	struct qe_frame *frame = ep->txframe;

	if ((frame_get_info(frame) & (ZLP | NO_REQ)) == (ZLP | NO_REQ)) {
		if (!restart)
			ep->udc->ep0_state = WAIT_FOR_SETUP;
		else
			sendnulldata(ep, ep->txframe, SETUP_STATUS | NO_REQ);
		return 0;
	}

	tx_req = ep->tx_req;
	if (tx_req != NULL) {
		if (!restart) {
			int asent = ep->last;
			ep->sent += asent;
			ep->last -= asent;
		} else {
			ep->last = 0;
		}

		/* a request already were transmitted completely */
		if ((ep->tx_req->req.length - ep->sent) <= 0) {
			ep->tx_req->req.actual = (unsigned int)ep->sent;
			ep0_req_complete(ep->udc, ep->tx_req);
			ep->tx_req = NULL;
			ep->last = 0;
			ep->sent = 0;
		}
	} else {
		dev_vdbg(ep->udc->dev, "the ep0_controller have no req\n");
	}

	return 0;
}

static int ep0_txframe_handle(struct qe_ep *ep)
{
	/* if have error, transmit again */
	if (frame_get_status(ep->txframe) & FRAME_ERROR) {
		qe_ep_flushtxfifo(ep);
		dev_vdbg(ep->udc->dev, "The EP0 transmit data have error!\n");
		if (frame_get_info(ep->txframe) & PID_DATA0)
			ep->data01 = 0;
		else
			ep->data01 = 1;

		ep0_txcomplete(ep, 1);
	} else
		ep0_txcomplete(ep, 0);

	frame_create_tx(ep, ep->txframe);
	return 0;
}

static int qe_ep0_txconf(struct qe_ep *ep)
{
	struct qe_bd __iomem *bd;
	struct qe_frame *pframe;
	u32 bdstatus;

	bd = ep->c_txbd;
	bdstatus = in_be32((u32 __iomem *)bd);
	while (!(bdstatus & T_R) && (bdstatus & ~T_W)) {
		pframe = ep->txframe;

		/* clear and recycle the BD */
		out_be32((u32 __iomem *)bd, bdstatus & T_W);
		out_be32(&bd->buf, 0);
		if (bdstatus & T_W)
			ep->c_txbd = ep->txbase;
		else
			ep->c_txbd++;

		if (ep->c_txbd == ep->n_txbd) {
			if (bdstatus & DEVICE_T_ERROR) {
				frame_set_status(pframe, FRAME_ERROR);
				if (bdstatus & T_TO)
					pframe->status |= TX_ER_TIMEOUT;
				if (bdstatus & T_UN)
					pframe->status |= TX_ER_UNDERUN;
			}
			ep0_txframe_handle(ep);
		}

		bd = ep->c_txbd;
		bdstatus = in_be32((u32 __iomem *)bd);
	}

	return 0;
}

static int ep_txframe_handle(struct qe_ep *ep)
{
	if (frame_get_status(ep->txframe) & FRAME_ERROR) {
		qe_ep_flushtxfifo(ep);
		dev_vdbg(ep->udc->dev, "The EP0 transmit data have error!\n");
		if (frame_get_info(ep->txframe) & PID_DATA0)
			ep->data01 = 0;
		else
			ep->data01 = 1;

		txcomplete(ep, 1);
	} else
		txcomplete(ep, 0);

	frame_create_tx(ep, ep->txframe); /* send the data */
	return 0;
}

/* confirm the already trainsmited bd */
static int qe_ep_txconf(struct qe_ep *ep)
{
	struct qe_bd __iomem *bd;
	struct qe_frame *pframe = NULL;
	u32 bdstatus;
	unsigned char breakonrxinterrupt = 0;

	bd = ep->c_txbd;
	bdstatus = in_be32((u32 __iomem *)bd);
	while (!(bdstatus & T_R) && (bdstatus & ~T_W)) {
		pframe = ep->txframe;
		if (bdstatus & DEVICE_T_ERROR) {
			frame_set_status(pframe, FRAME_ERROR);
			if (bdstatus & T_TO)
				pframe->status |= TX_ER_TIMEOUT;
			if (bdstatus & T_UN)
				pframe->status |= TX_ER_UNDERUN;
		}

		/* clear and recycle the BD */
		out_be32((u32 __iomem *)bd, bdstatus & T_W);
		out_be32(&bd->buf, 0);
		if (bdstatus & T_W)
			ep->c_txbd = ep->txbase;
		else
			ep->c_txbd++;

		/* handle the tx frame */
		ep_txframe_handle(ep);
		bd = ep->c_txbd;
		bdstatus = in_be32((u32 __iomem *)bd);
	}
	if (breakonrxinterrupt)
		return -EIO;
	else
		return 0;
}

/* Add a request in queue, and try to transmit a packet */
static int ep_req_send(struct qe_ep *ep, struct qe_req *req)
{
	int reval = 0;

	if (ep->tx_req == NULL) {
		ep->sent = 0;
		ep->last = 0;
		txcomplete(ep, 0); /* can gain a new tx_req */
		reval = frame_create_tx(ep, ep->txframe);
	}
	return reval;
}

/* Maybe this is a good ideal */
static int ep_req_rx(struct qe_ep *ep, struct qe_req *req)
{
	struct qe_udc *udc = ep->udc;
	struct qe_frame *pframe = NULL;
	struct qe_bd __iomem *bd;
	u32 bdstatus, length;
	u32 vaddr, fsize;
	u8 *cp;
	u8 finish_req = 0;
	u8 framepid;

	if (list_empty(&ep->queue)) {
		dev_vdbg(udc->dev, "the req already finish!\n");
		return 0;
	}
	pframe = ep->rxframe;

	bd = ep->n_rxbd;
	bdstatus = in_be32((u32 __iomem *)bd);
	length = bdstatus & BD_LENGTH_MASK;

	while (!(bdstatus & R_E) && length) {
		if (finish_req)
			break;
		if ((bdstatus & R_F) && (bdstatus & R_L)
					&& !(bdstatus & R_ERROR)) {
			qe_frame_clean(pframe);
			vaddr = (u32)phys_to_virt(in_be32(&bd->buf));
			frame_set_data(pframe, (u8 *)vaddr);
			frame_set_length(pframe, (length - USB_CRC_SIZE));
			frame_set_status(pframe, FRAME_OK);
			switch (bdstatus & R_PID) {
			case R_PID_DATA1:
				frame_set_info(pframe, PID_DATA1); break;
			default:
				frame_set_info(pframe, PID_DATA0); break;
			}
			/* handle the rx frame */

			if (frame_get_info(pframe) & PID_DATA1)
				framepid = 0x1;
			else
				framepid = 0;

			if (framepid != ep->data01) {
				dev_vdbg(udc->dev, "the data01 error!\n");
			} else {
				fsize = frame_get_length(pframe);

				cp = (u8 *)(req->req.buf) + req->req.actual;
				if (cp) {
					memcpy(cp, pframe->data, fsize);
					req->req.actual += fsize;
					if ((fsize < ep->ep.maxpacket)
						|| (req->req.actual >=
							req->req.length)) {
						finish_req = 1;
						done(ep, req, 0);
						if (list_empty(&ep->queue))
							qe_eprx_nack(ep);
					}
				}
				qe_ep_toggledata01(ep);
			}
		} else {
			dev_err(udc->dev, "The receive frame with error!\n");
		}

		/* note: don't clear the rxbd's buffer address *
		 * only Clear the length */
		out_be32((u32 __iomem *)bd, (bdstatus & BD_STATUS_MASK));
		ep->has_data--;

		/* Get next BD */
		if (bdstatus & R_W)
			bd = ep->rxbase;
		else
			bd++;

		bdstatus = in_be32((u32 __iomem *)bd);
		length = bdstatus & BD_LENGTH_MASK;
	}

	ep->n_rxbd = bd;
	ep_recycle_rxbds(ep);

	return 0;
}

/* only add the request in queue */
static int ep_req_receive(struct qe_ep *ep, struct qe_req *req)
{
	if (ep->state == EP_STATE_NACK) {
		if (ep->has_data <= 0) {
			/* Enable rx and unmask rx interrupt */
			qe_eprx_normal(ep);
		} else {
			/* Copy the exist BD data */
			ep_req_rx(ep, req);
		}
	}

	return 0;
}

/********************************************************************
	Internal Used Function End
********************************************************************/

/*-----------------------------------------------------------------------
	Endpoint Management Functions For Gadget
 -----------------------------------------------------------------------*/
static int qe_ep_enable(struct usb_ep *_ep,
			 const struct usb_endpoint_descriptor *desc)
{
	struct qe_udc *udc;
	struct qe_ep *ep;
	int retval = 0;
	unsigned char epnum;

	ep = container_of(_ep, struct qe_ep, ep);

	/* catch various bogus parameters */
	if (!_ep || !desc || _ep->name == ep_name[0] ||
			(desc->bDescriptorType != USB_DT_ENDPOINT))
		return -EINVAL;

	udc = ep->udc;
	if (!udc->driver || (udc->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;

	epnum = (u8)desc->bEndpointAddress & 0xF;

	retval = qe_ep_init(udc, epnum, desc);
	if (retval != 0) {
		cpm_muram_free(cpm_muram_offset(ep->rxbase));
		dev_dbg(udc->dev, "enable ep%d failed\n", ep->epnum);
		return -EINVAL;
	}
	dev_dbg(udc->dev, "enable ep%d successful\n", ep->epnum);
	return 0;
}

static int qe_ep_disable(struct usb_ep *_ep)
{
	struct qe_udc *udc;
	struct qe_ep *ep;
	unsigned long flags;
	unsigned int size;

	ep = container_of(_ep, struct qe_ep, ep);
	udc = ep->udc;

	if (!_ep || !ep->ep.desc) {
		dev_dbg(udc->dev, "%s not enabled\n", _ep ? ep->ep.name : NULL);
		return -EINVAL;
	}

	spin_lock_irqsave(&udc->lock, flags);
	/* Nuke all pending requests (does flush) */
	nuke(ep, -ESHUTDOWN);
	ep->ep.desc = NULL;
	ep->stopped = 1;
	ep->tx_req = NULL;
	qe_ep_reset(udc, ep->epnum);
	spin_unlock_irqrestore(&udc->lock, flags);

	cpm_muram_free(cpm_muram_offset(ep->rxbase));

	if (ep->dir == USB_DIR_OUT)
		size = (ep->ep.maxpacket + USB_CRC_SIZE + 2) *
				(USB_BDRING_LEN_RX + 1);
	else
		size = (ep->ep.maxpacket + USB_CRC_SIZE + 2) *
				(USB_BDRING_LEN + 1);

	if (ep->dir != USB_DIR_IN) {
		kfree(ep->rxframe);
		if (ep->rxbufmap) {
			dma_unmap_single(udc->gadget.dev.parent,
					ep->rxbuf_d, size,
					DMA_FROM_DEVICE);
			ep->rxbuf_d = DMA_ADDR_INVALID;
		} else {
			dma_sync_single_for_cpu(
					udc->gadget.dev.parent,
					ep->rxbuf_d, size,
					DMA_FROM_DEVICE);
		}
		kfree(ep->rxbuffer);
	}

	if (ep->dir != USB_DIR_OUT)
		kfree(ep->txframe);

	dev_dbg(udc->dev, "disabled %s OK\n", _ep->name);
	return 0;
}

static struct usb_request *qe_alloc_request(struct usb_ep *_ep,	gfp_t gfp_flags)
{
	struct qe_req *req;

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	req->req.dma = DMA_ADDR_INVALID;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void qe_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct qe_req *req;

	req = container_of(_req, struct qe_req, req);

	if (_req)
		kfree(req);
}

static int __qe_ep_queue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct qe_ep *ep = container_of(_ep, struct qe_ep, ep);
	struct qe_req *req = container_of(_req, struct qe_req, req);
	struct qe_udc *udc;
	int reval;

	udc = ep->udc;
	/* catch various bogus parameters */
	if (!_req || !req->req.complete || !req->req.buf
			|| !list_empty(&req->queue)) {
		dev_dbg(udc->dev, "bad params\n");
		return -EINVAL;
	}
	if (!_ep || (!ep->ep.desc && ep_index(ep))) {
		dev_dbg(udc->dev, "bad ep\n");
		return -EINVAL;
	}

	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	req->ep = ep;

	/* map virtual address to hardware */
	if (req->req.dma == DMA_ADDR_INVALID) {
		req->req.dma = dma_map_single(ep->udc->gadget.dev.parent,
					req->req.buf,
					req->req.length,
					ep_is_in(ep)
					? DMA_TO_DEVICE :
					DMA_FROM_DEVICE);
		req->mapped = 1;
	} else {
		dma_sync_single_for_device(ep->udc->gadget.dev.parent,
					req->req.dma, req->req.length,
					ep_is_in(ep)
					? DMA_TO_DEVICE :
					DMA_FROM_DEVICE);
		req->mapped = 0;
	}

	req->req.status = -EINPROGRESS;
	req->req.actual = 0;

	list_add_tail(&req->queue, &ep->queue);
	dev_vdbg(udc->dev, "gadget have request in %s! %d\n",
			ep->name, req->req.length);

	/* push the request to device */
	if (ep_is_in(ep))
		reval = ep_req_send(ep, req);

	/* EP0 */
	if (ep_index(ep) == 0 && req->req.length > 0) {
		if (ep_is_in(ep))
			udc->ep0_state = DATA_STATE_XMIT;
		else
			udc->ep0_state = DATA_STATE_RECV;
	}

	if (ep->dir == USB_DIR_OUT)
		reval = ep_req_receive(ep, req);

	return 0;
}

/* queues (submits) an I/O request to an endpoint */
static int qe_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
		       gfp_t gfp_flags)
{
	struct qe_ep *ep = container_of(_ep, struct qe_ep, ep);
	struct qe_udc *udc = ep->udc;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&udc->lock, flags);
	ret = __qe_ep_queue(_ep, _req);
	spin_unlock_irqrestore(&udc->lock, flags);
	return ret;
}

/* dequeues (cancels, unlinks) an I/O request from an endpoint */
static int qe_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct qe_ep *ep = container_of(_ep, struct qe_ep, ep);
	struct qe_req *req = NULL;
	struct qe_req *iter;
	unsigned long flags;

	if (!_ep || !_req)
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(iter, &ep->queue, queue) {
		if (&iter->req != _req)
			continue;
		req = iter;
		break;
	}

	if (!req) {
		spin_unlock_irqrestore(&ep->udc->lock, flags);
		return -EINVAL;
	}

	done(ep, req, -ECONNRESET);

	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return 0;
}

/*-----------------------------------------------------------------
 * modify the endpoint halt feature
 * @ep: the non-isochronous endpoint being stalled
 * @value: 1--set halt  0--clear halt
 * Returns zero, or a negative error code.
*----------------------------------------------------------------*/
static int qe_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct qe_ep *ep;
	unsigned long flags;
	int status = -EOPNOTSUPP;
	struct qe_udc *udc;

	ep = container_of(_ep, struct qe_ep, ep);
	if (!_ep || !ep->ep.desc) {
		status = -EINVAL;
		goto out;
	}

	udc = ep->udc;
	/* Attempt to halt IN ep will fail if any transfer requests
	 * are still queue */
	if (value && ep_is_in(ep) && !list_empty(&ep->queue)) {
		status = -EAGAIN;
		goto out;
	}

	status = 0;
	spin_lock_irqsave(&ep->udc->lock, flags);
	qe_eptx_stall_change(ep, value);
	qe_eprx_stall_change(ep, value);
	spin_unlock_irqrestore(&ep->udc->lock, flags);

	if (ep->epnum == 0) {
		udc->ep0_state = WAIT_FOR_SETUP;
		udc->ep0_dir = 0;
	}

	/* set data toggle to DATA0 on clear halt */
	if (value == 0)
		ep->data01 = 0;
out:
	dev_vdbg(udc->dev, "%s %s halt stat %d\n", ep->ep.name,
			value ?  "set" : "clear", status);

	return status;
}

static const struct usb_ep_ops qe_ep_ops = {
	.enable = qe_ep_enable,
	.disable = qe_ep_disable,

	.alloc_request = qe_alloc_request,
	.free_request = qe_free_request,

	.queue = qe_ep_queue,
	.dequeue = qe_ep_dequeue,

	.set_halt = qe_ep_set_halt,
};

/*------------------------------------------------------------------------
	Gadget Driver Layer Operations
 ------------------------------------------------------------------------*/

/* Get the current frame number */
static int qe_get_frame(struct usb_gadget *gadget)
{
	struct qe_udc *udc = container_of(gadget, struct qe_udc, gadget);
	u16 tmp;

	tmp = in_be16(&udc->usb_param->frame_n);
	if (tmp & 0x8000)
		return tmp & 0x07ff;
	return -EINVAL;
}

static int fsl_qe_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver);
static int fsl_qe_stop(struct usb_gadget *gadget);

/* defined in usb_gadget.h */
static const struct usb_gadget_ops qe_gadget_ops = {
	.get_frame = qe_get_frame,
	.udc_start = fsl_qe_start,
	.udc_stop = fsl_qe_stop,
};

/*-------------------------------------------------------------------------
	USB ep0 Setup process in BUS Enumeration
 -------------------------------------------------------------------------*/
static int udc_reset_ep_queue(struct qe_udc *udc, u8 pipe)
{
	struct qe_ep *ep = &udc->eps[pipe];

	nuke(ep, -ECONNRESET);
	ep->tx_req = NULL;
	return 0;
}

static int reset_queues(struct qe_udc *udc)
{
	u8 pipe;

	for (pipe = 0; pipe < USB_MAX_ENDPOINTS; pipe++)
		udc_reset_ep_queue(udc, pipe);

	/* report disconnect; the driver is already quiesced */
	spin_unlock(&udc->lock);
	usb_gadget_udc_reset(&udc->gadget, udc->driver);
	spin_lock(&udc->lock);

	return 0;
}

static void ch9setaddress(struct qe_udc *udc, u16 value, u16 index,
			u16 length)
{
	/* Save the new address to device struct */
	udc->device_address = (u8) value;
	/* Update usb state */
	udc->usb_state = USB_STATE_ADDRESS;

	/* Status phase , send a ZLP */
	if (ep0_prime_status(udc, USB_DIR_IN))
		qe_ep0_stall(udc);
}

static void ownercomplete(struct usb_ep *_ep, struct usb_request *_req)
{
	struct qe_req *req = container_of(_req, struct qe_req, req);

	req->req.buf = NULL;
	kfree(req);
}

static void ch9getstatus(struct qe_udc *udc, u8 request_type, u16 value,
			u16 index, u16 length)
{
	u16 usb_status = 0;
	struct qe_req *req;
	struct qe_ep *ep;
	int status = 0;

	ep = &udc->eps[0];
	if ((request_type & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
		/* Get device status */
		usb_status = 1 << USB_DEVICE_SELF_POWERED;
	} else if ((request_type & USB_RECIP_MASK) == USB_RECIP_INTERFACE) {
		/* Get interface status */
		/* We don't have interface information in udc driver */
		usb_status = 0;
	} else if ((request_type & USB_RECIP_MASK) == USB_RECIP_ENDPOINT) {
		/* Get endpoint status */
		int pipe = index & USB_ENDPOINT_NUMBER_MASK;
		if (pipe >= USB_MAX_ENDPOINTS)
			goto stall;
		struct qe_ep *target_ep = &udc->eps[pipe];
		u16 usep;

		/* stall if endpoint doesn't exist */
		if (!target_ep->ep.desc)
			goto stall;

		usep = in_be16(&udc->usb_regs->usb_usep[pipe]);
		if (index & USB_DIR_IN) {
			if (target_ep->dir != USB_DIR_IN)
				goto stall;
			if ((usep & USB_THS_MASK) == USB_THS_STALL)
				usb_status = 1 << USB_ENDPOINT_HALT;
		} else {
			if (target_ep->dir != USB_DIR_OUT)
				goto stall;
			if ((usep & USB_RHS_MASK) == USB_RHS_STALL)
				usb_status = 1 << USB_ENDPOINT_HALT;
		}
	}

	req = container_of(qe_alloc_request(&ep->ep, GFP_KERNEL),
					struct qe_req, req);
	req->req.length = 2;
	req->req.buf = udc->statusbuf;
	*(u16 *)req->req.buf = cpu_to_le16(usb_status);
	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->req.complete = ownercomplete;

	udc->ep0_dir = USB_DIR_IN;

	/* data phase */
	status = __qe_ep_queue(&ep->ep, &req->req);

	if (status == 0)
		return;
stall:
	dev_err(udc->dev, "Can't respond to getstatus request \n");
	qe_ep0_stall(udc);
}

/* only handle the setup request, suppose the device in normal status */
static void setup_received_handle(struct qe_udc *udc,
				struct usb_ctrlrequest *setup)
{
	/* Fix Endian (udc->local_setup_buff is cpu Endian now)*/
	u16 wValue = le16_to_cpu(setup->wValue);
	u16 wIndex = le16_to_cpu(setup->wIndex);
	u16 wLength = le16_to_cpu(setup->wLength);

	/* clear the previous request in the ep0 */
	udc_reset_ep_queue(udc, 0);

	if (setup->bRequestType & USB_DIR_IN)
		udc->ep0_dir = USB_DIR_IN;
	else
		udc->ep0_dir = USB_DIR_OUT;

	switch (setup->bRequest) {
	case USB_REQ_GET_STATUS:
		/* Data+Status phase form udc */
		if ((setup->bRequestType & (USB_DIR_IN | USB_TYPE_MASK))
					!= (USB_DIR_IN | USB_TYPE_STANDARD))
			break;
		ch9getstatus(udc, setup->bRequestType, wValue, wIndex,
					wLength);
		return;

	case USB_REQ_SET_ADDRESS:
		/* Status phase from udc */
		if (setup->bRequestType != (USB_DIR_OUT | USB_TYPE_STANDARD |
						USB_RECIP_DEVICE))
			break;
		ch9setaddress(udc, wValue, wIndex, wLength);
		return;

	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		/* Requests with no data phase, status phase from udc */
		if ((setup->bRequestType & USB_TYPE_MASK)
					!= USB_TYPE_STANDARD)
			break;

		if ((setup->bRequestType & USB_RECIP_MASK)
				== USB_RECIP_ENDPOINT) {
			int pipe = wIndex & USB_ENDPOINT_NUMBER_MASK;
			struct qe_ep *ep;

			if (wValue != 0 || wLength != 0
				|| pipe >= USB_MAX_ENDPOINTS)
				break;
			ep = &udc->eps[pipe];

			spin_unlock(&udc->lock);
			qe_ep_set_halt(&ep->ep,
					(setup->bRequest == USB_REQ_SET_FEATURE)
						? 1 : 0);
			spin_lock(&udc->lock);
		}

		ep0_prime_status(udc, USB_DIR_IN);

		return;

	default:
		break;
	}

	if (wLength) {
		/* Data phase from gadget, status phase from udc */
		if (setup->bRequestType & USB_DIR_IN) {
			udc->ep0_state = DATA_STATE_XMIT;
			udc->ep0_dir = USB_DIR_IN;
		} else {
			udc->ep0_state = DATA_STATE_RECV;
			udc->ep0_dir = USB_DIR_OUT;
		}
		spin_unlock(&udc->lock);
		if (udc->driver->setup(&udc->gadget,
					&udc->local_setup_buff) < 0)
			qe_ep0_stall(udc);
		spin_lock(&udc->lock);
	} else {
		/* No data phase, IN status from gadget */
		udc->ep0_dir = USB_DIR_IN;
		spin_unlock(&udc->lock);
		if (udc->driver->setup(&udc->gadget,
					&udc->local_setup_buff) < 0)
			qe_ep0_stall(udc);
		spin_lock(&udc->lock);
		udc->ep0_state = DATA_STATE_NEED_ZLP;
	}
}

/*-------------------------------------------------------------------------
	USB Interrupt handlers
 -------------------------------------------------------------------------*/
static void suspend_irq(struct qe_udc *udc)
{
	udc->resume_state = udc->usb_state;
	udc->usb_state = USB_STATE_SUSPENDED;

	/* report suspend to the driver ,serial.c not support this*/
	if (udc->driver->suspend)
		udc->driver->suspend(&udc->gadget);
}

static void resume_irq(struct qe_udc *udc)
{
	udc->usb_state = udc->resume_state;
	udc->resume_state = 0;

	/* report resume to the driver , serial.c not support this*/
	if (udc->driver->resume)
		udc->driver->resume(&udc->gadget);
}

static void idle_irq(struct qe_udc *udc)
{
	u8 usbs;

	usbs = in_8(&udc->usb_regs->usb_usbs);
	if (usbs & USB_IDLE_STATUS_MASK) {
		if ((udc->usb_state) != USB_STATE_SUSPENDED)
			suspend_irq(udc);
	} else {
		if (udc->usb_state == USB_STATE_SUSPENDED)
			resume_irq(udc);
	}
}

static int reset_irq(struct qe_udc *udc)
{
	unsigned char i;

	if (udc->usb_state == USB_STATE_DEFAULT)
		return 0;

	qe_usb_disable(udc);
	out_8(&udc->usb_regs->usb_usadr, 0);

	for (i = 0; i < USB_MAX_ENDPOINTS; i++) {
		if (udc->eps[i].init)
			qe_ep_reset(udc, i);
	}

	reset_queues(udc);
	udc->usb_state = USB_STATE_DEFAULT;
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->ep0_dir = USB_DIR_OUT;
	qe_usb_enable(udc);
	return 0;
}

static int bsy_irq(struct qe_udc *udc)
{
	return 0;
}

static int txe_irq(struct qe_udc *udc)
{
	return 0;
}

/* ep0 tx interrupt also in here */
static int tx_irq(struct qe_udc *udc)
{
	struct qe_ep *ep;
	struct qe_bd __iomem *bd;
	int i, res = 0;

	if ((udc->usb_state == USB_STATE_ADDRESS)
		&& (in_8(&udc->usb_regs->usb_usadr) == 0))
		out_8(&udc->usb_regs->usb_usadr, udc->device_address);

	for (i = (USB_MAX_ENDPOINTS-1); ((i >= 0) && (res == 0)); i--) {
		ep = &udc->eps[i];
		if (ep && ep->init && (ep->dir != USB_DIR_OUT)) {
			bd = ep->c_txbd;
			if (!(in_be32((u32 __iomem *)bd) & T_R)
						&& (in_be32(&bd->buf))) {
				/* confirm the transmitted bd */
				if (ep->epnum == 0)
					res = qe_ep0_txconf(ep);
				else
					res = qe_ep_txconf(ep);
			}
		}
	}
	return res;
}


/* setup packect's rx is handle in the function too */
static void rx_irq(struct qe_udc *udc)
{
	struct qe_ep *ep;
	struct qe_bd __iomem *bd;
	int i;

	for (i = 0; i < USB_MAX_ENDPOINTS; i++) {
		ep = &udc->eps[i];
		if (ep && ep->init && (ep->dir != USB_DIR_IN)) {
			bd = ep->n_rxbd;
			if (!(in_be32((u32 __iomem *)bd) & R_E)
						&& (in_be32(&bd->buf))) {
				if (ep->epnum == 0) {
					qe_ep0_rx(udc);
				} else {
					/*non-setup package receive*/
					qe_ep_rx(ep);
				}
			}
		}
	}
}

static irqreturn_t qe_udc_irq(int irq, void *_udc)
{
	struct qe_udc *udc = (struct qe_udc *)_udc;
	u16 irq_src;
	irqreturn_t status = IRQ_NONE;
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	irq_src = in_be16(&udc->usb_regs->usb_usber) &
		in_be16(&udc->usb_regs->usb_usbmr);
	/* Clear notification bits */
	out_be16(&udc->usb_regs->usb_usber, irq_src);
	/* USB Interrupt */
	if (irq_src & USB_E_IDLE_MASK) {
		idle_irq(udc);
		irq_src &= ~USB_E_IDLE_MASK;
		status = IRQ_HANDLED;
	}

	if (irq_src & USB_E_TXB_MASK) {
		tx_irq(udc);
		irq_src &= ~USB_E_TXB_MASK;
		status = IRQ_HANDLED;
	}

	if (irq_src & USB_E_RXB_MASK) {
		rx_irq(udc);
		irq_src &= ~USB_E_RXB_MASK;
		status = IRQ_HANDLED;
	}

	if (irq_src & USB_E_RESET_MASK) {
		reset_irq(udc);
		irq_src &= ~USB_E_RESET_MASK;
		status = IRQ_HANDLED;
	}

	if (irq_src & USB_E_BSY_MASK) {
		bsy_irq(udc);
		irq_src &= ~USB_E_BSY_MASK;
		status = IRQ_HANDLED;
	}

	if (irq_src & USB_E_TXE_MASK) {
		txe_irq(udc);
		irq_src &= ~USB_E_TXE_MASK;
		status = IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return status;
}

/*-------------------------------------------------------------------------
	Gadget driver probe and unregister.
 --------------------------------------------------------------------------*/
static int fsl_qe_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct qe_udc *udc;
	unsigned long flags;

	udc = container_of(gadget, struct qe_udc, gadget);
	/* lock is needed but whether should use this lock or another */
	spin_lock_irqsave(&udc->lock, flags);

	/* hook up the driver */
	udc->driver = driver;
	udc->gadget.speed = driver->max_speed;

	/* Enable IRQ reg and Set usbcmd reg EN bit */
	qe_usb_enable(udc);

	out_be16(&udc->usb_regs->usb_usber, 0xffff);
	out_be16(&udc->usb_regs->usb_usbmr, USB_E_DEFAULT_DEVICE);
	udc->usb_state = USB_STATE_ATTACHED;
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->ep0_dir = USB_DIR_OUT;
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int fsl_qe_stop(struct usb_gadget *gadget)
{
	struct qe_udc *udc;
	struct qe_ep *loop_ep;
	unsigned long flags;

	udc = container_of(gadget, struct qe_udc, gadget);
	/* stop usb controller, disable intr */
	qe_usb_disable(udc);

	/* in fact, no needed */
	udc->usb_state = USB_STATE_ATTACHED;
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->ep0_dir = 0;

	/* stand operation */
	spin_lock_irqsave(&udc->lock, flags);
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	nuke(&udc->eps[0], -ESHUTDOWN);
	list_for_each_entry(loop_ep, &udc->gadget.ep_list, ep.ep_list)
		nuke(loop_ep, -ESHUTDOWN);
	spin_unlock_irqrestore(&udc->lock, flags);

	udc->driver = NULL;

	return 0;
}

/* udc structure's alloc and setup, include ep-param alloc */
static struct qe_udc *qe_udc_config(struct platform_device *ofdev)
{
	struct qe_udc *udc;
	struct device_node *np = ofdev->dev.of_node;
	unsigned long tmp_addr = 0;
	struct usb_device_para __iomem *usbpram;
	unsigned int i;
	u64 size;
	u32 offset;

	udc = kzalloc(sizeof(*udc), GFP_KERNEL);
	if (!udc)
		goto cleanup;

	udc->dev = &ofdev->dev;

	/* get default address of usb parameter in MURAM from device tree */
	offset = *of_get_address(np, 1, &size, NULL);
	udc->usb_param = cpm_muram_addr(offset);
	memset_io(udc->usb_param, 0, size);

	usbpram = udc->usb_param;
	out_be16(&usbpram->frame_n, 0);
	out_be32(&usbpram->rstate, 0);

	tmp_addr = cpm_muram_alloc((USB_MAX_ENDPOINTS *
					sizeof(struct usb_ep_para)),
					   USB_EP_PARA_ALIGNMENT);
	if (IS_ERR_VALUE(tmp_addr))
		goto cleanup;

	for (i = 0; i < USB_MAX_ENDPOINTS; i++) {
		out_be16(&usbpram->epptr[i], (u16)tmp_addr);
		udc->ep_param[i] = cpm_muram_addr(tmp_addr);
		tmp_addr += 32;
	}

	memset_io(udc->ep_param[0], 0,
			USB_MAX_ENDPOINTS * sizeof(struct usb_ep_para));

	udc->resume_state = USB_STATE_NOTATTACHED;
	udc->usb_state = USB_STATE_POWERED;
	udc->ep0_dir = 0;

	spin_lock_init(&udc->lock);
	return udc;

cleanup:
	kfree(udc);
	return NULL;
}

/* USB Controller register init */
static int qe_udc_reg_init(struct qe_udc *udc)
{
	struct usb_ctlr __iomem *qe_usbregs;
	qe_usbregs = udc->usb_regs;

	/* Spec says that we must enable the USB controller to change mode. */
	out_8(&qe_usbregs->usb_usmod, 0x01);
	/* Mode changed, now disable it, since muram isn't initialized yet. */
	out_8(&qe_usbregs->usb_usmod, 0x00);

	/* Initialize the rest. */
	out_be16(&qe_usbregs->usb_usbmr, 0);
	out_8(&qe_usbregs->usb_uscom, 0);
	out_be16(&qe_usbregs->usb_usber, USBER_ALL_CLEAR);

	return 0;
}

static int qe_ep_config(struct qe_udc *udc, unsigned char pipe_num)
{
	struct qe_ep *ep = &udc->eps[pipe_num];

	ep->udc = udc;
	strcpy(ep->name, ep_name[pipe_num]);
	ep->ep.name = ep_name[pipe_num];

	if (pipe_num == 0) {
		ep->ep.caps.type_control = true;
	} else {
		ep->ep.caps.type_iso = true;
		ep->ep.caps.type_bulk = true;
		ep->ep.caps.type_int = true;
	}

	ep->ep.caps.dir_in = true;
	ep->ep.caps.dir_out = true;

	ep->ep.ops = &qe_ep_ops;
	ep->stopped = 1;
	usb_ep_set_maxpacket_limit(&ep->ep, (unsigned short) ~0);
	ep->ep.desc = NULL;
	ep->dir = 0xff;
	ep->epnum = (u8)pipe_num;
	ep->sent = 0;
	ep->last = 0;
	ep->init = 0;
	ep->rxframe = NULL;
	ep->txframe = NULL;
	ep->tx_req = NULL;
	ep->state = EP_STATE_IDLE;
	ep->has_data = 0;

	/* the queue lists any req for this ep */
	INIT_LIST_HEAD(&ep->queue);

	/* gagdet.ep_list used for ep_autoconfig so no ep0*/
	if (pipe_num != 0)
		list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);

	ep->gadget = &udc->gadget;

	return 0;
}

/*-----------------------------------------------------------------------
 *	UDC device Driver operation functions				*
 *----------------------------------------------------------------------*/
static void qe_udc_release(struct device *dev)
{
	struct qe_udc *udc = container_of(dev, struct qe_udc, gadget.dev);
	int i;

	complete(udc->done);
	cpm_muram_free(cpm_muram_offset(udc->ep_param[0]));
	for (i = 0; i < USB_MAX_ENDPOINTS; i++)
		udc->ep_param[i] = NULL;

	kfree(udc);
}

/* Driver probe functions */
static const struct of_device_id qe_udc_match[];
static int qe_udc_probe(struct platform_device *ofdev)
{
	struct qe_udc *udc;
	const struct of_device_id *match;
	struct device_node *np = ofdev->dev.of_node;
	struct qe_ep *ep;
	unsigned int ret = 0;
	unsigned int i;
	const void *prop;

	match = of_match_device(qe_udc_match, &ofdev->dev);
	if (!match)
		return -EINVAL;

	prop = of_get_property(np, "mode", NULL);
	if (!prop || strcmp(prop, "peripheral"))
		return -ENODEV;

	/* Initialize the udc structure including QH member and other member */
	udc = qe_udc_config(ofdev);
	if (!udc) {
		dev_err(&ofdev->dev, "failed to initialize\n");
		return -ENOMEM;
	}

	udc->soc_type = (unsigned long)match->data;
	udc->usb_regs = of_iomap(np, 0);
	if (!udc->usb_regs) {
		ret = -ENOMEM;
		goto err1;
	}

	/* initialize usb hw reg except for regs for EP,
	 * leave usbintr reg untouched*/
	qe_udc_reg_init(udc);

	/* here comes the stand operations for probe
	 * set the qe_udc->gadget.xxx */
	udc->gadget.ops = &qe_gadget_ops;

	/* gadget.ep0 is a pointer */
	udc->gadget.ep0 = &udc->eps[0].ep;

	INIT_LIST_HEAD(&udc->gadget.ep_list);

	/* modify in register gadget process */
	udc->gadget.speed = USB_SPEED_UNKNOWN;

	/* name: Identifies the controller hardware type. */
	udc->gadget.name = driver_name;
	udc->gadget.dev.parent = &ofdev->dev;

	/* initialize qe_ep struct */
	for (i = 0; i < USB_MAX_ENDPOINTS ; i++) {
		/* because the ep type isn't decide here so
		 * qe_ep_init() should be called in ep_enable() */

		/* setup the qe_ep struct and link ep.ep.list
		 * into gadget.ep_list */
		qe_ep_config(udc, (unsigned char)i);
	}

	/* ep0 initialization in here */
	ret = qe_ep_init(udc, 0, &qe_ep0_desc);
	if (ret)
		goto err2;

	/* create a buf for ZLP send, need to remain zeroed */
	udc->nullbuf = devm_kzalloc(&ofdev->dev, 256, GFP_KERNEL);
	if (udc->nullbuf == NULL) {
		ret = -ENOMEM;
		goto err3;
	}

	/* buffer for data of get_status request */
	udc->statusbuf = devm_kzalloc(&ofdev->dev, 2, GFP_KERNEL);
	if (udc->statusbuf == NULL) {
		ret = -ENOMEM;
		goto err3;
	}

	udc->nullp = virt_to_phys((void *)udc->nullbuf);
	if (udc->nullp == DMA_ADDR_INVALID) {
		udc->nullp = dma_map_single(
					udc->gadget.dev.parent,
					udc->nullbuf,
					256,
					DMA_TO_DEVICE);
		udc->nullmap = 1;
	} else {
		dma_sync_single_for_device(udc->gadget.dev.parent,
					udc->nullp, 256,
					DMA_TO_DEVICE);
	}

	tasklet_setup(&udc->rx_tasklet, ep_rx_tasklet);
	/* request irq and disable DR  */
	udc->usb_irq = irq_of_parse_and_map(np, 0);
	if (!udc->usb_irq) {
		ret = -EINVAL;
		goto err_noirq;
	}

	ret = request_irq(udc->usb_irq, qe_udc_irq, 0,
				driver_name, udc);
	if (ret) {
		dev_err(udc->dev, "cannot request irq %d err %d\n",
				udc->usb_irq, ret);
		goto err4;
	}

	ret = usb_add_gadget_udc_release(&ofdev->dev, &udc->gadget,
			qe_udc_release);
	if (ret)
		goto err5;

	platform_set_drvdata(ofdev, udc);
	dev_info(udc->dev,
			"%s USB controller initialized as device\n",
			(udc->soc_type == PORT_QE) ? "QE" : "CPM");
	return 0;

err5:
	free_irq(udc->usb_irq, udc);
err4:
	irq_dispose_mapping(udc->usb_irq);
err_noirq:
	if (udc->nullmap) {
		dma_unmap_single(udc->gadget.dev.parent,
			udc->nullp, 256,
				DMA_TO_DEVICE);
			udc->nullp = DMA_ADDR_INVALID;
	} else {
		dma_sync_single_for_cpu(udc->gadget.dev.parent,
			udc->nullp, 256,
				DMA_TO_DEVICE);
	}
err3:
	ep = &udc->eps[0];
	cpm_muram_free(cpm_muram_offset(ep->rxbase));
	kfree(ep->rxframe);
	kfree(ep->rxbuffer);
	kfree(ep->txframe);
err2:
	iounmap(udc->usb_regs);
err1:
	kfree(udc);
	return ret;
}

#ifdef CONFIG_PM
static int qe_udc_suspend(struct platform_device *dev, pm_message_t state)
{
	return -ENOTSUPP;
}

static int qe_udc_resume(struct platform_device *dev)
{
	return -ENOTSUPP;
}
#endif

static void qe_udc_remove(struct platform_device *ofdev)
{
	struct qe_udc *udc = platform_get_drvdata(ofdev);
	struct qe_ep *ep;
	unsigned int size;
	DECLARE_COMPLETION_ONSTACK(done);

	usb_del_gadget_udc(&udc->gadget);

	udc->done = &done;
	tasklet_disable(&udc->rx_tasklet);

	if (udc->nullmap) {
		dma_unmap_single(udc->gadget.dev.parent,
			udc->nullp, 256,
				DMA_TO_DEVICE);
			udc->nullp = DMA_ADDR_INVALID;
	} else {
		dma_sync_single_for_cpu(udc->gadget.dev.parent,
			udc->nullp, 256,
				DMA_TO_DEVICE);
	}

	ep = &udc->eps[0];
	cpm_muram_free(cpm_muram_offset(ep->rxbase));
	size = (ep->ep.maxpacket + USB_CRC_SIZE + 2) * (USB_BDRING_LEN + 1);

	kfree(ep->rxframe);
	if (ep->rxbufmap) {
		dma_unmap_single(udc->gadget.dev.parent,
				ep->rxbuf_d, size,
				DMA_FROM_DEVICE);
		ep->rxbuf_d = DMA_ADDR_INVALID;
	} else {
		dma_sync_single_for_cpu(udc->gadget.dev.parent,
				ep->rxbuf_d, size,
				DMA_FROM_DEVICE);
	}

	kfree(ep->rxbuffer);
	kfree(ep->txframe);

	free_irq(udc->usb_irq, udc);
	irq_dispose_mapping(udc->usb_irq);

	tasklet_kill(&udc->rx_tasklet);

	iounmap(udc->usb_regs);

	/* wait for release() of gadget.dev to free udc */
	wait_for_completion(&done);
}

/*-------------------------------------------------------------------------*/
static const struct of_device_id qe_udc_match[] = {
	{
		.compatible = "fsl,mpc8323-qe-usb",
		.data = (void *)PORT_QE,
	},
	{
		.compatible = "fsl,mpc8360-qe-usb",
		.data = (void *)PORT_QE,
	},
	{
		.compatible = "fsl,mpc8272-cpm-usb",
		.data = (void *)PORT_CPM,
	},
	{},
};

MODULE_DEVICE_TABLE(of, qe_udc_match);

static struct platform_driver udc_driver = {
	.driver = {
		.name = driver_name,
		.of_match_table = qe_udc_match,
	},
	.probe          = qe_udc_probe,
	.remove_new     = qe_udc_remove,
#ifdef CONFIG_PM
	.suspend        = qe_udc_suspend,
	.resume         = qe_udc_resume,
#endif
};

module_platform_driver(udc_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
