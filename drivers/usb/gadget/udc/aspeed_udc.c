// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2021 Aspeed Technology Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/prefetch.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/slab.h>

#define AST_UDC_NUM_ENDPOINTS		(1 + 4)
#define AST_UDC_EP0_MAX_PACKET		64	/* EP0's max packet size */
#define AST_UDC_EPn_MAX_PACKET		1024	/* Generic EPs max packet size */
#define AST_UDC_DESCS_COUNT		256	/* Use 256 stages descriptor mode (32/256) */
#define AST_UDC_DESC_MODE		1	/* Single/Multiple Stage(s) Descriptor Mode */

#define AST_UDC_EP_DMA_SIZE		(AST_UDC_EPn_MAX_PACKET + 8 * AST_UDC_DESCS_COUNT)

/*****************************
 *                           *
 * UDC register definitions  *
 *                           *
 *****************************/

#define AST_UDC_FUNC_CTRL		0x00	/* Root Function Control & Status Register */
#define AST_UDC_CONFIG			0x04	/* Root Configuration Setting Register */
#define AST_UDC_IER			0x08	/* Interrupt Control Register */
#define AST_UDC_ISR			0x0C	/* Interrupt Status Register */
#define AST_UDC_EP_ACK_IER		0x10	/* Programmable ep Pool ACK Interrupt Enable Reg */
#define AST_UDC_EP_NAK_IER		0x14	/* Programmable ep Pool NAK Interrupt Enable Reg */
#define AST_UDC_EP_ACK_ISR		0x18	/* Programmable ep Pool ACK Interrupt Status Reg */
#define AST_UDC_EP_NAK_ISR		0x1C	/* Programmable ep Pool NAK Interrupt Status Reg */
#define AST_UDC_DEV_RESET		0x20	/* Device Controller Soft Reset Enable Register */
#define AST_UDC_STS			0x24	/* USB Status Register */
#define AST_VHUB_EP_DATA		0x28	/* Programmable ep Pool Data Toggle Value Set */
#define AST_VHUB_ISO_TX_FAIL		0x2C	/* Isochronous Transaction Fail Accumulator */
#define AST_UDC_EP0_CTRL		0x30	/* Endpoint 0 Control/Status Register */
#define AST_UDC_EP0_DATA_BUFF		0x34	/* Base Address of ep0 IN/OUT Data Buffer Reg */
#define AST_UDC_SETUP0			0x80    /* Root Device Setup Data Buffer0 */
#define AST_UDC_SETUP1			0x84    /* Root Device Setup Data Buffer1 */


/* Main control reg */
#define USB_PHY_CLK_EN			BIT(31)
#define USB_FIFO_DYN_PWRD_EN		BIT(19)
#define USB_EP_LONG_DESC		BIT(18)
#define USB_BIST_TEST_PASS		BIT(13)
#define USB_BIST_TURN_ON		BIT(12)
#define USB_PHY_RESET_DIS		BIT(11)
#define USB_TEST_MODE(x)		((x) << 8)
#define USB_FORCE_TIMER_HS		BIT(7)
#define USB_FORCE_HS			BIT(6)
#define USB_REMOTE_WAKEUP_12MS		BIT(5)
#define USB_REMOTE_WAKEUP_EN		BIT(4)
#define USB_AUTO_REMOTE_WAKEUP_EN	BIT(3)
#define USB_STOP_CLK_IN_SUPEND		BIT(2)
#define USB_UPSTREAM_FS			BIT(1)
#define USB_UPSTREAM_EN			BIT(0)

/* Main config reg */
#define UDC_CFG_SET_ADDR(x)		((x) & UDC_CFG_ADDR_MASK)
#define UDC_CFG_ADDR_MASK		GENMASK(6, 0)

/* Interrupt ctrl & status reg */
#define UDC_IRQ_EP_POOL_NAK		BIT(17)
#define UDC_IRQ_EP_POOL_ACK_STALL	BIT(16)
#define UDC_IRQ_BUS_RESUME		BIT(8)
#define UDC_IRQ_BUS_SUSPEND		BIT(7)
#define UDC_IRQ_BUS_RESET		BIT(6)
#define UDC_IRQ_EP0_IN_DATA_NAK		BIT(4)
#define UDC_IRQ_EP0_IN_ACK_STALL	BIT(3)
#define UDC_IRQ_EP0_OUT_NAK		BIT(2)
#define UDC_IRQ_EP0_OUT_ACK_STALL	BIT(1)
#define UDC_IRQ_EP0_SETUP		BIT(0)
#define UDC_IRQ_ACK_ALL			(0x1ff)

/* EP isr reg */
#define USB_EP3_ISR			BIT(3)
#define USB_EP2_ISR			BIT(2)
#define USB_EP1_ISR			BIT(1)
#define USB_EP0_ISR			BIT(0)
#define UDC_IRQ_EP_ACK_ALL		(0xf)

/*Soft reset reg */
#define ROOT_UDC_SOFT_RESET		BIT(0)

/* USB status reg */
#define UDC_STS_HIGHSPEED		BIT(27)

/* Programmable EP data toggle */
#define EP_TOGGLE_SET_EPNUM(x)		((x) & 0x3)

/* EP0 ctrl reg */
#define EP0_GET_RX_LEN(x)		((x >> 16) & 0x7f)
#define EP0_TX_LEN(x)			((x & 0x7f) << 8)
#define EP0_RX_BUFF_RDY			BIT(2)
#define EP0_TX_BUFF_RDY			BIT(1)
#define EP0_STALL			BIT(0)

/*************************************
 *                                   *
 * per-endpoint register definitions *
 *                                   *
 *************************************/

#define AST_UDC_EP_CONFIG		0x00	/* Endpoint Configuration Register */
#define AST_UDC_EP_DMA_CTRL		0x04	/* DMA Descriptor List Control/Status Register */
#define AST_UDC_EP_DMA_BUFF		0x08	/* DMA Descriptor/Buffer Base Address */
#define AST_UDC_EP_DMA_STS		0x0C	/* DMA Descriptor List R/W Pointer and Status */

#define AST_UDC_EP_BASE			0x200
#define AST_UDC_EP_OFFSET		0x10

/* EP config reg */
#define EP_SET_MAX_PKT(x)		((x & 0x3ff) << 16)
#define EP_DATA_FETCH_CTRL(x)		((x & 0x3) << 14)
#define EP_AUTO_DATA_DISABLE		(0x1 << 13)
#define EP_SET_EP_STALL			(0x1 << 12)
#define EP_SET_EP_NUM(x)		((x & 0xf) << 8)
#define EP_SET_TYPE_MASK(x)		((x) << 5)
#define EP_TYPE_BULK			(0x1)
#define EP_TYPE_INT			(0x2)
#define EP_TYPE_ISO			(0x3)
#define EP_DIR_OUT			(0x1 << 4)
#define EP_ALLOCATED_MASK		(0x7 << 1)
#define EP_ENABLE			BIT(0)

/* EP DMA ctrl reg */
#define EP_DMA_CTRL_GET_PROC_STS(x)	((x >> 4) & 0xf)
#define EP_DMA_CTRL_STS_RX_IDLE		0x0
#define EP_DMA_CTRL_STS_TX_IDLE		0x8
#define EP_DMA_CTRL_IN_LONG_MODE	(0x1 << 3)
#define EP_DMA_CTRL_RESET		(0x1 << 2)
#define EP_DMA_SINGLE_STAGE		(0x1 << 1)
#define EP_DMA_DESC_MODE		(0x1 << 0)

/* EP DMA status reg */
#define EP_DMA_SET_TX_SIZE(x)		((x & 0x7ff) << 16)
#define EP_DMA_GET_TX_SIZE(x)		(((x) >> 16) & 0x7ff)
#define EP_DMA_GET_RPTR(x)		(((x) >> 8) & 0xff)
#define EP_DMA_GET_WPTR(x)		((x) & 0xff)
#define EP_DMA_SINGLE_KICK		(1 << 0) /* WPTR = 1 for single mode */

/* EP desc reg */
#define AST_EP_DMA_DESC_INTR_ENABLE	BIT(31)
#define AST_EP_DMA_DESC_PID_DATA0	(0 << 14)
#define AST_EP_DMA_DESC_PID_DATA2	BIT(14)
#define AST_EP_DMA_DESC_PID_DATA1	(2 << 14)
#define AST_EP_DMA_DESC_PID_MDATA	(3 << 14)
#define EP_DESC1_IN_LEN(x)		((x) & 0x1fff)
#define AST_EP_DMA_DESC_MAX_LEN		(7680) /* Max packet length for trasmit in 1 desc */

struct ast_udc_request {
	struct usb_request	req;
	struct list_head	queue;
	unsigned		mapped:1;
	unsigned int		actual_dma_length;
	u32			saved_dma_wptr;
};

#define to_ast_req(__req) container_of(__req, struct ast_udc_request, req)

struct ast_dma_desc {
	u32	des_0;
	u32	des_1;
};

struct ast_udc_ep {
	struct usb_ep			ep;

	/* Request queue */
	struct list_head		queue;

	struct ast_udc_dev		*udc;
	void __iomem			*ep_reg;
	void				*epn_buf;
	dma_addr_t			epn_buf_dma;
	const struct usb_endpoint_descriptor	*desc;

	/* DMA Descriptors */
	struct ast_dma_desc		*descs;
	dma_addr_t			descs_dma;
	u32				descs_wptr;
	u32				chunk_max;

	bool				dir_in:1;
	unsigned			stopped:1;
	bool				desc_mode:1;
};

#define to_ast_ep(__ep) container_of(__ep, struct ast_udc_ep, ep)

struct ast_udc_dev {
	struct platform_device		*pdev;
	void __iomem			*reg;
	int				irq;
	spinlock_t			lock;
	struct clk			*clk;
	struct work_struct		wake_work;

	/* EP0 DMA buffers allocated in one chunk */
	void				*ep0_buf;
	dma_addr_t			ep0_buf_dma;
	struct ast_udc_ep		ep[AST_UDC_NUM_ENDPOINTS];

	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;
	void __iomem			*creq;
	enum usb_device_state		suspended_from;
	int				desc_mode;

	/* Force full speed only */
	bool				force_usb1:1;
	unsigned			is_control_tx:1;
	bool				wakeup_en:1;
};

#define to_ast_dev(__g) container_of(__g, struct ast_udc_dev, gadget)

static const char * const ast_ep_name[] = {
	"ep0", "ep1", "ep2", "ep3", "ep4"
};

#ifdef AST_UDC_DEBUG_ALL
#define AST_UDC_DEBUG
#define AST_SETUP_DEBUG
#define AST_EP_DEBUG
#define AST_ISR_DEBUG
#endif

#ifdef AST_SETUP_DEBUG
#define SETUP_DBG(u, fmt, ...)	\
	dev_dbg(&(u)->pdev->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#else
#define SETUP_DBG(u, fmt, ...)
#endif

#ifdef AST_EP_DEBUG
#define EP_DBG(e, fmt, ...)	\
	dev_dbg(&(e)->udc->pdev->dev, "%s():%s " fmt, __func__,	\
		 (e)->ep.name, ##__VA_ARGS__)
#else
#define EP_DBG(ep, fmt, ...)	((void)(ep))
#endif

#ifdef AST_UDC_DEBUG
#define UDC_DBG(u, fmt, ...)	\
	dev_dbg(&(u)->pdev->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#else
#define UDC_DBG(u, fmt, ...)
#endif

#ifdef AST_ISR_DEBUG
#define ISR_DBG(u, fmt, ...)	\
	dev_dbg(&(u)->pdev->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#else
#define ISR_DBG(u, fmt, ...)
#endif

/*-------------------------------------------------------------------------*/
#define ast_udc_read(udc, offset) \
	readl((udc)->reg + (offset))
#define ast_udc_write(udc, val, offset) \
	writel((val), (udc)->reg + (offset))

#define ast_ep_read(ep, reg) \
	readl((ep)->ep_reg + (reg))
#define ast_ep_write(ep, val, reg) \
	writel((val), (ep)->ep_reg + (reg))

/*-------------------------------------------------------------------------*/

static void ast_udc_done(struct ast_udc_ep *ep, struct ast_udc_request *req,
			 int status)
{
	struct ast_udc_dev *udc = ep->udc;

	EP_DBG(ep, "req @%p, len (%d/%d), buf:0x%x, dir:0x%x\n",
	       req, req->req.actual, req->req.length,
	       (u32)req->req.buf, ep->dir_in);

	list_del(&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	if (status && status != -ESHUTDOWN)
		EP_DBG(ep, "done req:%p, status:%d\n", req, status);

	spin_unlock(&udc->lock);
	usb_gadget_giveback_request(&ep->ep, &req->req);
	spin_lock(&udc->lock);
}

static void ast_udc_nuke(struct ast_udc_ep *ep, int status)
{
	int count = 0;

	while (!list_empty(&ep->queue)) {
		struct ast_udc_request *req;

		req = list_entry(ep->queue.next, struct ast_udc_request,
				 queue);
		ast_udc_done(ep, req, status);
		count++;
	}

	if (count)
		EP_DBG(ep, "Nuked %d request(s)\n", count);
}

/*
 * Stop activity on all endpoints.
 * Device controller for which EP activity is to be stopped.
 *
 * All the endpoints are stopped and any pending transfer requests if any on
 * the endpoint are terminated.
 */
static void ast_udc_stop_activity(struct ast_udc_dev *udc)
{
	struct ast_udc_ep *ep;
	int i;

	for (i = 0; i < AST_UDC_NUM_ENDPOINTS; i++) {
		ep = &udc->ep[i];
		ep->stopped = 1;
		ast_udc_nuke(ep, -ESHUTDOWN);
	}
}

static int ast_udc_ep_enable(struct usb_ep *_ep,
			     const struct usb_endpoint_descriptor *desc)
{
	u16 maxpacket = usb_endpoint_maxp(desc);
	struct ast_udc_ep *ep = to_ast_ep(_ep);
	struct ast_udc_dev *udc = ep->udc;
	u8 epnum = usb_endpoint_num(desc);
	unsigned long flags;
	u32 ep_conf = 0;
	u8 dir_in;
	u8 type;

	if (!_ep || !ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT ||
	    maxpacket == 0 || maxpacket > ep->ep.maxpacket) {
		EP_DBG(ep, "Failed, invalid EP enable param\n");
		return -EINVAL;
	}

	if (!udc->driver) {
		EP_DBG(ep, "bogus device state\n");
		return -ESHUTDOWN;
	}

	EP_DBG(ep, "maxpacket:0x%x\n", maxpacket);

	spin_lock_irqsave(&udc->lock, flags);

	ep->desc = desc;
	ep->stopped = 0;
	ep->ep.maxpacket = maxpacket;
	ep->chunk_max = AST_EP_DMA_DESC_MAX_LEN;

	if (maxpacket < AST_UDC_EPn_MAX_PACKET)
		ep_conf = EP_SET_MAX_PKT(maxpacket);

	ep_conf |= EP_SET_EP_NUM(epnum);

	type = usb_endpoint_type(desc);
	dir_in = usb_endpoint_dir_in(desc);
	ep->dir_in = dir_in;
	if (!ep->dir_in)
		ep_conf |= EP_DIR_OUT;

	EP_DBG(ep, "type %d, dir_in %d\n", type, dir_in);
	switch (type) {
	case USB_ENDPOINT_XFER_ISOC:
		ep_conf |= EP_SET_TYPE_MASK(EP_TYPE_ISO);
		break;

	case USB_ENDPOINT_XFER_BULK:
		ep_conf |= EP_SET_TYPE_MASK(EP_TYPE_BULK);
		break;

	case USB_ENDPOINT_XFER_INT:
		ep_conf |= EP_SET_TYPE_MASK(EP_TYPE_INT);
		break;
	}

	ep->desc_mode = udc->desc_mode && ep->descs_dma && ep->dir_in;
	if (ep->desc_mode) {
		ast_ep_write(ep, EP_DMA_CTRL_RESET, AST_UDC_EP_DMA_CTRL);
		ast_ep_write(ep, 0, AST_UDC_EP_DMA_STS);
		ast_ep_write(ep, ep->descs_dma, AST_UDC_EP_DMA_BUFF);

		/* Enable Long Descriptor Mode */
		ast_ep_write(ep, EP_DMA_CTRL_IN_LONG_MODE | EP_DMA_DESC_MODE,
			     AST_UDC_EP_DMA_CTRL);

		ep->descs_wptr = 0;

	} else {
		ast_ep_write(ep, EP_DMA_CTRL_RESET, AST_UDC_EP_DMA_CTRL);
		ast_ep_write(ep, EP_DMA_SINGLE_STAGE, AST_UDC_EP_DMA_CTRL);
		ast_ep_write(ep, 0, AST_UDC_EP_DMA_STS);
	}

	/* Cleanup data toggle just in case */
	ast_udc_write(udc, EP_TOGGLE_SET_EPNUM(epnum), AST_VHUB_EP_DATA);

	/* Enable EP */
	ast_ep_write(ep, ep_conf | EP_ENABLE, AST_UDC_EP_CONFIG);

	EP_DBG(ep, "ep_config: 0x%x\n", ast_ep_read(ep, AST_UDC_EP_CONFIG));

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int ast_udc_ep_disable(struct usb_ep *_ep)
{
	struct ast_udc_ep *ep = to_ast_ep(_ep);
	struct ast_udc_dev *udc = ep->udc;
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	ep->ep.desc = NULL;
	ep->stopped = 1;

	ast_udc_nuke(ep, -ESHUTDOWN);
	ast_ep_write(ep, 0, AST_UDC_EP_CONFIG);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static struct usb_request *ast_udc_ep_alloc_request(struct usb_ep *_ep,
						    gfp_t gfp_flags)
{
	struct ast_udc_ep *ep = to_ast_ep(_ep);
	struct ast_udc_request *req;

	req = kzalloc(sizeof(struct ast_udc_request), gfp_flags);
	if (!req) {
		EP_DBG(ep, "request allocation failed\n");
		return NULL;
	}

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void ast_udc_ep_free_request(struct usb_ep *_ep,
				    struct usb_request *_req)
{
	struct ast_udc_request *req = to_ast_req(_req);

	kfree(req);
}

static int ast_dma_descriptor_setup(struct ast_udc_ep *ep, u32 dma_buf,
				    u16 tx_len, struct ast_udc_request *req)
{
	struct ast_udc_dev *udc = ep->udc;
	struct device *dev = &udc->pdev->dev;
	bool last = false;
	int chunk, count;
	u32 offset;

	if (!ep->descs) {
		dev_warn(dev, "%s: Empty DMA descs list failure\n",
			 ep->ep.name);
		return -EINVAL;
	}

	chunk = tx_len;
	offset = count = 0;

	EP_DBG(ep, "req @%p, %s:%d, %s:0x%x, %s:0x%x\n", req,
	       "wptr", ep->descs_wptr, "dma_buf", dma_buf,
	       "tx_len", tx_len);

	/* Create Descriptor Lists */
	while (chunk >= 0 && !last && count < AST_UDC_DESCS_COUNT) {

		ep->descs[ep->descs_wptr].des_0 = dma_buf + offset;

		if (chunk > ep->chunk_max) {
			ep->descs[ep->descs_wptr].des_1 = ep->chunk_max;
		} else {
			ep->descs[ep->descs_wptr].des_1 = chunk;
			last = true;
		}

		chunk -= ep->chunk_max;

		EP_DBG(ep, "descs[%d]: 0x%x 0x%x\n",
		       ep->descs_wptr,
		       ep->descs[ep->descs_wptr].des_0,
		       ep->descs[ep->descs_wptr].des_1);

		if (count == 0)
			req->saved_dma_wptr = ep->descs_wptr;

		ep->descs_wptr++;
		count++;

		if (ep->descs_wptr >= AST_UDC_DESCS_COUNT)
			ep->descs_wptr = 0;

		offset = ep->chunk_max * count;
	}

	return 0;
}

static void ast_udc_epn_kick(struct ast_udc_ep *ep, struct ast_udc_request *req)
{
	u32 tx_len;
	u32 last;

	last = req->req.length - req->req.actual;
	tx_len = last > ep->ep.maxpacket ? ep->ep.maxpacket : last;

	EP_DBG(ep, "kick req @%p, len:%d, dir:%d\n",
	       req, tx_len, ep->dir_in);

	ast_ep_write(ep, req->req.dma + req->req.actual, AST_UDC_EP_DMA_BUFF);

	/* Start DMA */
	ast_ep_write(ep, EP_DMA_SET_TX_SIZE(tx_len), AST_UDC_EP_DMA_STS);
	ast_ep_write(ep, EP_DMA_SET_TX_SIZE(tx_len) | EP_DMA_SINGLE_KICK,
		     AST_UDC_EP_DMA_STS);
}

static void ast_udc_epn_kick_desc(struct ast_udc_ep *ep,
				  struct ast_udc_request *req)
{
	u32 descs_max_size;
	u32 tx_len;
	u32 last;

	descs_max_size = AST_EP_DMA_DESC_MAX_LEN * AST_UDC_DESCS_COUNT;

	last = req->req.length - req->req.actual;
	tx_len = last > descs_max_size ? descs_max_size : last;

	EP_DBG(ep, "kick req @%p, %s:%d, %s:0x%x, %s:0x%x (%d/%d), %s:0x%x\n",
	       req, "tx_len", tx_len, "dir_in", ep->dir_in,
	       "dma", req->req.dma + req->req.actual,
	       req->req.actual, req->req.length,
	       "descs_max_size", descs_max_size);

	if (!ast_dma_descriptor_setup(ep, req->req.dma + req->req.actual,
				      tx_len, req))
		req->actual_dma_length += tx_len;

	/* make sure CPU done everything before triggering DMA */
	mb();

	ast_ep_write(ep, ep->descs_wptr, AST_UDC_EP_DMA_STS);

	EP_DBG(ep, "descs_wptr:%d, dstat:0x%x, dctrl:0x%x\n",
	       ep->descs_wptr,
	       ast_ep_read(ep, AST_UDC_EP_DMA_STS),
	       ast_ep_read(ep, AST_UDC_EP_DMA_CTRL));
}

static void ast_udc_ep0_queue(struct ast_udc_ep *ep,
			      struct ast_udc_request *req)
{
	struct ast_udc_dev *udc = ep->udc;
	u32 tx_len;
	u32 last;

	last = req->req.length - req->req.actual;
	tx_len = last > ep->ep.maxpacket ? ep->ep.maxpacket : last;

	ast_udc_write(udc, req->req.dma + req->req.actual,
		      AST_UDC_EP0_DATA_BUFF);

	if (ep->dir_in) {
		/* IN requests, send data */
		SETUP_DBG(udc, "IN: %s:0x%x, %s:0x%x, %s:%d (%d/%d), %s:%d\n",
			  "buf", (u32)req->req.buf,
			  "dma", req->req.dma + req->req.actual,
			  "tx_len", tx_len,
			  req->req.actual, req->req.length,
			  "dir_in", ep->dir_in);

		req->req.actual += tx_len;
		ast_udc_write(udc, EP0_TX_LEN(tx_len), AST_UDC_EP0_CTRL);
		ast_udc_write(udc, EP0_TX_LEN(tx_len) | EP0_TX_BUFF_RDY,
			      AST_UDC_EP0_CTRL);

	} else {
		/* OUT requests, receive data */
		SETUP_DBG(udc, "OUT: %s:%x, %s:%x, %s:(%d/%d), %s:%d\n",
			  "buf", (u32)req->req.buf,
			  "dma", req->req.dma + req->req.actual,
			  "len", req->req.actual, req->req.length,
			  "dir_in", ep->dir_in);

		if (!req->req.length) {
			/* 0 len request, send tx as completion */
			ast_udc_write(udc, EP0_TX_BUFF_RDY, AST_UDC_EP0_CTRL);
			ep->dir_in = 0x1;
		} else
			ast_udc_write(udc, EP0_RX_BUFF_RDY, AST_UDC_EP0_CTRL);
	}
}

static int ast_udc_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
			    gfp_t gfp_flags)
{
	struct ast_udc_request *req = to_ast_req(_req);
	struct ast_udc_ep *ep = to_ast_ep(_ep);
	struct ast_udc_dev *udc = ep->udc;
	struct device *dev = &udc->pdev->dev;
	unsigned long flags;
	int rc;

	if (unlikely(!_req || !_req->complete || !_req->buf || !_ep)) {
		dev_warn(dev, "Invalid EP request !\n");
		return -EINVAL;
	}

	if (ep->stopped) {
		dev_warn(dev, "%s is already stopped !\n", _ep->name);
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&udc->lock, flags);

	list_add_tail(&req->queue, &ep->queue);

	req->req.actual = 0;
	req->req.status = -EINPROGRESS;
	req->actual_dma_length = 0;

	rc = usb_gadget_map_request(&udc->gadget, &req->req, ep->dir_in);
	if (rc) {
		EP_DBG(ep, "Request mapping failure %d\n", rc);
		dev_warn(dev, "Request mapping failure %d\n", rc);
		goto end;
	}

	EP_DBG(ep, "enqueue req @%p\n", req);
	EP_DBG(ep, "l=%d, dma:0x%x, zero:%d, is_in:%d\n",
		_req->length, _req->dma, _req->zero, ep->dir_in);

	/* EP0 request enqueue */
	if (ep->ep.desc == NULL) {
		if ((req->req.dma % 4) != 0) {
			dev_warn(dev, "EP0 req dma alignment error\n");
			rc = -ESHUTDOWN;
			goto end;
		}

		ast_udc_ep0_queue(ep, req);
		goto end;
	}

	/* EPn request enqueue */
	if (list_is_singular(&ep->queue)) {
		if (ep->desc_mode)
			ast_udc_epn_kick_desc(ep, req);
		else
			ast_udc_epn_kick(ep, req);
	}

end:
	spin_unlock_irqrestore(&udc->lock, flags);

	return rc;
}

static int ast_udc_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct ast_udc_ep *ep = to_ast_ep(_ep);
	struct ast_udc_dev *udc = ep->udc;
	struct ast_udc_request *req;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&udc->lock, flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req) {
			list_del_init(&req->queue);
			ast_udc_done(ep, req, -ESHUTDOWN);
			_req->status = -ECONNRESET;
			break;
		}
	}

	/* dequeue request not found */
	if (&req->req != _req)
		rc = -EINVAL;

	spin_unlock_irqrestore(&udc->lock, flags);

	return rc;
}

static int ast_udc_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct ast_udc_ep *ep = to_ast_ep(_ep);
	struct ast_udc_dev *udc = ep->udc;
	unsigned long flags;
	int epnum;
	u32 ctrl;

	EP_DBG(ep, "val:%d\n", value);

	spin_lock_irqsave(&udc->lock, flags);

	epnum = usb_endpoint_num(ep->desc);

	/* EP0 */
	if (epnum == 0) {
		ctrl = ast_udc_read(udc, AST_UDC_EP0_CTRL);
		if (value)
			ctrl |= EP0_STALL;
		else
			ctrl &= ~EP0_STALL;

		ast_udc_write(udc, ctrl, AST_UDC_EP0_CTRL);

	} else {
	/* EPn */
		ctrl = ast_udc_read(udc, AST_UDC_EP_CONFIG);
		if (value)
			ctrl |= EP_SET_EP_STALL;
		else
			ctrl &= ~EP_SET_EP_STALL;

		ast_ep_write(ep, ctrl, AST_UDC_EP_CONFIG);

		/* only epn is stopped and waits for clear */
		ep->stopped = value ? 1 : 0;
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static const struct usb_ep_ops ast_udc_ep_ops = {
	.enable		= ast_udc_ep_enable,
	.disable	= ast_udc_ep_disable,
	.alloc_request	= ast_udc_ep_alloc_request,
	.free_request	= ast_udc_ep_free_request,
	.queue		= ast_udc_ep_queue,
	.dequeue	= ast_udc_ep_dequeue,
	.set_halt	= ast_udc_ep_set_halt,
	/* there's only imprecise fifo status reporting */
};

static void ast_udc_ep0_rx(struct ast_udc_dev *udc)
{
	ast_udc_write(udc, udc->ep0_buf_dma, AST_UDC_EP0_DATA_BUFF);
	ast_udc_write(udc, EP0_RX_BUFF_RDY, AST_UDC_EP0_CTRL);
}

static void ast_udc_ep0_tx(struct ast_udc_dev *udc)
{
	ast_udc_write(udc, udc->ep0_buf_dma, AST_UDC_EP0_DATA_BUFF);
	ast_udc_write(udc, EP0_TX_BUFF_RDY, AST_UDC_EP0_CTRL);
}

static void ast_udc_ep0_out(struct ast_udc_dev *udc)
{
	struct device *dev = &udc->pdev->dev;
	struct ast_udc_ep *ep = &udc->ep[0];
	struct ast_udc_request *req;
	u16 rx_len;

	if (list_empty(&ep->queue))
		return;

	req = list_entry(ep->queue.next, struct ast_udc_request, queue);

	rx_len = EP0_GET_RX_LEN(ast_udc_read(udc, AST_UDC_EP0_CTRL));
	req->req.actual += rx_len;

	SETUP_DBG(udc, "req %p (%d/%d)\n", req,
		  req->req.actual, req->req.length);

	if ((rx_len < ep->ep.maxpacket) ||
	    (req->req.actual == req->req.length)) {
		ast_udc_ep0_tx(udc);
		if (!ep->dir_in)
			ast_udc_done(ep, req, 0);

	} else {
		if (rx_len > req->req.length) {
			// Issue Fix
			dev_warn(dev, "Something wrong (%d/%d)\n",
				 req->req.actual, req->req.length);
			ast_udc_ep0_tx(udc);
			ast_udc_done(ep, req, 0);
			return;
		}

		ep->dir_in = 0;

		/* More works */
		ast_udc_ep0_queue(ep, req);
	}
}

static void ast_udc_ep0_in(struct ast_udc_dev *udc)
{
	struct ast_udc_ep *ep = &udc->ep[0];
	struct ast_udc_request *req;

	if (list_empty(&ep->queue)) {
		if (udc->is_control_tx) {
			ast_udc_ep0_rx(udc);
			udc->is_control_tx = 0;
		}

		return;
	}

	req = list_entry(ep->queue.next, struct ast_udc_request, queue);

	SETUP_DBG(udc, "req %p (%d/%d)\n", req,
		  req->req.actual, req->req.length);

	if (req->req.length == req->req.actual) {
		if (req->req.length)
			ast_udc_ep0_rx(udc);

		if (ep->dir_in)
			ast_udc_done(ep, req, 0);

	} else {
		/* More works */
		ast_udc_ep0_queue(ep, req);
	}
}

static void ast_udc_epn_handle(struct ast_udc_dev *udc, u16 ep_num)
{
	struct ast_udc_ep *ep = &udc->ep[ep_num];
	struct ast_udc_request *req;
	u16 len = 0;

	if (list_empty(&ep->queue))
		return;

	req = list_first_entry(&ep->queue, struct ast_udc_request, queue);

	len = EP_DMA_GET_TX_SIZE(ast_ep_read(ep, AST_UDC_EP_DMA_STS));
	req->req.actual += len;

	EP_DBG(ep, "req @%p, length:(%d/%d), %s:0x%x\n", req,
		req->req.actual, req->req.length, "len", len);

	/* Done this request */
	if (req->req.length == req->req.actual) {
		ast_udc_done(ep, req, 0);
		req = list_first_entry_or_null(&ep->queue,
					       struct ast_udc_request,
					       queue);

	} else {
		/* Check for short packet */
		if (len < ep->ep.maxpacket) {
			ast_udc_done(ep, req, 0);
			req = list_first_entry_or_null(&ep->queue,
						       struct ast_udc_request,
						       queue);
		}
	}

	/* More requests */
	if (req)
		ast_udc_epn_kick(ep, req);
}

static void ast_udc_epn_handle_desc(struct ast_udc_dev *udc, u16 ep_num)
{
	struct ast_udc_ep *ep = &udc->ep[ep_num];
	struct device *dev = &udc->pdev->dev;
	struct ast_udc_request *req;
	u32 proc_sts, wr_ptr, rd_ptr;
	u32 len_in_desc, ctrl;
	u16 total_len = 0;
	int i;

	if (list_empty(&ep->queue)) {
		dev_warn(dev, "%s request queue empty!\n", ep->ep.name);
		return;
	}

	req = list_first_entry(&ep->queue, struct ast_udc_request, queue);

	ctrl = ast_ep_read(ep, AST_UDC_EP_DMA_CTRL);
	proc_sts = EP_DMA_CTRL_GET_PROC_STS(ctrl);

	/* Check processing status is idle */
	if (proc_sts != EP_DMA_CTRL_STS_RX_IDLE &&
	    proc_sts != EP_DMA_CTRL_STS_TX_IDLE) {
		dev_warn(dev, "EP DMA CTRL: 0x%x, PS:0x%x\n",
			 ast_ep_read(ep, AST_UDC_EP_DMA_CTRL),
			 proc_sts);
		return;
	}

	ctrl = ast_ep_read(ep, AST_UDC_EP_DMA_STS);
	rd_ptr = EP_DMA_GET_RPTR(ctrl);
	wr_ptr = EP_DMA_GET_WPTR(ctrl);

	if (rd_ptr != wr_ptr) {
		dev_warn(dev, "desc list is not empty ! %s:%d, %s:%d\n",
		"rptr", rd_ptr, "wptr", wr_ptr);
		return;
	}

	EP_DBG(ep, "rd_ptr:%d, wr_ptr:%d\n", rd_ptr, wr_ptr);
	i = req->saved_dma_wptr;

	do {
		len_in_desc = EP_DESC1_IN_LEN(ep->descs[i].des_1);
		EP_DBG(ep, "desc[%d] len: %d\n", i, len_in_desc);
		total_len += len_in_desc;
		i++;
		if (i >= AST_UDC_DESCS_COUNT)
			i = 0;

	} while (i != wr_ptr);

	req->req.actual += total_len;

	EP_DBG(ep, "req @%p, length:(%d/%d), %s:0x%x\n", req,
		req->req.actual, req->req.length, "len", total_len);

	/* Done this request */
	if (req->req.length == req->req.actual) {
		ast_udc_done(ep, req, 0);
		req = list_first_entry_or_null(&ep->queue,
					       struct ast_udc_request,
					       queue);

	} else {
		/* Check for short packet */
		if (total_len < ep->ep.maxpacket) {
			ast_udc_done(ep, req, 0);
			req = list_first_entry_or_null(&ep->queue,
						       struct ast_udc_request,
						       queue);
		}
	}

	/* More requests & dma descs not setup yet */
	if (req && (req->actual_dma_length == req->req.actual)) {
		EP_DBG(ep, "More requests\n");
		ast_udc_epn_kick_desc(ep, req);
	}
}

static void ast_udc_ep0_data_tx(struct ast_udc_dev *udc, u8 *tx_data, u32 len)
{
	if (len) {
		memcpy(udc->ep0_buf, tx_data, len);

		ast_udc_write(udc, udc->ep0_buf_dma, AST_UDC_EP0_DATA_BUFF);
		ast_udc_write(udc, EP0_TX_LEN(len), AST_UDC_EP0_CTRL);
		ast_udc_write(udc, EP0_TX_LEN(len) | EP0_TX_BUFF_RDY,
			      AST_UDC_EP0_CTRL);
		udc->is_control_tx = 1;

	} else
		ast_udc_write(udc, EP0_TX_BUFF_RDY, AST_UDC_EP0_CTRL);
}

static void ast_udc_getstatus(struct ast_udc_dev *udc)
{
	struct usb_ctrlrequest crq;
	struct ast_udc_ep *ep;
	u16 status = 0;
	u16 epnum = 0;

	memcpy_fromio(&crq, udc->creq, sizeof(crq));

	switch (crq.bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		/* Get device status */
		status = 1 << USB_DEVICE_SELF_POWERED;
		break;
	case USB_RECIP_INTERFACE:
		break;
	case USB_RECIP_ENDPOINT:
		epnum = crq.wIndex & USB_ENDPOINT_NUMBER_MASK;
		if (epnum >= AST_UDC_NUM_ENDPOINTS)
			goto stall;
		status = udc->ep[epnum].stopped;
		break;
	default:
		goto stall;
	}

	ep = &udc->ep[epnum];
	EP_DBG(ep, "status: 0x%x\n", status);
	ast_udc_ep0_data_tx(udc, (u8 *)&status, sizeof(status));

	return;

stall:
	EP_DBG(ep, "Can't respond request\n");
	ast_udc_write(udc, ast_udc_read(udc, AST_UDC_EP0_CTRL) | EP0_STALL,
		      AST_UDC_EP0_CTRL);
}

static void ast_udc_ep0_handle_setup(struct ast_udc_dev *udc)
{
	struct ast_udc_ep *ep = &udc->ep[0];
	struct ast_udc_request *req;
	struct usb_ctrlrequest crq;
	int req_num = 0;
	int rc = 0;
	u32 reg;

	memcpy_fromio(&crq, udc->creq, sizeof(crq));

	SETUP_DBG(udc, "SETUP packet: %02x/%02x/%04x/%04x/%04x\n",
		  crq.bRequestType, crq.bRequest, le16_to_cpu(crq.wValue),
		  le16_to_cpu(crq.wIndex), le16_to_cpu(crq.wLength));

	/*
	 * Cleanup ep0 request(s) in queue because
	 * there is a new control setup comes.
	 */
	list_for_each_entry(req, &udc->ep[0].queue, queue) {
		req_num++;
		EP_DBG(ep, "there is req %p in ep0 queue !\n", req);
	}

	if (req_num)
		ast_udc_nuke(&udc->ep[0], -ETIMEDOUT);

	udc->ep[0].dir_in = crq.bRequestType & USB_DIR_IN;

	if ((crq.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (crq.bRequest) {
		case USB_REQ_SET_ADDRESS:
			if (ast_udc_read(udc, AST_UDC_STS) & UDC_STS_HIGHSPEED)
				udc->gadget.speed = USB_SPEED_HIGH;
			else
				udc->gadget.speed = USB_SPEED_FULL;

			SETUP_DBG(udc, "set addr: 0x%x\n", crq.wValue);
			reg = ast_udc_read(udc, AST_UDC_CONFIG);
			reg &= ~UDC_CFG_ADDR_MASK;
			reg |= UDC_CFG_SET_ADDR(crq.wValue);
			ast_udc_write(udc, reg, AST_UDC_CONFIG);
			goto req_complete;

		case USB_REQ_CLEAR_FEATURE:
			SETUP_DBG(udc, "ep0: CLEAR FEATURE\n");
			goto req_driver;

		case USB_REQ_SET_FEATURE:
			SETUP_DBG(udc, "ep0: SET FEATURE\n");
			goto req_driver;

		case USB_REQ_GET_STATUS:
			ast_udc_getstatus(udc);
			return;

		default:
			goto req_driver;
		}

	}

req_driver:
	if (udc->driver) {
		SETUP_DBG(udc, "Forwarding %s to gadget...\n",
			  udc->gadget.name);

		spin_unlock(&udc->lock);
		rc = udc->driver->setup(&udc->gadget, &crq);
		spin_lock(&udc->lock);

	} else {
		SETUP_DBG(udc, "No gadget for request !\n");
	}

	if (rc >= 0)
		return;

	/* Stall if gadget failed */
	SETUP_DBG(udc, "Stalling, rc:0x%x\n", rc);
	ast_udc_write(udc, ast_udc_read(udc, AST_UDC_EP0_CTRL) | EP0_STALL,
		      AST_UDC_EP0_CTRL);
	return;

req_complete:
	SETUP_DBG(udc, "ep0: Sending IN status without data\n");
	ast_udc_write(udc, EP0_TX_BUFF_RDY, AST_UDC_EP0_CTRL);
}

static irqreturn_t ast_udc_isr(int irq, void *data)
{
	struct ast_udc_dev *udc = (struct ast_udc_dev *)data;
	struct ast_udc_ep *ep;
	u32 isr, ep_isr;
	int i;

	spin_lock(&udc->lock);

	isr = ast_udc_read(udc, AST_UDC_ISR);
	if (!isr)
		goto done;

	/* Ack interrupts */
	ast_udc_write(udc, isr, AST_UDC_ISR);

	if (isr & UDC_IRQ_BUS_RESET) {
		ISR_DBG(udc, "UDC_IRQ_BUS_RESET\n");
		udc->gadget.speed = USB_SPEED_UNKNOWN;

		ep = &udc->ep[1];
		EP_DBG(ep, "dctrl:0x%x\n",
		       ast_ep_read(ep, AST_UDC_EP_DMA_CTRL));

		if (udc->driver && udc->driver->reset) {
			spin_unlock(&udc->lock);
			udc->driver->reset(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	if (isr & UDC_IRQ_BUS_SUSPEND) {
		ISR_DBG(udc, "UDC_IRQ_BUS_SUSPEND\n");
		udc->suspended_from = udc->gadget.state;
		usb_gadget_set_state(&udc->gadget, USB_STATE_SUSPENDED);

		if (udc->driver && udc->driver->suspend) {
			spin_unlock(&udc->lock);
			udc->driver->suspend(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	if (isr & UDC_IRQ_BUS_RESUME) {
		ISR_DBG(udc, "UDC_IRQ_BUS_RESUME\n");
		usb_gadget_set_state(&udc->gadget, udc->suspended_from);

		if (udc->driver && udc->driver->resume) {
			spin_unlock(&udc->lock);
			udc->driver->resume(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	if (isr & UDC_IRQ_EP0_IN_ACK_STALL) {
		ISR_DBG(udc, "UDC_IRQ_EP0_IN_ACK_STALL\n");
		ast_udc_ep0_in(udc);
	}

	if (isr & UDC_IRQ_EP0_OUT_ACK_STALL) {
		ISR_DBG(udc, "UDC_IRQ_EP0_OUT_ACK_STALL\n");
		ast_udc_ep0_out(udc);
	}

	if (isr & UDC_IRQ_EP0_SETUP) {
		ISR_DBG(udc, "UDC_IRQ_EP0_SETUP\n");
		ast_udc_ep0_handle_setup(udc);
	}

	if (isr & UDC_IRQ_EP_POOL_ACK_STALL) {
		ISR_DBG(udc, "UDC_IRQ_EP_POOL_ACK_STALL\n");
		ep_isr = ast_udc_read(udc, AST_UDC_EP_ACK_ISR);

		/* Ack EP interrupts */
		ast_udc_write(udc, ep_isr, AST_UDC_EP_ACK_ISR);

		/* Handle each EP */
		for (i = 0; i < AST_UDC_NUM_ENDPOINTS - 1; i++) {
			if (ep_isr & (0x1 << i)) {
				ep = &udc->ep[i + 1];
				if (ep->desc_mode)
					ast_udc_epn_handle_desc(udc, i + 1);
				else
					ast_udc_epn_handle(udc, i + 1);
			}
		}
	}

done:
	spin_unlock(&udc->lock);
	return IRQ_HANDLED;
}

static int ast_udc_gadget_getframe(struct usb_gadget *gadget)
{
	struct ast_udc_dev *udc = to_ast_dev(gadget);

	return (ast_udc_read(udc, AST_UDC_STS) >> 16) & 0x7ff;
}

static void ast_udc_wake_work(struct work_struct *work)
{
	struct ast_udc_dev *udc = container_of(work, struct ast_udc_dev,
					       wake_work);
	unsigned long flags;
	u32 ctrl;

	spin_lock_irqsave(&udc->lock, flags);

	UDC_DBG(udc, "Wakeup Host !\n");
	ctrl = ast_udc_read(udc, AST_UDC_FUNC_CTRL);
	ast_udc_write(udc, ctrl | USB_REMOTE_WAKEUP_EN, AST_UDC_FUNC_CTRL);

	spin_unlock_irqrestore(&udc->lock, flags);
}

static void ast_udc_wakeup_all(struct ast_udc_dev *udc)
{
	/*
	 * A device is trying to wake the world, because this
	 * can recurse into the device, we break the call chain
	 * using a work queue
	 */
	schedule_work(&udc->wake_work);
}

static int ast_udc_wakeup(struct usb_gadget *gadget)
{
	struct ast_udc_dev *udc = to_ast_dev(gadget);
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&udc->lock, flags);

	if (!udc->wakeup_en) {
		UDC_DBG(udc, "Remote Wakeup is disabled\n");
		rc = -EINVAL;
		goto err;
	}

	UDC_DBG(udc, "Device initiated wakeup\n");
	ast_udc_wakeup_all(udc);

err:
	spin_unlock_irqrestore(&udc->lock, flags);
	return rc;
}

/*
 * Activate/Deactivate link with host
 */
static int ast_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct ast_udc_dev *udc = to_ast_dev(gadget);
	unsigned long flags;
	u32 ctrl;

	spin_lock_irqsave(&udc->lock, flags);

	UDC_DBG(udc, "is_on: %d\n", is_on);
	if (is_on)
		ctrl = ast_udc_read(udc, AST_UDC_FUNC_CTRL) | USB_UPSTREAM_EN;
	else
		ctrl = ast_udc_read(udc, AST_UDC_FUNC_CTRL) & ~USB_UPSTREAM_EN;

	ast_udc_write(udc, ctrl, AST_UDC_FUNC_CTRL);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int ast_udc_start(struct usb_gadget *gadget,
			 struct usb_gadget_driver *driver)
{
	struct ast_udc_dev *udc = to_ast_dev(gadget);
	struct ast_udc_ep *ep;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&udc->lock, flags);

	UDC_DBG(udc, "\n");
	udc->driver = driver;
	udc->gadget.dev.of_node = udc->pdev->dev.of_node;

	for (i = 0; i < AST_UDC_NUM_ENDPOINTS; i++) {
		ep = &udc->ep[i];
		ep->stopped = 0;
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int ast_udc_stop(struct usb_gadget *gadget)
{
	struct ast_udc_dev *udc = to_ast_dev(gadget);
	unsigned long flags;
	u32 ctrl;

	spin_lock_irqsave(&udc->lock, flags);

	UDC_DBG(udc, "\n");
	ctrl = ast_udc_read(udc, AST_UDC_FUNC_CTRL) & ~USB_UPSTREAM_EN;
	ast_udc_write(udc, ctrl, AST_UDC_FUNC_CTRL);

	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->driver = NULL;

	ast_udc_stop_activity(udc);
	usb_gadget_set_state(&udc->gadget, USB_STATE_NOTATTACHED);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static const struct usb_gadget_ops ast_udc_ops = {
	.get_frame		= ast_udc_gadget_getframe,
	.wakeup			= ast_udc_wakeup,
	.pullup			= ast_udc_pullup,
	.udc_start		= ast_udc_start,
	.udc_stop		= ast_udc_stop,
};

/*
 * Support 1 Control Endpoint.
 * Support multiple programmable endpoints that can be configured to
 * Bulk IN/OUT, Interrupt IN/OUT, and Isochronous IN/OUT type endpoint.
 */
static void ast_udc_init_ep(struct ast_udc_dev *udc)
{
	struct ast_udc_ep *ep;
	int i;

	for (i = 0; i < AST_UDC_NUM_ENDPOINTS; i++) {
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

		ep->ep.ops = &ast_udc_ep_ops;
		ep->udc = udc;

		INIT_LIST_HEAD(&ep->queue);

		if (i == 0) {
			usb_ep_set_maxpacket_limit(&ep->ep,
						   AST_UDC_EP0_MAX_PACKET);
			continue;
		}

		ep->ep_reg = udc->reg + AST_UDC_EP_BASE +
				(AST_UDC_EP_OFFSET * (i - 1));

		ep->epn_buf = udc->ep0_buf + (i * AST_UDC_EP_DMA_SIZE);
		ep->epn_buf_dma = udc->ep0_buf_dma + (i * AST_UDC_EP_DMA_SIZE);
		usb_ep_set_maxpacket_limit(&ep->ep, AST_UDC_EPn_MAX_PACKET);

		ep->descs = ep->epn_buf + AST_UDC_EPn_MAX_PACKET;
		ep->descs_dma = ep->epn_buf_dma + AST_UDC_EPn_MAX_PACKET;
		ep->descs_wptr = 0;

		list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);
	}
}

static void ast_udc_init_dev(struct ast_udc_dev *udc)
{
	INIT_WORK(&udc->wake_work, ast_udc_wake_work);
}

static void ast_udc_init_hw(struct ast_udc_dev *udc)
{
	u32 ctrl;

	/* Enable PHY */
	ctrl = USB_PHY_CLK_EN | USB_PHY_RESET_DIS;
	ast_udc_write(udc, ctrl, AST_UDC_FUNC_CTRL);

	udelay(1);
	ast_udc_write(udc, 0, AST_UDC_DEV_RESET);

	/* Set descriptor ring size */
	if (AST_UDC_DESCS_COUNT == 256) {
		ctrl |= USB_EP_LONG_DESC;
		ast_udc_write(udc, ctrl, AST_UDC_FUNC_CTRL);
	}

	/* Mask & ack all interrupts before installing the handler */
	ast_udc_write(udc, 0, AST_UDC_IER);
	ast_udc_write(udc, UDC_IRQ_ACK_ALL, AST_UDC_ISR);

	/* Enable some interrupts */
	ctrl = UDC_IRQ_EP_POOL_ACK_STALL | UDC_IRQ_BUS_RESUME |
	       UDC_IRQ_BUS_SUSPEND | UDC_IRQ_BUS_RESET |
	       UDC_IRQ_EP0_IN_ACK_STALL | UDC_IRQ_EP0_OUT_ACK_STALL |
	       UDC_IRQ_EP0_SETUP;
	ast_udc_write(udc, ctrl, AST_UDC_IER);

	/* Cleanup and enable ep ACK interrupts */
	ast_udc_write(udc, UDC_IRQ_EP_ACK_ALL, AST_UDC_EP_ACK_IER);
	ast_udc_write(udc, UDC_IRQ_EP_ACK_ALL, AST_UDC_EP_ACK_ISR);

	ast_udc_write(udc, 0, AST_UDC_EP0_CTRL);
}

static void ast_udc_remove(struct platform_device *pdev)
{
	struct ast_udc_dev *udc = platform_get_drvdata(pdev);
	unsigned long flags;
	u32 ctrl;

	usb_del_gadget_udc(&udc->gadget);
	if (udc->driver) {
		/*
		 * This is broken as only some cleanup is skipped, *udev is
		 * freed and the register mapping goes away. Any further usage
		 * probably crashes. Also the device is unbound, so the skipped
		 * cleanup is never catched up later.
		 */
		dev_alert(&pdev->dev,
			  "Driver is busy and still going away. Fasten your seat belts!\n");
		return;
	}

	spin_lock_irqsave(&udc->lock, flags);

	/* Disable upstream port connection */
	ctrl = ast_udc_read(udc, AST_UDC_FUNC_CTRL) & ~USB_UPSTREAM_EN;
	ast_udc_write(udc, ctrl, AST_UDC_FUNC_CTRL);

	clk_disable_unprepare(udc->clk);

	spin_unlock_irqrestore(&udc->lock, flags);

	if (udc->ep0_buf)
		dma_free_coherent(&pdev->dev,
				  AST_UDC_EP_DMA_SIZE * AST_UDC_NUM_ENDPOINTS,
				  udc->ep0_buf,
				  udc->ep0_buf_dma);

	udc->ep0_buf = NULL;
}

static int ast_udc_probe(struct platform_device *pdev)
{
	enum usb_device_speed max_speed;
	struct device *dev = &pdev->dev;
	struct ast_udc_dev *udc;
	int rc;

	udc = devm_kzalloc(&pdev->dev, sizeof(struct ast_udc_dev), GFP_KERNEL);
	if (!udc)
		return -ENOMEM;

	udc->gadget.dev.parent = dev;
	udc->pdev = pdev;
	spin_lock_init(&udc->lock);

	udc->gadget.ops = &ast_udc_ops;
	udc->gadget.ep0 = &udc->ep[0].ep;
	udc->gadget.name = "aspeed-udc";
	udc->gadget.dev.init_name = "gadget";

	udc->reg = devm_platform_ioremap_resource(pdev, 0);
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
		dev_err(&pdev->dev, "Failed to enable clock (0x%x)\n", rc);
		goto err;
	}

	/* Check if we need to limit the HW to USB1 */
	max_speed = usb_get_maximum_speed(&pdev->dev);
	if (max_speed != USB_SPEED_UNKNOWN && max_speed < USB_SPEED_HIGH)
		udc->force_usb1 = true;

	/*
	 * Allocate DMA buffers for all EPs in one chunk
	 */
	udc->ep0_buf = dma_alloc_coherent(&pdev->dev,
					  AST_UDC_EP_DMA_SIZE *
					  AST_UDC_NUM_ENDPOINTS,
					  &udc->ep0_buf_dma, GFP_KERNEL);

	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->gadget.max_speed = USB_SPEED_HIGH;
	udc->creq = udc->reg + AST_UDC_SETUP0;

	/*
	 * Support single stage mode or 32/256 stages descriptor mode.
	 * Set default as Descriptor Mode.
	 */
	udc->desc_mode = AST_UDC_DESC_MODE;

	dev_info(&pdev->dev, "DMA %s\n", udc->desc_mode ?
		 "descriptor mode" : "single mode");

	INIT_LIST_HEAD(&udc->gadget.ep_list);
	INIT_LIST_HEAD(&udc->gadget.ep0->ep_list);

	/* Initialized udc ep */
	ast_udc_init_ep(udc);

	/* Initialized udc device */
	ast_udc_init_dev(udc);

	/* Initialized udc hardware */
	ast_udc_init_hw(udc);

	/* Find interrupt and install handler */
	udc->irq = platform_get_irq(pdev, 0);
	if (udc->irq < 0) {
		rc = udc->irq;
		goto err;
	}

	rc = devm_request_irq(&pdev->dev, udc->irq, ast_udc_isr, 0,
			      KBUILD_MODNAME, udc);
	if (rc) {
		dev_err(&pdev->dev, "Failed to request interrupt\n");
		goto err;
	}

	rc = usb_add_gadget_udc(&pdev->dev, &udc->gadget);
	if (rc) {
		dev_err(&pdev->dev, "Failed to add gadget udc\n");
		goto err;
	}

	dev_info(&pdev->dev, "Initialized udc in USB%s mode\n",
		 udc->force_usb1 ? "1" : "2");

	return 0;

err:
	dev_err(&pdev->dev, "Failed to udc probe, rc:0x%x\n", rc);
	ast_udc_remove(pdev);

	return rc;
}

static const struct of_device_id ast_udc_of_dt_ids[] = {
	{ .compatible = "aspeed,ast2600-udc", },
	{}
};

MODULE_DEVICE_TABLE(of, ast_udc_of_dt_ids);

static struct platform_driver ast_udc_driver = {
	.probe			= ast_udc_probe,
	.remove_new		= ast_udc_remove,
	.driver			= {
		.name			= KBUILD_MODNAME,
		.of_match_table		= ast_udc_of_dt_ids,
	},
};

module_platform_driver(ast_udc_driver);

MODULE_DESCRIPTION("ASPEED UDC driver");
MODULE_AUTHOR("Neal Liu <neal_liu@aspeedtech.com>");
MODULE_LICENSE("GPL");
