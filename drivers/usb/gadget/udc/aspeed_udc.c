// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2021 Aspeed Technology Inc.
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
#include <linux/prefetch.h>
#include <linux/clk.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/dma-mapping.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>

static unsigned int desc_mode;

module_param(desc_mode, uint, 0644);
MODULE_PARM_DESC(desc_mode, "ASPEED descriptor mode (off=0 / on=1)");

#define AST_DESC_MODE_HANLE_OVER_1024	1
#define EP_DMA_SIZE			3072

/*************************************************************************************/
#define ASPEED_UDC_FUN_CTRL		0x00
#define USB_PHY_CLK_EN			BIT(31)

#define USB_FIFO_DYN_PWRD_EN		BIT(19)
#define USB_EP_LONG_DESC		BIT(18)
#define USB_ISO_IN_NULL_RESP		BIT(17)

#define USB_BIST_TEST_PASS		BIT(13)
#define USB_BIST_TURN_ON		BIT(12)
#define USB_PHY_RESET_DIS		BIT(11)
/* */
#define USB_TEST_MODE(x)		((x) << 8)
#define USB_FORCE_TIMER_HS		BIT(7)
#define USB_FORCE_HS			BIT(6)
#define USB_REMOTE_WAKEUP_12MS		BIT(5)
#define USB_REMOTE_WAKEUP_EN		BIT(4)
#define USB_AUTO_REMOTE_WAKEUP_EN	BIT(3)
#define USB_STOP_CLK_IN_SUPEND		BIT(2)
#define USB_UPSTREAM_FS			BIT(1)
#define USB_UPSTREAM_EN			BIT(0)

#define ASPEED_UDC_CONFIG		0x04
#define USB_GET_DMA_STS(x)		((x >> 16) & 0xff)
#define USB_GET_DEV_ADDR(x)		(x & 0x7f)

#define ASPEED_UDC_IER			0x08
#define ASPEED_UDC_ISR			0x0C
#define USBR_RX_FIFO_EMPTY_ISR		BIT(31)
/* */
#define USB_PEP_POOL_NAK_ISR		BIT(17)
#define USB_PEP_POOL_ACK_STALL_ISR	BIT(16)
/* */
#define USB_SUSPEND_RESUME_ISR		BIT(8)
#define USB_SUSPEND_ISR			BIT(7)
#define USB_BUS_RESET_ISR		BIT(6)
/* */
#define USB_EP0_IN_DATA_NAK_ISR		BIT(4)
#define USB_EP0_IN_ACK_STALL_ISR	BIT(3)
#define USB_EP0_OUT_NAK_ISR		BIT(2)
#define USB_EP0_OUT_ACK_STALL_ISR	BIT(1)
#define USB_EP0_SETUP_ISR		BIT(0)

#define ASPEED_UDC_EP_ACK_IER		0x10
#define ASPEED_UDC_EP_NAK_IER		0x14
#define ASPEED_UDC_EP_ACK_ISR		0x18
#define ASPEED_UDC_EP_NAK_ISR		0x1C
#define USB_EP3_ISR			BIT(3)
#define USB_EP2_ISR			BIT(2)
#define USB_EP1_ISR			BIT(1)
#define USB_EP0_ISR			BIT(0)

#define ASPEED_UDC_DEV_RESET		0x20
#define DEV5_SOFT_RESET			BIT(5)
#define DEV4_SOFT_RESET			BIT(4)
#define DEV3_SOFT_RESET			BIT(3)
#define DEV2_SOFT_RESET			BIT(2)
#define DEV1_SOFT_RESET			BIT(1)
#define ROOT_HUB_SOFT_RESET		BIT(0)

#define ASPEED_UDC_STS			0x24
#define AST_VHUB_EP_DATA		0x28
#define AST_VHUB_ISO_TX_FAIL		0x2C
#define AST_VHUB_EP0_CTRL		0x30
#define EP0_GET_RX_LEN(x)		((x >> 16) & 0x7f)
#define EP0_TX_LEN(x)			((x & 0x7f) << 8)
#define EP0_RX_BUFF_RDY			BIT(2)
#define EP0_TX_BUFF_RDY			BIT(1)
#define EP0_STALL			BIT(0)

#define AST_VHUB_EP0_DATA_BUFF		0x34

/*************************************************************************************/
#define ASPEED_UDC_EP_CONFIG		0x00
#define EP_SET_MAX_PKT(x)		((x & 0x3ff) << 16)
#define EP_DATA_FETCH_CTRL(x)		((x & 0x3) << 14)
#define EP_AUTO_DATA_DISABLE		(0x1 << 13)
#define EP_SET_EP_STALL			(0x1 << 12)

#define EP_SET_EP_NUM(x)		((x & 0xf) << 8)

#define EP_SET_TYPE_MASK(x)		((x) << 4)
#define EP_TYPE_BULK_IN			(0x2 << 4)
#define EP_TYPE_BULK_OUT		(0x3 << 4)
#define EP_TYPE_INT_IN			(0x4 << 4)
#define EP_TYPE_INT_OUT			(0x5 << 4)
#define EP_TYPE_ISO_IN			(0x6 << 4)
#define EP_TYPE_ISO_OUT			(0x7 << 4)
#define EP_ALLOCATED_MASK		(0x7 << 1)
#define EP_ENABLE			BIT(0)

#define ASPEED_UDC_EP_DMA_CTRL		0x04
#define EP_SINGLE_DMA_MODE		(0x1 << 1)

#define AST_NUM_EP_DMA_DESC		32

#define AST_EP_DMA_DESC_INTR_ENABLE	BIT(31)
#define AST_EP_DMA_DESC_PID_DATA0	(0 << 14)
#define AST_EP_DMA_DESC_PID_DATA2	BIT(14)
#define AST_EP_DMA_DESC_PID_DATA1	(2 << 14)
#define AST_EP_DMA_DESC_PID_MDATA	(3 << 14)

#define ASPEED_UDC_EP_DMA_BUFF		0x08
#define ASPEED_UDC_EP_DMA_STS		0x0C

/*************************************************************************************/

struct aspeed_udc_request {
	struct usb_request req;
	struct list_head queue;
	unsigned mapped:1;
	unsigned int actual_dma_length;
	u32 saved_dma_wptr;
};
struct ast_dma_desc {
	u32		des_0;
	u32		des_1;
};
struct aspeed_udc_ep {
	struct usb_ep			ep;

	/* Request queue */
	struct list_head		queue;

	struct aspeed_udc		*udc;
	void __iomem			*ep_reg;
	unsigned			stopped:1;
	u8				ep_dir;
	void				*ep_buf;
	dma_addr_t			ep_dma;
	const struct usb_endpoint_descriptor	*desc;
	struct ast_dma_desc		*dma_desc_list;
	dma_addr_t			dma_desc_dma_handle;
	u32				dma_desc_list_wptr;
	u32				data_toggle;
};

#define AST_NUM_ENDPOINTS		(4 + 1)
/*
 * driver is non-SMP, and just blocks IRQs whenever it needs
 * access protection for chip registers or driver state
 */
struct aspeed_udc {
	struct platform_device	*pdev;
	void __iomem		*reg;
	int			irq;
	spinlock_t		lock;

	struct clk		*clk;

	/* EP0 DMA buffers allocated in one chunk */
	void			*ep0_ctrl_buf;
	dma_addr_t		ep0_ctrl_dma;

	struct aspeed_udc_ep	ep[AST_NUM_ENDPOINTS];

	struct usb_gadget	gadget;


	struct usb_gadget_driver	*driver;
	unsigned			suspended:1;
	unsigned			req_pending:1;
	unsigned			wait_for_addr_ack:1;
	unsigned			wait_for_config_ack:1;
	unsigned			active_suspend:1;
	unsigned			is_udc_control_tx:1;
	u8				addr;

	struct proc_dir_entry	*pde;
	struct usb_ctrlrequest *root_setup;



	enum usb_device_state	suspended_from;
	int			desc_mode;
    /* Force full speed only */
	bool			force_usb1 : 1;

};

static const char * const ast_ep_name[] = {
	"ep0", "ep1", "ep2", "ep3", "ep4"
};

#define AST_UDC_DEBUG
//#define AST_BUS_DEBUG
//#define AST_SETUP_DEBUG
//#define AST_EP_DEBUG
//#define AST_ISR_DEBUG

#ifdef AST_BUS_DEBUG
#define BUS_DBG(fmt, args...) pr_info("%s() " fmt, __func__, ## args)
#else
#define BUS_DBG(fmt, args...)
#endif

#ifdef AST_SETUP_DEBUG
#define SETUP_DBG(fmt, args...) pr_info("%s() " fmt, __func__, ## args)
#else
#define SETUP_DBG(fmt, args...)
#endif

#ifdef AST_EP_DEBUG
#define EP_DBG(fmt, args...) pr_info("%s() " fmt, __func__, ## args)

#else
#define EP_DBG(fmt, args...)
#endif

#ifdef AST_UDC_DEBUG
#define UDC_DBG(fmt, args...) pr_info("%s() " fmt, __func__, ## args)
#else
#define UDC_DBG(fmt, args...)
#endif

#ifdef AST_ISR_DEBUG
#define ISR_DBG(fmt, args...) pr_info("%s() " fmt, __func__, ## args)
#else
#define ISR_DBG(fmt, args...)
#endif

/*-------------------------------------------------------------------------*/
#define ast_udc_read(udc, offset) \
	__raw_readl((udc)->reg + (offset))
#define ast_udc_write(udc, val, offset) \
	__raw_writel((val), (udc)->reg + (offset))

#define ast_ep_read(ep, reg) \
	__raw_readl((ep)->ep_reg + (reg))
#define ast_ep_write(ep, val, reg) \
	__raw_writel((val), (ep)->ep_reg + (reg))

/*-------------------------------------------------------------------------*/

static void aspeed_udc_done(struct aspeed_udc_ep *ep, struct aspeed_udc_request *req, int status)
{
	struct aspeed_udc *udc = ep->udc;

	EP_DBG("%s len (%d/%d) buf %x, dir %x\n",
		ep->ep.name, req->req.actual, req->req.length, req->req.buf, ep->ep_dir);

	list_del(&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	if (status && status != -ESHUTDOWN)
		EP_DBG("%s done %p, status %d\n", ep->ep.name, req, status);

	spin_unlock(&udc->lock);
	usb_gadget_giveback_request(&ep->ep, &req->req);
	spin_lock(&udc->lock);
}

static void aspeed_udc_nuke(struct aspeed_udc_ep *ep, int status)
{
	/* Sanity check */
	if (&ep->queue == NULL)
		return;

	while (!list_empty(&ep->queue)) {
		struct aspeed_udc_request *req;

		req = list_entry(ep->queue.next, struct aspeed_udc_request,
				queue);
		aspeed_udc_done(ep, req, status);
	}
}

/**
 * Stop activity on all endpoints.
 * Device controller for which EP activity is to be stopped.
 *
 * All the endpoints are stopped and any pending transfer requests if any on
 * the endpoint are terminated.
 */
static void aspeed_udc_stop_activity(struct aspeed_udc *udc)
{
	int epnum = 0;
	struct aspeed_udc_ep *ep;

	for (epnum = 0; epnum < AST_NUM_ENDPOINTS; epnum++) {
		ep = &udc->ep[epnum];
		ep->stopped = 1;
		aspeed_udc_nuke(ep, -ESHUTDOWN);
	}
}

/*-------------------------------------------------------------------------*/

static int aspeed_udc_ep_enable(struct usb_ep *_ep,
				const struct usb_endpoint_descriptor *desc)
{
	struct aspeed_udc_ep *ep = container_of(_ep, struct aspeed_udc_ep, ep);
	struct aspeed_udc *udc = ep->udc;
	u16 maxpacket = usb_endpoint_maxp(desc) & 0x7ff;
	u16 nr_trans = ((usb_endpoint_maxp(desc) >> 11) & 3) + 1;
	u8 epnum = usb_endpoint_num(desc);
	unsigned long flags;
	u32 ep_conf = 0;
	u8 type;
	u8 dir_in;

	EP_DBG("%s, set ep #%d, maxpacket %d ,wmax %d trans:%d\n",
		ep->ep.name, epnum, maxpacket, le16_to_cpu(desc->wMaxPacketSize), nr_trans);

	if (!_ep || !ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT) {
		pr_info("bad ep or descriptor %s %d , maxpacket %d, ep maxpacket %d\n",
			_ep->name, desc->bDescriptorType, maxpacket, ep->ep.maxpacket);
		return -EINVAL;
	}

	if (!udc->driver) {
		pr_info("bogus device state\n");
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&udc->lock, flags);

	ep->desc = desc;
	ep->stopped = 0;
	ep->ep.maxpacket = maxpacket;
	ep->ep.mult = nr_trans - 1;

	if (maxpacket > 1024) {
		pr_info("TODO check size\n");
		maxpacket = 1024;
	}

	if (maxpacket == 1024)
		ep_conf = 0;
	else
		ep_conf = EP_SET_MAX_PKT(maxpacket);

	ep_conf |= EP_SET_EP_NUM(epnum);

	if (udc->desc_mode) {
		if (nr_trans > 1) {
			ep_conf |= EP_DATA_FETCH_CTRL((nr_trans-1));
			ep_conf |= EP_AUTO_DATA_DISABLE;
		} else
			ep_conf |= EP_AUTO_DATA_DISABLE;
	}

	type = usb_endpoint_type(desc);
	dir_in = usb_endpoint_dir_in(desc);
	ep->ep_dir = dir_in;

	EP_DBG("epnum %d, type %d, dir_in %d\n", epnum, type, dir_in);
	switch (type) {
	case USB_ENDPOINT_XFER_ISOC:
		if (dir_in)
			ep_conf |= EP_TYPE_ISO_IN;
		else
			ep_conf |= EP_TYPE_ISO_OUT;
		break;

	case USB_ENDPOINT_XFER_BULK:
		if (dir_in)
			ep_conf |= EP_TYPE_BULK_IN;
		else
			ep_conf |= EP_TYPE_BULK_OUT;
		break;

	case USB_ENDPOINT_XFER_INT:
		if (dir_in)
			ep_conf |= EP_TYPE_INT_IN;
		else
			ep_conf |= EP_TYPE_INT_OUT;
		break;
	}

	if (udc->desc_mode) {
		ast_ep_write(ep, 0x4, ASPEED_UDC_EP_DMA_CTRL);
		ast_ep_write(ep, 0, ASPEED_UDC_EP_DMA_STS);
		ast_ep_write(ep, ep->dma_desc_dma_handle, ASPEED_UDC_EP_DMA_BUFF);
		ast_ep_write(ep, 0x1, ASPEED_UDC_EP_DMA_CTRL);
		/* must clear the write pointer otherwise re-open streaming failed. */
		ep->dma_desc_list_wptr = 0;
		/* Set to DATA1 so that 1st packet will be initialize as DATA0 after toggle */
		ep->data_toggle	= AST_EP_DMA_DESC_PID_DATA1;

	} else {
		ast_ep_write(ep, 0x4, ASPEED_UDC_EP_DMA_CTRL);
		ast_ep_write(ep, 0x2, ASPEED_UDC_EP_DMA_CTRL);
		ast_ep_write(ep, 0, ASPEED_UDC_EP_DMA_STS);
	}
	ast_ep_write(ep, ep_conf | EP_ENABLE, ASPEED_UDC_EP_CONFIG);

	EP_DBG("read ep %d seting: 0x%08X\n", epnum, ast_ep_read(ep, ASPEED_UDC_EP_CONFIG));
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int aspeed_udc_ep_disable(struct usb_ep *_ep)
{
	struct aspeed_udc_ep *ep = container_of(_ep, struct aspeed_udc_ep, ep);
	struct aspeed_udc *udc = ep->udc;
	unsigned long flags;

	EP_DBG("%s\n", _ep->name);

	spin_lock_irqsave(&udc->lock, flags);

	ep->ep.desc = NULL;
	ep->stopped = 1;

	aspeed_udc_nuke(ep, -ESHUTDOWN);

	ast_ep_write(ep, 0, ASPEED_UDC_EP_CONFIG);
	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

static struct usb_request *
aspeed_udc_ep_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct aspeed_udc_request *req;

	EP_DBG("%s\n", _ep->name);
	req = kzalloc(sizeof(struct aspeed_udc_request), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);
	return &req->req;
}

static void aspeed_udc_ep_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct aspeed_udc_request *req;

	EP_DBG("%s\n", _ep->name);
	req = container_of(_req, struct aspeed_udc_request, req);
	kfree(req);
}
static dma_addr_t aspeed_dma_sg_addr(struct aspeed_udc_request *req, unsigned int offset)
{
	struct scatterlist *sg = req->req.sg;
	struct scatterlist *s;
	unsigned int	length;
	dma_addr_t	dma;
	int		i;

	EP_DBG("offset = %d\n", offset);

	for_each_sg(sg, s, req->req.num_sgs, i) {
		length = sg_dma_len(s);
		dma = sg_dma_address(s);

		EP_DBG("(i=%d / %d) 0x%08X %d\n", i, req->req.num_sgs, dma, length, offset);
		if (length > offset) {
			dma += offset;
			EP_DBG("(a) dma = 0x%08X offset=%d\n", dma, offset);
			goto out;

		} else {
			offset -= length;
			dma += offset;
			EP_DBG("(b) dma = 0x%08X offset=%d\n", dma, offset);
		}

		if (sg_is_last(s))
			EP_DBG("%s\n", "no more sg list\n");
	}
out:
	EP_DBG("return dma address = 0x%08x\n", dma);
	return dma;
}

static int aspeed_dma_descriptor_setup(struct aspeed_udc_ep *ep,
	unsigned int dma_address, u16 tx_len, struct aspeed_udc_request *req)
{
	u64 packet_size_new;
	u64 maxpacket_to_set;
	int i;
	unsigned int offset;

	if (!ep->dma_desc_list) {
		EP_DBG("%s %s\n", ep->ep.name, "failed due to empty DMA descriptor list");
		return -1;
	}

	if (!ep->ep_dir) {
		EP_DBG("%s %s\n", ep->ep.name, "DMA descriptor list temp. not support RX (OUT)");
		return -2;
	}

	if (req->req.num_sgs)
		offset = dma_address;

	packet_size_new = tx_len;
	maxpacket_to_set = ep->ep.maxpacket;
	i = 0;

	while (packet_size_new > 0) {
		EP_DBG("(%d)dma_address = 0x%08X, packet_size_new = %llu (tx_len = %u)\n",
				ep->dma_desc_list_wptr, dma_address, packet_size_new, tx_len);

		if (req->req.num_sgs) {
			offset = i * ep->ep.maxpacket;
			ep->dma_desc_list[ep->dma_desc_list_wptr].des_0 = aspeed_dma_sg_addr(req, offset);
			EP_DBG("(%d)dma_address = 0x%08X, offset = %d\n",
				ep->dma_desc_list_wptr, ep->dma_desc_list[ep->dma_desc_list_wptr].des_0, offset);

		} else {
			ep->dma_desc_list[ep->dma_desc_list_wptr].des_0 = (dma_address + (i * ep->ep.maxpacket));
			EP_DBG("(%d)dma_address = 0x%08X, packet_size_new = %llu (tx_len = %u)\n",
				ep->dma_desc_list_wptr, dma_address, packet_size_new, tx_len);
		}

		if (usb_endpoint_type(ep->desc) == USB_ENDPOINT_XFER_ISOC) {
			if (packet_size_new <= ep->ep.maxpacket) {
				ep->dma_desc_list[ep->dma_desc_list_wptr].des_1 =
					AST_EP_DMA_DESC_PID_DATA0 | (packet_size_new);

			} else if (packet_size_new <= ep->ep.maxpacket * 2) {
				ep->dma_desc_list[ep->dma_desc_list_wptr].des_1 =
					AST_EP_DMA_DESC_PID_DATA1 | (maxpacket_to_set);

			} else if (packet_size_new <= ep->ep.maxpacket * 3) {
				ep->dma_desc_list[ep->dma_desc_list_wptr].des_1 =
					AST_EP_DMA_DESC_PID_DATA2 | (maxpacket_to_set);
			}
		} else {
			/* Bulk or Interrupt Transfer */

			/* DATA0 / DATA1 toggles */
			if (ep->data_toggle == AST_EP_DMA_DESC_PID_DATA0)
				ep->data_toggle = AST_EP_DMA_DESC_PID_DATA1;
			else
				ep->data_toggle = AST_EP_DMA_DESC_PID_DATA0;

			/* setup descriptor size and pid */
			if (packet_size_new <= ep->ep.maxpacket)
				ep->dma_desc_list[ep->dma_desc_list_wptr].des_1 =
					ep->data_toggle | (packet_size_new);
			else
				ep->dma_desc_list[ep->dma_desc_list_wptr].des_1 =
					ep->data_toggle | (maxpacket_to_set);
		}

		EP_DBG("(%d, %x) dma_desc_list 0x%x 0x%x\n", ep->dma_desc_list_wptr, req,
				ep->dma_desc_list[ep->dma_desc_list_wptr].des_0,
				ep->dma_desc_list[ep->dma_desc_list_wptr].des_1);

		if (i == 0)
			req->saved_dma_wptr = ep->dma_desc_list_wptr;

		ep->dma_desc_list_wptr++;
		i++;
		if (ep->dma_desc_list_wptr >= AST_NUM_EP_DMA_DESC)
			ep->dma_desc_list_wptr = 0;

		if (packet_size_new >= ep->ep.maxpacket)
			packet_size_new -= ep->ep.maxpacket;
		else
			break; // last data sent
	}

	if (req->req.zero) {
		EP_DBG("Send an extra zero length packet\n");

		ep->dma_desc_list[ep->dma_desc_list_wptr].des_0 = (dma_address + (i - 1) * ep->ep.maxpacket);
		if (usb_endpoint_type(ep->desc) == USB_ENDPOINT_XFER_ISOC)
			ep->dma_desc_list[ep->dma_desc_list_wptr].des_1 = AST_EP_DMA_DESC_PID_DATA0 | (0);
		else {
			/* DATA0 / DATA1 toggles */
			if (ep->data_toggle == AST_EP_DMA_DESC_PID_DATA0)
				ep->data_toggle = AST_EP_DMA_DESC_PID_DATA1;
			else
				ep->data_toggle = AST_EP_DMA_DESC_PID_DATA0;

			ep->dma_desc_list[ep->dma_desc_list_wptr].des_1 = ep->data_toggle | (0);
		}

		ep->dma_desc_list_wptr++;
		if (ep->dma_desc_list_wptr >= AST_NUM_EP_DMA_DESC)
			ep->dma_desc_list_wptr = 0;
	}

	return 0;
}
static void aspeed_udc_ep_dma(struct aspeed_udc_ep *ep, struct aspeed_udc_request *req)
{
	u16 tx_len;

	if ((req->req.length - req->req.actual) > ep->ep.maxpacket)
		tx_len = ep->ep.maxpacket;
	else
		tx_len = req->req.length - req->req.actual;

	if (tx_len > 1024)
		pr_info("*************************************************\n");

	EP_DBG("dma: %s : len : %d dir %x\n", ep->ep.name, tx_len, ep->ep_dir);

	if ((req->req.dma % 4) != 0) {
		if ((ep->ep_dir) && (!req->req.actual))
			memcpy(ep->ep_buf, req->req.buf, req->req.length);

		ast_ep_write(ep, ep->ep_dma + req->req.actual, ASPEED_UDC_EP_DMA_BUFF);
	} else
		ast_ep_write(ep, req->req.dma + req->req.actual, ASPEED_UDC_EP_DMA_BUFF);

	//trigger
	ast_ep_write(ep, tx_len << 16, ASPEED_UDC_EP_DMA_STS);
	ast_ep_write(ep, tx_len << 16 | 0x1, ASPEED_UDC_EP_DMA_STS);
}

static void aspeed_udc_ep_dma_desc_mode(struct aspeed_udc_ep *ep, struct aspeed_udc_request *req)
{
	u16 tx_len;
	u32 dma_sts;
	u32 req_size;

	req_size = ep->ep.maxpacket * max_t(unsigned int, ep->ep.maxburst, 1);
	req_size *= ep->ep.mult + 1;

	/* new one for sending more than 1024 bytes in ASPEED_UDC_EP_DMA_STS */
	if ((req->req.length - req->req.actual) > req_size)
		tx_len = req_size;
	else
		tx_len = req->req.length - req->req.actual;

	EP_DBG("dma: %s : sgs: %d tx_len : %d dir %x: dma %x [%d %d %d]\n",
		ep->ep.name, req->req.num_sgs, tx_len, ep->ep_dir, req->req.dma + req->req.actual,
		req->req.length, req->req.actual, req_size);

	if (req->req.num_sgs)
		req->req.dma = 0;

	if ((req->req.dma % 4) != 0) {
		pr_info("Not supported=> 1: %s : %x len (%d/%d) dir %x\n",
			ep->ep.name, req->req.dma, req->req.actual, req->req.length, ep->ep_dir);
	} else {
		//-----------------------
		ast_ep_write(ep, 0x4, ASPEED_UDC_EP_DMA_CTRL); //new
		if (!aspeed_dma_descriptor_setup(ep, req->req.dma + req->req.actual, tx_len, req))
			req->actual_dma_length += tx_len;

		ast_ep_write(ep, ep->dma_desc_dma_handle, ASPEED_UDC_EP_DMA_BUFF); //new
		ast_ep_write(ep, 0x1, ASPEED_UDC_EP_DMA_CTRL); //new

		dma_sts = ast_ep_read(ep, ASPEED_UDC_EP_DMA_STS);
		dma_sts &= 0x0000FF00;

		/* make sure CPU done everything before triggering USB DMA */
		mb();

		ast_ep_write(ep, dma_sts | (u8) ep->dma_desc_list_wptr, ASPEED_UDC_EP_DMA_STS);
	}
}

static void aspeed_udc_ep0_queue(struct aspeed_udc_ep *ep, struct aspeed_udc_request *req)
{
	struct aspeed_udc *udc = ep->udc;
	u16 tx_len;

	if ((req->req.length - req->req.actual) > ep->ep.maxpacket)
		tx_len = ep->ep.maxpacket;
	else
		tx_len = req->req.length - req->req.actual;

	ast_udc_write(udc, req->req.dma + req->req.actual, AST_VHUB_EP0_DATA_BUFF);

	if (ep->ep_dir) {
		SETUP_DBG("ep0 in addr buf %x, dma %x , txlen %d:(%d/%d) ,dir %d\n",
			(u32)req->req.buf, req->req.dma + req->req.actual, tx_len, req->req.actual, req->req.length, ep->ep_dir);
		req->req.actual += tx_len;

		ast_udc_write(udc, EP0_TX_LEN(tx_len), AST_VHUB_EP0_CTRL);
		ast_udc_write(udc, EP0_TX_LEN(tx_len) | EP0_TX_BUFF_RDY, AST_VHUB_EP0_CTRL);

	} else {
		SETUP_DBG("ep0 out ~~ addr buf %x, dma %x , (%d/%d) ,dir %d\n",
			(u32)req->req.buf, req->req.dma + req->req.actual, req->req.actual, req->req.length, ep->ep_dir);

		if (!req->req.length) {
			ast_udc_write(udc, EP0_TX_BUFF_RDY, AST_VHUB_EP0_CTRL);
			ep->ep_dir = 0x80;
		} else
			ast_udc_write(udc, EP0_RX_BUFF_RDY, AST_VHUB_EP0_CTRL);
	}
}

static int aspeed_udc_ep_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct aspeed_udc_request *req = container_of(_req, struct aspeed_udc_request, req);
	struct aspeed_udc_ep *ep = container_of(_ep, struct aspeed_udc_ep, ep);
	struct aspeed_udc *udc = ep->udc;
	unsigned long flags;

	if (unlikely(!_req || !_req->complete || !_req->buf || !_ep))
		return -EINVAL;

	if (ep->stopped) {
		pr_info("%s : is stop\n", _ep->name);
		return 1;
	}

	EP_DBG("%s: len: %d\n", _ep->name, _req->length);
/*
 *	if (req->req.zero && udc->desc_mode == 0)
 *		EP_DBG("[FIXME] Warning: Temp. don't support to send an extra zero length packet in single buffer mode\n");
 */
	spin_lock_irqsave(&udc->lock, flags);

	list_add_tail(&req->queue, &ep->queue);

	req->req.actual = 0;
	req->req.status = -EINPROGRESS;
	req->actual_dma_length = 0;

	if (usb_gadget_map_request(&udc->gadget, &req->req, ep->ep_dir)) {
		pr_info("map ERROR\n");
		return 1;
	}

	EP_DBG("%s : dma : %x (req %x :_req %x)\n", _ep->name, req->req.dma, req, _req);

	if (ep->ep.desc == NULL) {	/* ep0 */
		if ((req->req.dma % 4) != 0) {
			pr_info("EP0 dma error:%x\n", req->req.dma);
			return 1;
		}

		aspeed_udc_ep0_queue(ep, req);

	} else {
		if (list_is_singular(&ep->queue)) {
			if (udc->desc_mode)
				aspeed_udc_ep_dma_desc_mode(ep, req);
			else
				aspeed_udc_ep_dma(ep, req);
		}
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int aspeed_udc_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct aspeed_udc_ep *ep = container_of(_ep, struct aspeed_udc_ep, ep);
	struct aspeed_udc_request *req;
	unsigned long flags;
	struct aspeed_udc *udc = ep->udc;

	UDC_DBG("%s\n", _ep->name);

	if (!_ep || ep->ep.name == ast_ep_name[0])
		return -EINVAL;

	spin_lock_irqsave(&udc->lock, flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req) {
			list_del_init(&req->queue);
			_req->status = -ECONNRESET;
			break;
		}
	}

	if (&req->req != _req) {
		spin_unlock_irqrestore(&udc->lock, flags);
		return -EINVAL;
	}

	aspeed_udc_done(ep, req, -ESHUTDOWN);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int aspeed_udc_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct aspeed_udc_ep *ep = container_of(_ep, struct aspeed_udc_ep, ep);
	struct aspeed_udc *udc = ep->udc;
	unsigned long flags;

	UDC_DBG("%s\n", _ep->name);
	pr_info("%s : %d\n", _ep->name, value);
	if (!_ep)
		return -EINVAL;

	spin_lock_irqsave(&udc->lock, flags);

	if (_ep->name == ast_ep_name[0]) {
		if (value)
			ast_udc_write(udc, ast_udc_read(udc, AST_VHUB_EP0_CTRL) | EP0_STALL, AST_VHUB_EP0_CTRL);
		else
			ast_udc_write(udc, ast_udc_read(udc, AST_VHUB_EP0_CTRL) & ~EP0_STALL, AST_VHUB_EP0_CTRL);
	} else {
		if (value)
			ast_ep_write(ep, ast_ep_read(ep, ASPEED_UDC_EP_CONFIG) | EP_SET_EP_STALL, ASPEED_UDC_EP_CONFIG);
		else
			ast_ep_write(ep, ast_ep_read(ep, ASPEED_UDC_EP_CONFIG) & ~EP_SET_EP_STALL, ASPEED_UDC_EP_CONFIG);

		ep->stopped = value ? 1:0; //only non-ep0 is stopped and waiting for a clear
	}

	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

static const struct usb_ep_ops aspeed_udc_ep_ops = {
	.enable		= aspeed_udc_ep_enable,
	.disable	= aspeed_udc_ep_disable,
	.alloc_request	= aspeed_udc_ep_alloc_request,
	.free_request	= aspeed_udc_ep_free_request,
	.queue		= aspeed_udc_ep_queue,
	.dequeue	= aspeed_udc_ep_dequeue,
	.set_halt	= aspeed_udc_ep_set_halt,
	/* there's only imprecise fifo status reporting */
};

/*************************************************************************************************************************************************/
void aspeed_udc_ep0_rx(struct aspeed_udc *udc)
{
	SETUP_DBG("\n");

	ast_udc_write(udc, udc->ep0_ctrl_dma, AST_VHUB_EP0_DATA_BUFF);
	ast_udc_write(udc, EP0_RX_BUFF_RDY, AST_VHUB_EP0_CTRL);

}

void aspeed_udc_ep0_tx(struct aspeed_udc *udc)
{
	SETUP_DBG("\n");

	ast_udc_write(udc, udc->ep0_ctrl_dma, AST_VHUB_EP0_DATA_BUFF);
	ast_udc_write(udc, EP0_TX_BUFF_RDY, AST_VHUB_EP0_CTRL);

}

void aspeed_udc_ep0_out(struct aspeed_udc *udc)
{
	struct aspeed_udc_ep *ep = &udc->ep[0];
	struct aspeed_udc_request *req;
	u16 rx_len = EP0_GET_RX_LEN(ast_udc_read(udc, AST_VHUB_EP0_CTRL));
	u8 *buf;

	SETUP_DBG("\n");

	if (list_empty(&ep->queue))
		return;

	req = list_entry(ep->queue.next, struct aspeed_udc_request, queue);

	buf = req->req.buf;

	req->req.actual += rx_len;
	SETUP_DBG("req %x (%d/%d)\n", req, req->req.length, req->req.actual);
	if ((rx_len < ep->ep.maxpacket) || (req->req.actual == req->req.length)) {
		//[UVC Gadget] aspeed_udc_ep0_tx will "always" cause a zero length IN (Status OK) for SET_CUR
		//This is not our case. So, gadget driver will response in DATA Stage by itself
		aspeed_udc_ep0_tx(udc);
		if (!ep->ep_dir)
			aspeed_udc_done(ep, req, 0);

	} else {
		if (rx_len > req->req.length) {
			//Issue Fix
			pr_info("Issue Fix (%d/%d)\n", req->req.actual, req->req.length);
			aspeed_udc_ep0_tx(udc);
			aspeed_udc_done(ep, req, 0);
			return;
		}

		ep->ep_dir = 0;
		aspeed_udc_ep0_queue(ep, req);
	}
}

void aspeed_udc_ep0_in(struct aspeed_udc *udc)
{
	struct aspeed_udc_ep *ep = &udc->ep[0];
	struct aspeed_udc_request *req;

	SETUP_DBG("\n");

	if (list_empty(&ep->queue)) {
		if (udc->is_udc_control_tx) {
			SETUP_DBG("is_udc_control_tx\n");
			aspeed_udc_ep0_rx(udc);
			udc->is_udc_control_tx = 0;
		}

		return;
	}

	req = list_entry(ep->queue.next, struct aspeed_udc_request, queue);

	SETUP_DBG("req=%x (%d/%d)\n", req, req->req.length, req->req.actual);

	if (req->req.length == req->req.actual) {
		//If SET_CUR IN/Status stage, the request is zero. This is the end of command. Don't don't to further trigger OUT trans.
		if (req->req.length)
			aspeed_udc_ep0_rx(udc);

		if (ep->ep_dir)
			aspeed_udc_done(ep, req, 0);

	} else
		aspeed_udc_ep0_queue(ep, req);
}

void aspeed_udc_ep_handle(struct aspeed_udc *udc, u16 ep_num)
{
	struct aspeed_udc_ep *ep = &udc->ep[ep_num];
	struct aspeed_udc_request *req;
	u16 len = 0;

	if (list_empty(&ep->queue))
		return;

	req = list_first_entry(&ep->queue, struct aspeed_udc_request, queue);

	len = (ast_ep_read(ep, ASPEED_UDC_EP_DMA_STS) >> 16) & 0x7ff;

	req->req.actual += len;

	if (req->req.length == req->req.actual) {
		usb_gadget_unmap_request(&udc->gadget, &req->req, ep->ep_dir);
		if ((req->req.dma % 4) != 0) {
			if (!ep->ep_dir) {
				prefetchw(req->req.buf);
				memcpy(req->req.buf, ep->ep_buf, req->req.actual);
			}
		}

		aspeed_udc_done(ep, req, 0);
		if (!list_empty(&ep->queue)) {
			req = list_first_entry(&ep->queue, struct aspeed_udc_request, queue);
			aspeed_udc_ep_dma(ep, req);
		}
	} else {
		if (len < ep->ep.maxpacket) {
			usb_gadget_unmap_request(&udc->gadget, &req->req, ep->ep_dir);
			if ((req->req.dma % 4) != 0) {
				if (!ep->ep_dir)
					memcpy(req->req.buf, ep->ep_buf, req->req.actual);
			}

			aspeed_udc_done(ep, req, 0);
			if (!list_empty(&ep->queue)) {
				req = list_first_entry(&ep->queue, struct aspeed_udc_request, queue);
				aspeed_udc_ep_dma(ep, req);
			}
		} else
			aspeed_udc_ep_dma(ep, req);
	}
}
void aspeed_udc_ep_handle_desc_mode(struct aspeed_udc *udc, u16 ep_num)
{
	struct aspeed_udc_ep *ep = &udc->ep[ep_num];
	struct aspeed_udc_request *req;
	u16 len = 0;
	u32 processing_status = 0;
	u32 wr_ptr = 0;
	u32 rd_ptr1 = 0;
	u32 rd_ptr2 = 0;
	u32 index = 0;
	u16 len_in_desc = 0;
	u16 total_len = 0;
	int i;

	if (list_empty(&ep->queue)) {
		EP_DBG("%s handle reqest empty!!!\n", ep->ep.name);
		return;
	}

	req = list_first_entry(&ep->queue, struct aspeed_udc_request, queue);

	EP_DBG("ASPEED_UDC_EP_DMA_CTRL: 0x%08X\n", ast_ep_read(ep, ASPEED_UDC_EP_DMA_CTRL));
	processing_status = (ast_ep_read(ep, ASPEED_UDC_EP_DMA_CTRL) >> 4) & 15;

	if (processing_status != 0 && processing_status != 8) {
		pr_info("PS (%d): ASPEED_UDC_EP_DMA_CTRL: 0x%08X\n",
			processing_status, ast_ep_read(ep, ASPEED_UDC_EP_DMA_CTRL));
		return;
	}

	wr_ptr = (ast_ep_read(ep, ASPEED_UDC_EP_DMA_STS)) & 0xFF;
	rd_ptr1 = (ast_ep_read(ep, ASPEED_UDC_EP_DMA_STS) >> 8) & 0xFF;
	rd_ptr2 = (ast_ep_read(ep, ASPEED_UDC_EP_DMA_STS) >> 8) & 0xFF;
	if (rd_ptr1 != rd_ptr2)
		pr_info("rd_ptr not sync %d %d\n", rd_ptr1, rd_ptr2);

	if (rd_ptr2 != wr_ptr) {
		pr_info("desc not empty? %d %d\n", rd_ptr2, wr_ptr);
		return;
	}

	len = (ast_ep_read(ep, ASPEED_UDC_EP_DMA_STS) >> 16) & 2047;
	if (len > 1024)
		EP_DBG("strange len %d\n", len);

	if (wr_ptr == 0)
		index = AST_NUM_EP_DMA_DESC - 1;
	else
		index = wr_ptr - 1;

#if (AST_DESC_MODE_HANLE_OVER_1024 == 0)
	//only max. 1024 bytes
	len_in_desc = (u16)(ep->dma_desc_list[req->saved_dma_wptr].des_1 & 2047);

	//Broken image if this following happens
	if (len != len_in_desc) {
		pr_info(" Size incorrect? => %d %d %d %d[%x]\n", len, len_in_desc, req->req.length,
			req->saved_dma_wptr, ast_ep_read(ep, ASPEED_UDC_EP_DMA_STS));
	}
#else
	//for max. over 1024 bytes
	i = req->saved_dma_wptr;
	do {
		len_in_desc =  (u16)(ep->dma_desc_list[i].des_1 & 2047);
		total_len += len_in_desc;
		i++;
		if (i >= AST_NUM_EP_DMA_DESC)
			i = 0;

	} while (i != wr_ptr);

	len = total_len;
#endif

	req->req.actual += len;

	EP_DBG(" %s : actual %d len=%d\n", ep->ep.name, req->req.actual, len);

	if (req->req.length <= req->req.actual) {
		usb_gadget_unmap_request(&udc->gadget, &req->req, ep->ep_dir);
		if ((req->req.dma % 4) != 0) {
			pr_info("Not supported in desc_mode\n");
			return;
		}

		aspeed_udc_done(ep, req, 0);
		if (!list_empty(&ep->queue)) {
			req = list_first_entry(&ep->queue, struct aspeed_udc_request, queue);
			EP_DBG("1. %s next req dma %x (req=%x)\n", ep->ep.name, req->req.dma, req);
			if (req->actual_dma_length == req->req.actual)
				aspeed_udc_ep_dma_desc_mode(ep, req);
			else
				EP_DBG("1. %s skip req %x dma (due to already setup ? %d %d)\n",
					ep->ep.name, req, req->actual_dma_length, req->req.actual);
		}
	} else {
		if (len < ep->ep.maxpacket) {
			usb_gadget_unmap_request(&udc->gadget, &req->req, ep->ep_dir);
			if ((req->req.dma % 4) != 0) {
				pr_info("Not supported in desc_mode\n");
				return;
			}

			aspeed_udc_done(ep, req, 0);
			if (!list_empty(&ep->queue)) {
				req = list_first_entry(&ep->queue, struct aspeed_udc_request, queue);

				EP_DBG("2. %s next req %x\n", ep->ep.name, req);
				if (req->actual_dma_length == req->req.actual)
					aspeed_udc_ep_dma_desc_mode(ep, req);
			}
		} else {
			//next
			if (req->actual_dma_length == req->req.actual)
				aspeed_udc_ep_dma_desc_mode(ep, req);
		}
	}

	EP_DBG("%s exits\n", ep->ep.name);
}

void ast_udc_ep0_data_tx(struct aspeed_udc *udc, u8 *tx_data, u32 len)
{
	if (len) {
		memcpy(udc->ep0_ctrl_buf, tx_data, len);
		ast_udc_write(udc, udc->ep0_ctrl_dma, AST_VHUB_EP0_DATA_BUFF);
		ast_udc_write(udc, EP0_TX_LEN(len), AST_VHUB_EP0_CTRL);
		ast_udc_write(udc, EP0_TX_LEN(len) | EP0_TX_BUFF_RDY, AST_VHUB_EP0_CTRL);
		udc->is_udc_control_tx = 1;

	} else {
		ast_udc_write(udc, EP0_TX_BUFF_RDY, AST_VHUB_EP0_CTRL);
	}
}

static void ast_udc_getstatus(struct aspeed_udc *udc)
{
	int epnum;
	u16 status = 0;

	switch (udc->root_setup->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		/* Get device status */
		status = 1 << USB_DEVICE_SELF_POWERED;
		break;
	case USB_RECIP_INTERFACE:
		break;
	case USB_RECIP_ENDPOINT:
		epnum = udc->root_setup->wIndex & USB_ENDPOINT_NUMBER_MASK;
		status = udc->ep[epnum].stopped;
		break;
	default:
		goto stall;
	}

	EP_DBG("status = %d\n", status);
	ast_udc_ep0_data_tx(udc, (u8 *)&status, sizeof(status));

	return;

stall:
	pr_info("Can't respond request\n");
	ast_udc_write(udc, ast_udc_read(udc, AST_VHUB_EP0_CTRL) | EP0_STALL, AST_VHUB_EP0_CTRL);
}

void aspeed_udc_setup_handle(struct aspeed_udc *udc)
{
	u16 ep_num = 0;
	struct aspeed_udc_request *req_traverse;
	int j = 0;

	SETUP_DBG("type : %x, req : %x, val : %x, idx: %x, len : %d\n",
		udc->root_setup->bRequestType,
		udc->root_setup->bRequest,
		udc->root_setup->wValue,
		udc->root_setup->wIndex,
		udc->root_setup->wLength);

	// Clear ep0 requests in queue because here means the new control setup already comes
	EP_DBG("Clear ep0 queue if needed:\n");
	list_for_each_entry(req_traverse, &udc->ep[0].queue, queue) {
		j++;
		EP_DBG("[%d] there is req %x in ep0 queue !\n", j, req_traverse);
	}
	aspeed_udc_nuke(&udc->ep[0], -ETIMEDOUT);

	udc->ep[0].ep_dir = udc->root_setup->bRequestType & USB_DIR_IN;

	if ((udc->root_setup->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (udc->root_setup->bRequest) {
		case USB_REQ_SET_ADDRESS:
			if (ast_udc_read(udc, ASPEED_UDC_STS) & (0x1 << 27))
				udc->gadget.speed = USB_SPEED_HIGH;
			else
				udc->gadget.speed = USB_SPEED_FULL;

			SETUP_DBG("set addr %x\n", udc->root_setup->wValue);
			ast_udc_write(udc, udc->root_setup->wValue, ASPEED_UDC_CONFIG);
			ast_udc_write(udc, EP0_TX_BUFF_RDY, AST_VHUB_EP0_CTRL);
			break;

		case USB_REQ_CLEAR_FEATURE:
			ep_num = udc->root_setup->wIndex & USB_ENDPOINT_NUMBER_MASK;
			EP_DBG("USB_REQ_CLEAR_FEATURE ep-%d\n", ep_num);
			ast_udc_write(udc, (ep_num - 1), AST_VHUB_EP_DATA);
			ast_ep_write(&udc->ep[ep_num], ast_ep_read(&udc->ep[ep_num], ASPEED_UDC_EP_CONFIG) & ~EP_SET_EP_STALL, ASPEED_UDC_EP_CONFIG);
			ast_udc_write(udc, EP0_TX_BUFF_RDY, AST_VHUB_EP0_CTRL);
			/* UVC bulk mode needs CLEAR_FEATURE to stop streaming. So, invoked gadget's setup function here, too  */
			spin_unlock(&udc->lock);
			udc->driver->setup(&udc->gadget, udc->root_setup);
			spin_lock(&udc->lock);
			break;

		case USB_REQ_SET_FEATURE:
			EP_DBG("USB_REQ_SET_FEATURE ep-%d\n", udc->root_setup->wIndex & USB_ENDPOINT_NUMBER_MASK);
			break;

		case USB_REQ_GET_STATUS:
			ast_udc_getstatus(udc);
			break;

		default:
			spin_unlock(&udc->lock);
			if (udc->driver->setup(&udc->gadget, udc->root_setup) < 0) {
				ast_udc_write(udc, ast_udc_read(udc, AST_VHUB_EP0_CTRL) | EP0_STALL, AST_VHUB_EP0_CTRL);
				pr_info("udc->root_setup->bRequest %d\n", udc->root_setup->bRequest);
			}
			spin_lock(&udc->lock);
		break;
		}

	} else {
		switch (udc->root_setup->bRequest) {
		default:
			spin_unlock(&udc->lock);
			if (udc->driver->setup(&udc->gadget, udc->root_setup) < 0) {
				ast_udc_write(udc, ast_udc_read(udc, AST_VHUB_EP0_CTRL) | EP0_STALL, AST_VHUB_EP0_CTRL);
				pr_info("CLASS udc->root_setup->bRequest %d\n", udc->root_setup->bRequest);
			}
			spin_lock(&udc->lock);
			break;
		}
	}
}

static irqreturn_t aspeed_udc_isr(int irq, void *data)
{
	struct aspeed_udc *udc = (struct aspeed_udc *)data;
	u32 isr = ast_udc_read(udc, ASPEED_UDC_ISR);
	u32 ep_isr = 0;
	int i = 0;

	spin_lock(&udc->lock);

	if (isr & USB_BUS_RESET_ISR) {
		BUS_DBG("USB_BUS_RESET_ISR\n");
		ast_udc_write(udc, USB_BUS_RESET_ISR, ASPEED_UDC_ISR);
		udc->gadget.speed = USB_SPEED_UNKNOWN;

		if (udc->driver && udc->driver->reset) {
			spin_unlock(&udc->lock);
			udc->driver->reset(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	if (isr & USB_SUSPEND_ISR) {
		//Suspend, we don't handle this in sample
		BUS_DBG("USB_SUSPEND_ISR\n");
		ast_udc_write(udc, USB_SUSPEND_ISR, ASPEED_UDC_ISR);
		udc->suspended_from = udc->gadget.state;
		usb_gadget_set_state(&udc->gadget, USB_STATE_SUSPENDED);

		if (udc->driver && udc->driver->suspend) {
			spin_unlock(&udc->lock);
			udc->driver->suspend(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	if (isr & USB_SUSPEND_RESUME_ISR) {
		//Suspend, we don't handle this in sample
		BUS_DBG("USB_SUSPEND_RESUME_ISR\n");
		ast_udc_write(udc, USB_SUSPEND_RESUME_ISR, ASPEED_UDC_ISR);
		usb_gadget_set_state(&udc->gadget, udc->suspended_from);

		if (udc->driver && udc->driver->resume) {
			spin_unlock(&udc->lock);
			udc->driver->resume(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	if (isr & USB_EP0_IN_ACK_STALL_ISR) {
		ISR_DBG("USB_EP0_IN_ACK_STALL_ISR\n");
		ast_udc_write(udc, USB_EP0_IN_ACK_STALL_ISR, ASPEED_UDC_ISR);
		aspeed_udc_ep0_in(udc);
	}

	if (isr & USB_EP0_OUT_ACK_STALL_ISR) {
		ISR_DBG("USB_EP0_OUT_ACK_STALL_ISR\n");
		ast_udc_write(udc, USB_EP0_OUT_ACK_STALL_ISR, ASPEED_UDC_ISR);
		aspeed_udc_ep0_out(udc);
	}

	if (isr & USB_EP0_OUT_NAK_ISR) {
//		ISR_DBG("USB_EP0_OUT_NAK_ISR\n");
		ast_udc_write(udc, USB_EP0_OUT_NAK_ISR, ASPEED_UDC_ISR);
	}

	if (isr & USB_EP0_IN_DATA_NAK_ISR) {
		//IN NAK, we don't handle this in sample
//		ISR_DBG("ISR_HUB_EP0_IN_DATA_ACK\n");
		ast_udc_write(udc, USB_EP0_IN_DATA_NAK_ISR, ASPEED_UDC_ISR);
	}

	if (isr & USB_EP0_SETUP_ISR) {
		ISR_DBG("SETUP\n");
		ast_udc_write(udc, USB_EP0_SETUP_ISR, ASPEED_UDC_ISR);
		aspeed_udc_setup_handle(udc);
	}

	if (isr & USB_PEP_POOL_ACK_STALL_ISR) {
//		EP_DBG("USB_PEP_POOL_ACK_STALL_ISR\n");
		ep_isr = ast_udc_read(udc, ASPEED_UDC_EP_ACK_ISR);
		for (i = 0; i < 4; i++) {
			if (ep_isr & (0x1 << i)) {
				ast_udc_write(udc, 0x1 << i, ASPEED_UDC_EP_ACK_ISR);

				if (udc->desc_mode)
					aspeed_udc_ep_handle_desc_mode(udc, i + 1);
				else
					aspeed_udc_ep_handle(udc, i + 1);
			}
		}
	}

	if (isr & USB_PEP_POOL_NAK_ISR) {
		ISR_DBG("USB_PEP_POOL_NAK_ISR ****************************************\n");
		ast_udc_write(udc, USB_PEP_POOL_NAK_ISR, ASPEED_UDC_ISR);
	}

	spin_unlock(&udc->lock);
	return IRQ_HANDLED;
}
/*-------------------------------------------------------------------------*/
static int aspeed_udc_gadget_getframe(struct usb_gadget *gadget)
{
	struct aspeed_udc *udc = container_of(gadget, struct aspeed_udc, gadget);

	UDC_DBG("\n");
	return (ast_udc_read(udc, ASPEED_UDC_STS) >> 16) & 0x7ff;
}

static int aspeed_udc_wakeup(struct usb_gadget *gadget)
{
	UDC_DBG("TODO\n");
	return 0;
}

/*
 * activate/deactivate link with host; minimize power usage for
 * inactive links by cutting clocks and transceiver power.
 */
static int aspeed_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct aspeed_udc *udc = container_of(gadget, struct aspeed_udc, gadget);

	UDC_DBG("%d\n", is_on);

	if (is_on)
		ast_udc_write(udc, ast_udc_read(udc, ASPEED_UDC_FUN_CTRL) | USB_UPSTREAM_EN, ASPEED_UDC_FUN_CTRL);
	else
		ast_udc_write(udc, ast_udc_read(udc, ASPEED_UDC_FUN_CTRL) & ~USB_UPSTREAM_EN, ASPEED_UDC_FUN_CTRL);

	return 0;
}

static int aspeed_udc_start(struct usb_gadget *gadget, struct usb_gadget_driver *driver)
{
	struct aspeed_udc *udc = container_of(gadget, struct aspeed_udc, gadget);
	int epnum;
	struct aspeed_udc_ep *ep;

	UDC_DBG("\n");

	if (!udc)
		return -ENODEV;

	if (udc->driver)
		return -EBUSY;

	udc->driver = driver;

	udc->gadget.dev.of_node = udc->pdev->dev.of_node;

	for (epnum = 0; epnum < AST_NUM_ENDPOINTS; epnum++) {
		ep = &udc->ep[epnum];
		ep->stopped = 0;
	}

	return 0;
}

static int aspeed_udc_stop(struct usb_gadget *gadget)
{
	struct aspeed_udc *udc = container_of(gadget, struct aspeed_udc, gadget);
	unsigned long flags;

	UDC_DBG("\n");

	spin_lock_irqsave(&udc->lock, flags);
	ast_udc_write(udc, ast_udc_read(udc, ASPEED_UDC_FUN_CTRL) & ~USB_UPSTREAM_EN, ASPEED_UDC_FUN_CTRL);
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	aspeed_udc_stop_activity(udc);
	spin_unlock_irqrestore(&udc->lock, flags);

	udc->driver = NULL;

	usb_gadget_set_state(&udc->gadget, USB_STATE_NOTATTACHED);

	return 0;
}

static const struct usb_gadget_ops ast_udc_ops = {
	.get_frame		= aspeed_udc_gadget_getframe,
	.wakeup			= aspeed_udc_wakeup,
	.pullup			= aspeed_udc_pullup,
	.udc_start		= aspeed_udc_start,
	.udc_stop		= aspeed_udc_stop,
};
/*-------------------------------------------------------------------------*/
static void aspeed_udc_init(struct aspeed_udc *udc)
{

	ast_udc_write(udc, USB_PHY_CLK_EN | USB_PHY_RESET_DIS | USB_ISO_IN_NULL_RESP, ASPEED_UDC_FUN_CTRL);

	udelay(1);
	ast_udc_write(udc, 0, ASPEED_UDC_DEV_RESET);

	ast_udc_write(udc, 0x1ffff & ~USB_EP0_OUT_NAK_ISR & ~USB_EP0_IN_DATA_NAK_ISR, ASPEED_UDC_IER);

	ast_udc_write(udc, 0xf, ASPEED_UDC_EP_ACK_IER);
	ast_udc_write(udc, 0xf, ASPEED_UDC_EP_ACK_ISR);

	ast_udc_write(udc, 0, AST_VHUB_EP0_CTRL);
}

static int aspeed_udc_remove(struct platform_device *pdev)
{
	struct aspeed_udc *udc = platform_get_drvdata(pdev);
	struct aspeed_udc_ep *ep;
	struct resource *res;
	unsigned long	flags;
	int i;

	usb_del_gadget_udc(&udc->gadget);
	if (udc->driver)
		return -EBUSY;

	spin_lock_irqsave(&udc->lock, flags);
	ast_udc_write(udc, ast_udc_read(udc, ASPEED_UDC_FUN_CTRL) & ~USB_UPSTREAM_EN, ASPEED_UDC_FUN_CTRL);
	spin_unlock_irqrestore(&udc->lock, flags);

	device_init_wakeup(&pdev->dev, 0);
	free_irq(udc->irq, udc);
	iounmap(udc->reg);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	for (i = 0; i < AST_NUM_ENDPOINTS; i++) {
		ep = &udc->ep[i];
		if (i)
			dma_free_coherent(&pdev->dev, AST_NUM_EP_DMA_DESC * sizeof(u64), ep->dma_desc_list, ep->dma_desc_dma_handle);
	}

	return 0;
}

static int aspeed_udc_probe(struct platform_device *pdev)
{
	enum usb_device_speed max_speed;
	struct device *dev = &pdev->dev;
	struct aspeed_udc *udc;
	struct aspeed_udc_ep *ep;
	struct resource *res;
	int rc = 0;
	int i;

	udc = devm_kzalloc(&pdev->dev, sizeof(struct aspeed_udc), GFP_KERNEL);
	if (!udc)
		return -ENOMEM;

	/* init software state */
	udc->gadget.dev.parent = dev;
	udc->pdev = pdev;
	spin_lock_init(&udc->lock);

	udc->gadget.ops = &ast_udc_ops;
	udc->gadget.ep0 = &udc->ep[0].ep;
	udc->gadget.name = "aspeed-udc";
	udc->gadget.dev.init_name = "gadget";

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	udc->reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(udc->reg)) {
		dev_err(&pdev->dev, "Failed to map resources\n");
		return PTR_ERR(udc->reg);
	}

	platform_set_drvdata(pdev, udc);

	udc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(udc->clk)) {
		rc = PTR_ERR(udc->clk);
		goto err;
	}
	rc = clk_prepare_enable(udc->clk);
	if (rc) {
		dev_err(&pdev->dev, "Error couldn't enable clock (%d)\n", rc);
		goto err;
	}

	/* Check if we need to limit the HW to USB1 */
	max_speed = usb_get_maximum_speed(&pdev->dev);
	if (max_speed != USB_SPEED_UNKNOWN && max_speed < USB_SPEED_HIGH)
		udc->force_usb1 = true;

	/* Mask & ack all interrupts before installing the handler */
	ast_udc_write(udc, 0, ASPEED_UDC_IER);
	ast_udc_write(udc, 0x1ffff, ASPEED_UDC_ISR);

	 /* Find interrupt and install handler */
	udc->irq = platform_get_irq(pdev, 0);
	if (udc->irq < 0) {
		dev_err(&pdev->dev, "Failed to get interrupt\n");
		rc = udc->irq;
		goto err;
	}

	/*
	 * Allocate DMA buffers for all EP0s in one chunk,
	 * one per port and one for the vHub itself
	 */
	udc->ep0_ctrl_buf = dma_alloc_coherent(&pdev->dev,
					EP_DMA_SIZE * AST_NUM_ENDPOINTS, &udc->ep0_ctrl_dma, GFP_KERNEL);

	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->gadget.max_speed = USB_SPEED_HIGH;

	udc->root_setup = udc->reg + 0x80;

	INIT_LIST_HEAD(&udc->gadget.ep_list);
	INIT_LIST_HEAD(&udc->gadget.ep0->ep_list);

	for (i = 0; i < AST_NUM_ENDPOINTS; i++) {
		ep = &udc->ep[i];
		ep->ep.name = ast_ep_name[i];
		if (i == 0) {
			ep->ep.caps.type_control = true;
		} else {
			ep->ep.caps.type_iso = true;
			ep->ep.caps.type_bulk = true;
			ep->ep.caps.type_int = true;
		}
		ep->ep.caps.dir_in = true;
		ep->ep.caps.dir_out = true;

		ep->ep.ops = &aspeed_udc_ep_ops;
		ep->udc = udc;
		if (i) {
			ep->ep_reg = udc->reg + 0x200 + (0x10 * (i - 1));

			ep->ep_buf = udc->ep0_ctrl_buf + (i * EP_DMA_SIZE);
			ep->ep_dma = udc->ep0_ctrl_dma + (i * EP_DMA_SIZE);
			usb_ep_set_maxpacket_limit(&ep->ep, 1024);
			/* allocate endpoint descrptor list (Note: must be DMA memory) */
			ep->dma_desc_list = dma_alloc_coherent(udc->gadget.dev.parent,
								AST_NUM_EP_DMA_DESC * sizeof(struct ast_dma_desc),
								&ep->dma_desc_dma_handle, GFP_KERNEL);
			ep->dma_desc_list_wptr = 0;

		} else {
			ep->ep_reg = 0;
			usb_ep_set_maxpacket_limit(&ep->ep, 64);
		}

//		pr_info("%s:maxpacket %d, dma:%x\n ", ep->ep.name, ep->ep.maxpacket, ep->ep_dma);
		if (i)
			list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);

		INIT_LIST_HEAD(&ep->queue);
	}

	udc->desc_mode = desc_mode;

	pr_info("ast_udc: current dma mode = %s\n",
		udc->desc_mode ? "descriptor mode" : "single buffer mode");

	aspeed_udc_init(udc);

	/* request UDC and maybe VBUS irqs */
	rc = devm_request_irq(&pdev->dev, udc->irq, aspeed_udc_isr, 0,
			      KBUILD_MODNAME, udc);
	if (rc < 0) {
		pr_info("request irq %d failed\n", udc->irq);
		goto err;
	}

	rc = usb_add_gadget_udc(&pdev->dev, &udc->gadget);
	if (rc)
		goto err;

	dev_set_drvdata(&pdev->dev, udc);
	device_init_wakeup(&pdev->dev, 1);

	pr_info("ast_udc: driver successfully loaded\n");

	return 0;

err:
	aspeed_udc_remove(pdev);
	pr_info("ast udc probe failed, %d\n", rc);

	return rc;
}

static const struct of_device_id aspeed_udc_of_dt_ids[] = {
	{ .compatible = "aspeed,ast2600-udc", },
	{}
};

MODULE_DEVICE_TABLE(of, aspeed_udc_of_dt_ids);

static struct platform_driver aspeed_udc_driver = {
	.probe			= aspeed_udc_probe,
	.remove			= aspeed_udc_remove,
	.driver			= {
		.name			= KBUILD_MODNAME,
		.of_match_table		= aspeed_udc_of_dt_ids,
	},
};

module_platform_driver(aspeed_udc_driver);

MODULE_DESCRIPTION("ASPEED UDC driver");
MODULE_AUTHOR("Neal Liu");
MODULE_LICENSE("GPL");
