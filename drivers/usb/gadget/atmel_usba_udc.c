/*
 * Driver for the Atmel USBA high speed USB device controller
 *
 * Copyright (C) 2005-2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/delay.h>

#include <asm/gpio.h>
#include <asm/arch/board.h>

#include "atmel_usba_udc.h"


static struct usba_udc the_udc;

#ifdef CONFIG_USB_GADGET_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/uaccess.h>

static int queue_dbg_open(struct inode *inode, struct file *file)
{
	struct usba_ep *ep = inode->i_private;
	struct usba_request *req, *req_copy;
	struct list_head *queue_data;

	queue_data = kmalloc(sizeof(*queue_data), GFP_KERNEL);
	if (!queue_data)
		return -ENOMEM;
	INIT_LIST_HEAD(queue_data);

	spin_lock_irq(&ep->udc->lock);
	list_for_each_entry(req, &ep->queue, queue) {
		req_copy = kmalloc(sizeof(*req_copy), GFP_ATOMIC);
		if (!req_copy)
			goto fail;
		memcpy(req_copy, req, sizeof(*req_copy));
		list_add_tail(&req_copy->queue, queue_data);
	}
	spin_unlock_irq(&ep->udc->lock);

	file->private_data = queue_data;
	return 0;

fail:
	spin_unlock_irq(&ep->udc->lock);
	list_for_each_entry_safe(req, req_copy, queue_data, queue) {
		list_del(&req->queue);
		kfree(req);
	}
	kfree(queue_data);
	return -ENOMEM;
}

/*
 * bbbbbbbb llllllll IZS sssss nnnn FDL\n\0
 *
 * b: buffer address
 * l: buffer length
 * I/i: interrupt/no interrupt
 * Z/z: zero/no zero
 * S/s: short ok/short not ok
 * s: status
 * n: nr_packets
 * F/f: submitted/not submitted to FIFO
 * D/d: using/not using DMA
 * L/l: last transaction/not last transaction
 */
static ssize_t queue_dbg_read(struct file *file, char __user *buf,
		size_t nbytes, loff_t *ppos)
{
	struct list_head *queue = file->private_data;
	struct usba_request *req, *tmp_req;
	size_t len, remaining, actual = 0;
	char tmpbuf[38];

	if (!access_ok(VERIFY_WRITE, buf, nbytes))
		return -EFAULT;

	mutex_lock(&file->f_dentry->d_inode->i_mutex);
	list_for_each_entry_safe(req, tmp_req, queue, queue) {
		len = snprintf(tmpbuf, sizeof(tmpbuf),
				"%8p %08x %c%c%c %5d %c%c%c\n",
				req->req.buf, req->req.length,
				req->req.no_interrupt ? 'i' : 'I',
				req->req.zero ? 'Z' : 'z',
				req->req.short_not_ok ? 's' : 'S',
				req->req.status,
				req->submitted ? 'F' : 'f',
				req->using_dma ? 'D' : 'd',
				req->last_transaction ? 'L' : 'l');
		len = min(len, sizeof(tmpbuf));
		if (len > nbytes)
			break;

		list_del(&req->queue);
		kfree(req);

		remaining = __copy_to_user(buf, tmpbuf, len);
		actual += len - remaining;
		if (remaining)
			break;

		nbytes -= len;
		buf += len;
	}
	mutex_unlock(&file->f_dentry->d_inode->i_mutex);

	return actual;
}

static int queue_dbg_release(struct inode *inode, struct file *file)
{
	struct list_head *queue_data = file->private_data;
	struct usba_request *req, *tmp_req;

	list_for_each_entry_safe(req, tmp_req, queue_data, queue) {
		list_del(&req->queue);
		kfree(req);
	}
	kfree(queue_data);
	return 0;
}

static int regs_dbg_open(struct inode *inode, struct file *file)
{
	struct usba_udc *udc;
	unsigned int i;
	u32 *data;
	int ret = -ENOMEM;

	mutex_lock(&inode->i_mutex);
	udc = inode->i_private;
	data = kmalloc(inode->i_size, GFP_KERNEL);
	if (!data)
		goto out;

	spin_lock_irq(&udc->lock);
	for (i = 0; i < inode->i_size / 4; i++)
		data[i] = __raw_readl(udc->regs + i * 4);
	spin_unlock_irq(&udc->lock);

	file->private_data = data;
	ret = 0;

out:
	mutex_unlock(&inode->i_mutex);

	return ret;
}

static ssize_t regs_dbg_read(struct file *file, char __user *buf,
		size_t nbytes, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	int ret;

	mutex_lock(&inode->i_mutex);
	ret = simple_read_from_buffer(buf, nbytes, ppos,
			file->private_data,
			file->f_dentry->d_inode->i_size);
	mutex_unlock(&inode->i_mutex);

	return ret;
}

static int regs_dbg_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

const struct file_operations queue_dbg_fops = {
	.owner		= THIS_MODULE,
	.open		= queue_dbg_open,
	.llseek		= no_llseek,
	.read		= queue_dbg_read,
	.release	= queue_dbg_release,
};

const struct file_operations regs_dbg_fops = {
	.owner		= THIS_MODULE,
	.open		= regs_dbg_open,
	.llseek		= generic_file_llseek,
	.read		= regs_dbg_read,
	.release	= regs_dbg_release,
};

static void usba_ep_init_debugfs(struct usba_udc *udc,
		struct usba_ep *ep)
{
	struct dentry *ep_root;

	ep_root = debugfs_create_dir(ep->ep.name, udc->debugfs_root);
	if (!ep_root)
		goto err_root;
	ep->debugfs_dir = ep_root;

	ep->debugfs_queue = debugfs_create_file("queue", 0400, ep_root,
						ep, &queue_dbg_fops);
	if (!ep->debugfs_queue)
		goto err_queue;

	if (ep->can_dma) {
		ep->debugfs_dma_status
			= debugfs_create_u32("dma_status", 0400, ep_root,
					&ep->last_dma_status);
		if (!ep->debugfs_dma_status)
			goto err_dma_status;
	}
	if (ep_is_control(ep)) {
		ep->debugfs_state
			= debugfs_create_u32("state", 0400, ep_root,
					&ep->state);
		if (!ep->debugfs_state)
			goto err_state;
	}

	return;

err_state:
	if (ep->can_dma)
		debugfs_remove(ep->debugfs_dma_status);
err_dma_status:
	debugfs_remove(ep->debugfs_queue);
err_queue:
	debugfs_remove(ep_root);
err_root:
	dev_err(&ep->udc->pdev->dev,
		"failed to create debugfs directory for %s\n", ep->ep.name);
}

static void usba_ep_cleanup_debugfs(struct usba_ep *ep)
{
	debugfs_remove(ep->debugfs_queue);
	debugfs_remove(ep->debugfs_dma_status);
	debugfs_remove(ep->debugfs_state);
	debugfs_remove(ep->debugfs_dir);
	ep->debugfs_dma_status = NULL;
	ep->debugfs_dir = NULL;
}

static void usba_init_debugfs(struct usba_udc *udc)
{
	struct dentry *root, *regs;
	struct resource *regs_resource;

	root = debugfs_create_dir(udc->gadget.name, NULL);
	if (IS_ERR(root) || !root)
		goto err_root;
	udc->debugfs_root = root;

	regs = debugfs_create_file("regs", 0400, root, udc, &regs_dbg_fops);
	if (!regs)
		goto err_regs;

	regs_resource = platform_get_resource(udc->pdev, IORESOURCE_MEM,
				CTRL_IOMEM_ID);
	regs->d_inode->i_size = regs_resource->end - regs_resource->start + 1;
	udc->debugfs_regs = regs;

	usba_ep_init_debugfs(udc, to_usba_ep(udc->gadget.ep0));

	return;

err_regs:
	debugfs_remove(root);
err_root:
	udc->debugfs_root = NULL;
	dev_err(&udc->pdev->dev, "debugfs is not available\n");
}

static void usba_cleanup_debugfs(struct usba_udc *udc)
{
	usba_ep_cleanup_debugfs(to_usba_ep(udc->gadget.ep0));
	debugfs_remove(udc->debugfs_regs);
	debugfs_remove(udc->debugfs_root);
	udc->debugfs_regs = NULL;
	udc->debugfs_root = NULL;
}
#else
static inline void usba_ep_init_debugfs(struct usba_udc *udc,
					 struct usba_ep *ep)
{

}

static inline void usba_ep_cleanup_debugfs(struct usba_ep *ep)
{

}

static inline void usba_init_debugfs(struct usba_udc *udc)
{

}

static inline void usba_cleanup_debugfs(struct usba_udc *udc)
{

}
#endif

static int vbus_is_present(struct usba_udc *udc)
{
	if (udc->vbus_pin != -1)
		return gpio_get_value(udc->vbus_pin);

	/* No Vbus detection: Assume always present */
	return 1;
}

static void copy_to_fifo(void __iomem *fifo, const void *buf, int len)
{
	unsigned long tmp;

	DBG(DBG_FIFO, "copy to FIFO (len %d):\n", len);
	for (; len > 0; len -= 4, buf += 4, fifo += 4) {
		tmp = *(unsigned long *)buf;
		if (len >= 4) {
			DBG(DBG_FIFO, "  -> %08lx\n", tmp);
			__raw_writel(tmp, fifo);
		} else {
			do {
				DBG(DBG_FIFO, "  -> %02lx\n", tmp >> 24);
				__raw_writeb(tmp >> 24, fifo);
				fifo++;
				tmp <<= 8;
			} while (--len);
			break;
		}
	}
}

static void copy_from_fifo(void *buf, void __iomem *fifo, int len)
{
	union {
		unsigned long *w;
		unsigned char *b;
	} p;
	unsigned long tmp;

	DBG(DBG_FIFO, "copy from FIFO (len %d):\n", len);
	for (p.w = buf; len > 0; len -= 4, p.w++, fifo += 4) {
		if (len >= 4) {
			tmp = __raw_readl(fifo);
			*p.w = tmp;
			DBG(DBG_FIFO, "  -> %08lx\n", tmp);
		} else {
			do {
				tmp = __raw_readb(fifo);
				*p.b = tmp;
				DBG(DBG_FIFO, " -> %02lx\n", tmp);
				fifo++, p.b++;
			} while (--len);
		}
	}
}

static void next_fifo_transaction(struct usba_ep *ep, struct usba_request *req)
{
	unsigned int transaction_len;

	transaction_len = req->req.length - req->req.actual;
	req->last_transaction = 1;
	if (transaction_len > ep->ep.maxpacket) {
		transaction_len = ep->ep.maxpacket;
		req->last_transaction = 0;
	} else if (transaction_len == ep->ep.maxpacket && req->req.zero)
		req->last_transaction = 0;

	DBG(DBG_QUEUE, "%s: submit_transaction, req %p (length %d)%s\n",
		ep->ep.name, req, transaction_len,
		req->last_transaction ? ", done" : "");

	copy_to_fifo(ep->fifo, req->req.buf + req->req.actual, transaction_len);
	usba_ep_writel(ep, SET_STA, USBA_TX_PK_RDY);
	req->req.actual += transaction_len;
}

static void submit_request(struct usba_ep *ep, struct usba_request *req)
{
	DBG(DBG_QUEUE, "%s: submit_request: req %p (length %d)\n",
		ep->ep.name, req, req->req.length);

	req->req.actual = 0;
	req->submitted = 1;

	if (req->using_dma) {
		if (req->req.length == 0) {
			usba_ep_writel(ep, CTL_ENB, USBA_TX_PK_RDY);
			return;
		}

		if (req->req.zero)
			usba_ep_writel(ep, CTL_ENB, USBA_SHORT_PACKET);
		else
			usba_ep_writel(ep, CTL_DIS, USBA_SHORT_PACKET);

		usba_dma_writel(ep, ADDRESS, req->req.dma);
		usba_dma_writel(ep, CONTROL, req->ctrl);
	} else {
		next_fifo_transaction(ep, req);
		if (req->last_transaction) {
			usba_ep_writel(ep, CTL_DIS, USBA_TX_PK_RDY);
			usba_ep_writel(ep, CTL_ENB, USBA_TX_COMPLETE);
		} else {
			usba_ep_writel(ep, CTL_DIS, USBA_TX_COMPLETE);
			usba_ep_writel(ep, CTL_ENB, USBA_TX_PK_RDY);
		}
	}
}

static void submit_next_request(struct usba_ep *ep)
{
	struct usba_request *req;

	if (list_empty(&ep->queue)) {
		usba_ep_writel(ep, CTL_DIS, USBA_TX_PK_RDY | USBA_RX_BK_RDY);
		return;
	}

	req = list_entry(ep->queue.next, struct usba_request, queue);
	if (!req->submitted)
		submit_request(ep, req);
}

static void send_status(struct usba_udc *udc, struct usba_ep *ep)
{
	ep->state = STATUS_STAGE_IN;
	usba_ep_writel(ep, SET_STA, USBA_TX_PK_RDY);
	usba_ep_writel(ep, CTL_ENB, USBA_TX_COMPLETE);
}

static void receive_data(struct usba_ep *ep)
{
	struct usba_udc *udc = ep->udc;
	struct usba_request *req;
	unsigned long status;
	unsigned int bytecount, nr_busy;
	int is_complete = 0;

	status = usba_ep_readl(ep, STA);
	nr_busy = USBA_BFEXT(BUSY_BANKS, status);

	DBG(DBG_QUEUE, "receive data: nr_busy=%u\n", nr_busy);

	while (nr_busy > 0) {
		if (list_empty(&ep->queue)) {
			usba_ep_writel(ep, CTL_DIS, USBA_RX_BK_RDY);
			break;
		}
		req = list_entry(ep->queue.next,
				 struct usba_request, queue);

		bytecount = USBA_BFEXT(BYTE_COUNT, status);

		if (status & (1 << 31))
			is_complete = 1;
		if (req->req.actual + bytecount >= req->req.length) {
			is_complete = 1;
			bytecount = req->req.length - req->req.actual;
		}

		copy_from_fifo(req->req.buf + req->req.actual,
				ep->fifo, bytecount);
		req->req.actual += bytecount;

		usba_ep_writel(ep, CLR_STA, USBA_RX_BK_RDY);

		if (is_complete) {
			DBG(DBG_QUEUE, "%s: request done\n", ep->ep.name);
			req->req.status = 0;
			list_del_init(&req->queue);
			usba_ep_writel(ep, CTL_DIS, USBA_RX_BK_RDY);
			spin_unlock(&udc->lock);
			req->req.complete(&ep->ep, &req->req);
			spin_lock(&udc->lock);
		}

		status = usba_ep_readl(ep, STA);
		nr_busy = USBA_BFEXT(BUSY_BANKS, status);

		if (is_complete && ep_is_control(ep)) {
			send_status(udc, ep);
			break;
		}
	}
}

static void
request_complete(struct usba_ep *ep, struct usba_request *req, int status)
{
	struct usba_udc *udc = ep->udc;

	WARN_ON(!list_empty(&req->queue));

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;

	if (req->mapped) {
		dma_unmap_single(
			&udc->pdev->dev, req->req.dma, req->req.length,
			ep->is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
	}

	DBG(DBG_GADGET | DBG_REQ,
		"%s: req %p complete: status %d, actual %u\n",
		ep->ep.name, req, req->req.status, req->req.actual);

	spin_unlock(&udc->lock);
	req->req.complete(&ep->ep, &req->req);
	spin_lock(&udc->lock);
}

static void
request_complete_list(struct usba_ep *ep, struct list_head *list, int status)
{
	struct usba_request *req, *tmp_req;

	list_for_each_entry_safe(req, tmp_req, list, queue) {
		list_del_init(&req->queue);
		request_complete(ep, req, status);
	}
}

static int
usba_ep_enable(struct usb_ep *_ep, const struct usb_endpoint_descriptor *desc)
{
	struct usba_ep *ep = to_usba_ep(_ep);
	struct usba_udc *udc = ep->udc;
	unsigned long flags, ept_cfg, maxpacket;
	unsigned int nr_trans;

	DBG(DBG_GADGET, "%s: ep_enable: desc=%p\n", ep->ep.name, desc);

	maxpacket = le16_to_cpu(desc->wMaxPacketSize) & 0x7ff;

	if (((desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK) != ep->index)
			|| ep->index == 0
			|| desc->bDescriptorType != USB_DT_ENDPOINT
			|| maxpacket == 0
			|| maxpacket > ep->fifo_size) {
		DBG(DBG_ERR, "ep_enable: Invalid argument");
		return -EINVAL;
	}

	ep->is_isoc = 0;
	ep->is_in = 0;

	if (maxpacket <= 8)
		ept_cfg = USBA_BF(EPT_SIZE, USBA_EPT_SIZE_8);
	else
		/* LSB is bit 1, not 0 */
		ept_cfg = USBA_BF(EPT_SIZE, fls(maxpacket - 1) - 3);

	DBG(DBG_HW, "%s: EPT_SIZE = %lu (maxpacket = %lu)\n",
			ep->ep.name, ept_cfg, maxpacket);

	if ((desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) {
		ep->is_in = 1;
		ept_cfg |= USBA_EPT_DIR_IN;
	}

	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_CONTROL:
		ept_cfg |= USBA_BF(EPT_TYPE, USBA_EPT_TYPE_CONTROL);
		ept_cfg |= USBA_BF(BK_NUMBER, USBA_BK_NUMBER_ONE);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (!ep->can_isoc) {
			DBG(DBG_ERR, "ep_enable: %s is not isoc capable\n",
					ep->ep.name);
			return -EINVAL;
		}

		/*
		 * Bits 11:12 specify number of _additional_
		 * transactions per microframe.
		 */
		nr_trans = ((le16_to_cpu(desc->wMaxPacketSize) >> 11) & 3) + 1;
		if (nr_trans > 3)
			return -EINVAL;

		ep->is_isoc = 1;
		ept_cfg |= USBA_BF(EPT_TYPE, USBA_EPT_TYPE_ISO);

		/*
		 * Do triple-buffering on high-bandwidth iso endpoints.
		 */
		if (nr_trans > 1 && ep->nr_banks == 3)
			ept_cfg |= USBA_BF(BK_NUMBER, USBA_BK_NUMBER_TRIPLE);
		else
			ept_cfg |= USBA_BF(BK_NUMBER, USBA_BK_NUMBER_DOUBLE);
		ept_cfg |= USBA_BF(NB_TRANS, nr_trans);
		break;
	case USB_ENDPOINT_XFER_BULK:
		ept_cfg |= USBA_BF(EPT_TYPE, USBA_EPT_TYPE_BULK);
		ept_cfg |= USBA_BF(BK_NUMBER, USBA_BK_NUMBER_DOUBLE);
		break;
	case USB_ENDPOINT_XFER_INT:
		ept_cfg |= USBA_BF(EPT_TYPE, USBA_EPT_TYPE_INT);
		ept_cfg |= USBA_BF(BK_NUMBER, USBA_BK_NUMBER_DOUBLE);
		break;
	}

	spin_lock_irqsave(&ep->udc->lock, flags);

	if (ep->desc) {
		spin_unlock_irqrestore(&ep->udc->lock, flags);
		DBG(DBG_ERR, "ep%d already enabled\n", ep->index);
		return -EBUSY;
	}

	ep->desc = desc;
	ep->ep.maxpacket = maxpacket;

	usba_ep_writel(ep, CFG, ept_cfg);
	usba_ep_writel(ep, CTL_ENB, USBA_EPT_ENABLE);

	if (ep->can_dma) {
		u32 ctrl;

		usba_writel(udc, INT_ENB,
				(usba_readl(udc, INT_ENB)
					| USBA_BF(EPT_INT, 1 << ep->index)
					| USBA_BF(DMA_INT, 1 << ep->index)));
		ctrl = USBA_AUTO_VALID | USBA_INTDIS_DMA;
		usba_ep_writel(ep, CTL_ENB, ctrl);
	} else {
		usba_writel(udc, INT_ENB,
				(usba_readl(udc, INT_ENB)
					| USBA_BF(EPT_INT, 1 << ep->index)));
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	DBG(DBG_HW, "EPT_CFG%d after init: %#08lx\n", ep->index,
			(unsigned long)usba_ep_readl(ep, CFG));
	DBG(DBG_HW, "INT_ENB after init: %#08lx\n",
			(unsigned long)usba_readl(udc, INT_ENB));

	return 0;
}

static int usba_ep_disable(struct usb_ep *_ep)
{
	struct usba_ep *ep = to_usba_ep(_ep);
	struct usba_udc *udc = ep->udc;
	LIST_HEAD(req_list);
	unsigned long flags;

	DBG(DBG_GADGET, "ep_disable: %s\n", ep->ep.name);

	spin_lock_irqsave(&udc->lock, flags);

	if (!ep->desc) {
		spin_unlock_irqrestore(&udc->lock, flags);
		DBG(DBG_ERR, "ep_disable: %s not enabled\n", ep->ep.name);
		return -EINVAL;
	}
	ep->desc = NULL;

	list_splice_init(&ep->queue, &req_list);
	if (ep->can_dma) {
		usba_dma_writel(ep, CONTROL, 0);
		usba_dma_writel(ep, ADDRESS, 0);
		usba_dma_readl(ep, STATUS);
	}
	usba_ep_writel(ep, CTL_DIS, USBA_EPT_ENABLE);
	usba_writel(udc, INT_ENB,
			usba_readl(udc, INT_ENB)
			& ~USBA_BF(EPT_INT, 1 << ep->index));

	request_complete_list(ep, &req_list, -ESHUTDOWN);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static struct usb_request *
usba_ep_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct usba_request *req;

	DBG(DBG_GADGET, "ep_alloc_request: %p, 0x%x\n", _ep, gfp_flags);

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);
	req->req.dma = DMA_ADDR_INVALID;

	return &req->req;
}

static void
usba_ep_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct usba_request *req = to_usba_req(_req);

	DBG(DBG_GADGET, "ep_free_request: %p, %p\n", _ep, _req);

	kfree(req);
}

static int queue_dma(struct usba_udc *udc, struct usba_ep *ep,
		struct usba_request *req, gfp_t gfp_flags)
{
	unsigned long flags;
	int ret;

	DBG(DBG_DMA, "%s: req l/%u d/%08x %c%c%c\n",
		ep->ep.name, req->req.length, req->req.dma,
		req->req.zero ? 'Z' : 'z',
		req->req.short_not_ok ? 'S' : 's',
		req->req.no_interrupt ? 'I' : 'i');

	if (req->req.length > 0x10000) {
		/* Lengths from 0 to 65536 (inclusive) are supported */
		DBG(DBG_ERR, "invalid request length %u\n", req->req.length);
		return -EINVAL;
	}

	req->using_dma = 1;

	if (req->req.dma == DMA_ADDR_INVALID) {
		req->req.dma = dma_map_single(
			&udc->pdev->dev, req->req.buf, req->req.length,
			ep->is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		req->mapped = 1;
	} else {
		dma_sync_single_for_device(
			&udc->pdev->dev, req->req.dma, req->req.length,
			ep->is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		req->mapped = 0;
	}

	req->ctrl = USBA_BF(DMA_BUF_LEN, req->req.length)
			| USBA_DMA_CH_EN | USBA_DMA_END_BUF_IE
			| USBA_DMA_END_TR_EN | USBA_DMA_END_TR_IE;

	if (ep->is_in)
		req->ctrl |= USBA_DMA_END_BUF_EN;

	/*
	 * Add this request to the queue and submit for DMA if
	 * possible. Check if we're still alive first -- we may have
	 * received a reset since last time we checked.
	 */
	ret = -ESHUTDOWN;
	spin_lock_irqsave(&udc->lock, flags);
	if (ep->desc) {
		if (list_empty(&ep->queue))
			submit_request(ep, req);

		list_add_tail(&req->queue, &ep->queue);
		ret = 0;
	}
	spin_unlock_irqrestore(&udc->lock, flags);

	return ret;
}

static int
usba_ep_queue(struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct usba_request *req = to_usba_req(_req);
	struct usba_ep *ep = to_usba_ep(_ep);
	struct usba_udc *udc = ep->udc;
	unsigned long flags;
	int ret;

	DBG(DBG_GADGET | DBG_QUEUE | DBG_REQ, "%s: queue req %p, len %u\n",
			ep->ep.name, req, _req->length);

	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN || !ep->desc)
		return -ESHUTDOWN;

	req->submitted = 0;
	req->using_dma = 0;
	req->last_transaction = 0;

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	if (ep->can_dma)
		return queue_dma(udc, ep, req, gfp_flags);

	/* May have received a reset since last time we checked */
	ret = -ESHUTDOWN;
	spin_lock_irqsave(&udc->lock, flags);
	if (ep->desc) {
		list_add_tail(&req->queue, &ep->queue);

		if (ep->is_in || (ep_is_control(ep)
				&& (ep->state == DATA_STAGE_IN
					|| ep->state == STATUS_STAGE_IN)))
			usba_ep_writel(ep, CTL_ENB, USBA_TX_PK_RDY);
		else
			usba_ep_writel(ep, CTL_ENB, USBA_RX_BK_RDY);
		ret = 0;
	}
	spin_unlock_irqrestore(&udc->lock, flags);

	return ret;
}

static void
usba_update_req(struct usba_ep *ep, struct usba_request *req, u32 status)
{
	req->req.actual = req->req.length - USBA_BFEXT(DMA_BUF_LEN, status);
}

static int stop_dma(struct usba_ep *ep, u32 *pstatus)
{
	unsigned int timeout;
	u32 status;

	/*
	 * Stop the DMA controller. When writing both CH_EN
	 * and LINK to 0, the other bits are not affected.
	 */
	usba_dma_writel(ep, CONTROL, 0);

	/* Wait for the FIFO to empty */
	for (timeout = 40; timeout; --timeout) {
		status = usba_dma_readl(ep, STATUS);
		if (!(status & USBA_DMA_CH_EN))
			break;
		udelay(1);
	}

	if (pstatus)
		*pstatus = status;

	if (timeout == 0) {
		dev_err(&ep->udc->pdev->dev,
			"%s: timed out waiting for DMA FIFO to empty\n",
			ep->ep.name);
		return -ETIMEDOUT;
	}

	return 0;
}

static int usba_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct usba_ep *ep = to_usba_ep(_ep);
	struct usba_udc *udc = ep->udc;
	struct usba_request *req = to_usba_req(_req);
	unsigned long flags;
	u32 status;

	DBG(DBG_GADGET | DBG_QUEUE, "ep_dequeue: %s, req %p\n",
			ep->ep.name, req);

	spin_lock_irqsave(&udc->lock, flags);

	if (req->using_dma) {
		/*
		 * If this request is currently being transferred,
		 * stop the DMA controller and reset the FIFO.
		 */
		if (ep->queue.next == &req->queue) {
			status = usba_dma_readl(ep, STATUS);
			if (status & USBA_DMA_CH_EN)
				stop_dma(ep, &status);

#ifdef CONFIG_USB_GADGET_DEBUG_FS
			ep->last_dma_status = status;
#endif

			usba_writel(udc, EPT_RST, 1 << ep->index);

			usba_update_req(ep, req, status);
		}
	}

	/*
	 * Errors should stop the queue from advancing until the
	 * completion function returns.
	 */
	list_del_init(&req->queue);

	request_complete(ep, req, -ECONNRESET);

	/* Process the next request if any */
	submit_next_request(ep);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int usba_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct usba_ep *ep = to_usba_ep(_ep);
	struct usba_udc *udc = ep->udc;
	unsigned long flags;
	int ret = 0;

	DBG(DBG_GADGET, "endpoint %s: %s HALT\n", ep->ep.name,
			value ? "set" : "clear");

	if (!ep->desc) {
		DBG(DBG_ERR, "Attempted to halt uninitialized ep %s\n",
				ep->ep.name);
		return -ENODEV;
	}
	if (ep->is_isoc) {
		DBG(DBG_ERR, "Attempted to halt isochronous ep %s\n",
				ep->ep.name);
		return -ENOTTY;
	}

	spin_lock_irqsave(&udc->lock, flags);

	/*
	 * We can't halt IN endpoints while there are still data to be
	 * transferred
	 */
	if (!list_empty(&ep->queue)
			|| ((value && ep->is_in && (usba_ep_readl(ep, STA)
					& USBA_BF(BUSY_BANKS, -1L))))) {
		ret = -EAGAIN;
	} else {
		if (value)
			usba_ep_writel(ep, SET_STA, USBA_FORCE_STALL);
		else
			usba_ep_writel(ep, CLR_STA,
					USBA_FORCE_STALL | USBA_TOGGLE_CLR);
		usba_ep_readl(ep, STA);
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return ret;
}

static int usba_ep_fifo_status(struct usb_ep *_ep)
{
	struct usba_ep *ep = to_usba_ep(_ep);

	return USBA_BFEXT(BYTE_COUNT, usba_ep_readl(ep, STA));
}

static void usba_ep_fifo_flush(struct usb_ep *_ep)
{
	struct usba_ep *ep = to_usba_ep(_ep);
	struct usba_udc *udc = ep->udc;

	usba_writel(udc, EPT_RST, 1 << ep->index);
}

static const struct usb_ep_ops usba_ep_ops = {
	.enable		= usba_ep_enable,
	.disable	= usba_ep_disable,
	.alloc_request	= usba_ep_alloc_request,
	.free_request	= usba_ep_free_request,
	.queue		= usba_ep_queue,
	.dequeue	= usba_ep_dequeue,
	.set_halt	= usba_ep_set_halt,
	.fifo_status	= usba_ep_fifo_status,
	.fifo_flush	= usba_ep_fifo_flush,
};

static int usba_udc_get_frame(struct usb_gadget *gadget)
{
	struct usba_udc *udc = to_usba_udc(gadget);

	return USBA_BFEXT(FRAME_NUMBER, usba_readl(udc, FNUM));
}

static int usba_udc_wakeup(struct usb_gadget *gadget)
{
	struct usba_udc *udc = to_usba_udc(gadget);
	unsigned long flags;
	u32 ctrl;
	int ret = -EINVAL;

	spin_lock_irqsave(&udc->lock, flags);
	if (udc->devstatus & (1 << USB_DEVICE_REMOTE_WAKEUP)) {
		ctrl = usba_readl(udc, CTRL);
		usba_writel(udc, CTRL, ctrl | USBA_REMOTE_WAKE_UP);
		ret = 0;
	}
	spin_unlock_irqrestore(&udc->lock, flags);

	return ret;
}

static int
usba_udc_set_selfpowered(struct usb_gadget *gadget, int is_selfpowered)
{
	struct usba_udc *udc = to_usba_udc(gadget);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);
	if (is_selfpowered)
		udc->devstatus |= 1 << USB_DEVICE_SELF_POWERED;
	else
		udc->devstatus &= ~(1 << USB_DEVICE_SELF_POWERED);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static const struct usb_gadget_ops usba_udc_ops = {
	.get_frame		= usba_udc_get_frame,
	.wakeup			= usba_udc_wakeup,
	.set_selfpowered	= usba_udc_set_selfpowered,
};

#define EP(nam, idx, maxpkt, maxbk, dma, isoc)			\
{								\
	.ep	= {						\
		.ops		= &usba_ep_ops,			\
		.name		= nam,				\
		.maxpacket	= maxpkt,			\
	},							\
	.udc		= &the_udc,				\
	.queue		= LIST_HEAD_INIT(usba_ep[idx].queue),	\
	.fifo_size	= maxpkt,				\
	.nr_banks	= maxbk,				\
	.index		= idx,					\
	.can_dma	= dma,					\
	.can_isoc	= isoc,					\
}

static struct usba_ep usba_ep[] = {
	EP("ep0", 0, 64, 1, 0, 0),
	EP("ep1in-bulk", 1, 512, 2, 1, 1),
	EP("ep2out-bulk", 2, 512, 2, 1, 1),
	EP("ep3in-int", 3, 64, 3, 1, 0),
	EP("ep4out-int", 4, 64, 3, 1, 0),
	EP("ep5in-iso", 5, 1024, 3, 1, 1),
	EP("ep6out-iso", 6, 1024, 3, 1, 1),
};
#undef EP

static struct usb_endpoint_descriptor usba_ep0_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0,
	.bmAttributes = USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize = __constant_cpu_to_le16(64),
	/* FIXME: I have no idea what to put here */
	.bInterval = 1,
};

static void nop_release(struct device *dev)
{

}

static struct usba_udc the_udc = {
	.gadget	= {
		.ops		= &usba_udc_ops,
		.ep0		= &usba_ep[0].ep,
		.ep_list	= LIST_HEAD_INIT(the_udc.gadget.ep_list),
		.is_dualspeed	= 1,
		.name		= "atmel_usba_udc",
		.dev	= {
			.bus_id		= "gadget",
			.release	= nop_release,
		},
	},

	.lock	= SPIN_LOCK_UNLOCKED,
};

/*
 * Called with interrupts disabled and udc->lock held.
 */
static void reset_all_endpoints(struct usba_udc *udc)
{
	struct usba_ep *ep;
	struct usba_request *req, *tmp_req;

	usba_writel(udc, EPT_RST, ~0UL);

	ep = to_usba_ep(udc->gadget.ep0);
	list_for_each_entry_safe(req, tmp_req, &ep->queue, queue) {
		list_del_init(&req->queue);
		request_complete(ep, req, -ECONNRESET);
	}

	list_for_each_entry(ep, &udc->gadget.ep_list, ep.ep_list) {
		if (ep->desc) {
			spin_unlock(&udc->lock);
			usba_ep_disable(&ep->ep);
			spin_lock(&udc->lock);
		}
	}
}

static struct usba_ep *get_ep_by_addr(struct usba_udc *udc, u16 wIndex)
{
	struct usba_ep *ep;

	if ((wIndex & USB_ENDPOINT_NUMBER_MASK) == 0)
		return to_usba_ep(udc->gadget.ep0);

	list_for_each_entry (ep, &udc->gadget.ep_list, ep.ep_list) {
		u8 bEndpointAddress;

		if (!ep->desc)
			continue;
		bEndpointAddress = ep->desc->bEndpointAddress;
		if ((wIndex ^ bEndpointAddress) & USB_DIR_IN)
			continue;
		if ((bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)
				== (wIndex & USB_ENDPOINT_NUMBER_MASK))
			return ep;
	}

	return NULL;
}

/* Called with interrupts disabled and udc->lock held */
static inline void set_protocol_stall(struct usba_udc *udc, struct usba_ep *ep)
{
	usba_ep_writel(ep, SET_STA, USBA_FORCE_STALL);
	ep->state = WAIT_FOR_SETUP;
}

static inline int is_stalled(struct usba_udc *udc, struct usba_ep *ep)
{
	if (usba_ep_readl(ep, STA) & USBA_FORCE_STALL)
		return 1;
	return 0;
}

static inline void set_address(struct usba_udc *udc, unsigned int addr)
{
	u32 regval;

	DBG(DBG_BUS, "setting address %u...\n", addr);
	regval = usba_readl(udc, CTRL);
	regval = USBA_BFINS(DEV_ADDR, addr, regval);
	usba_writel(udc, CTRL, regval);
}

static int do_test_mode(struct usba_udc *udc)
{
	static const char test_packet_buffer[] = {
		/* JKJKJKJK * 9 */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* JJKKJJKK * 8 */
		0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
		/* JJKKJJKK * 8 */
		0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
		/* JJJJJJJKKKKKKK * 8 */
		0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		/* JJJJJJJK * 8 */
		0x7F, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD,
		/* {JKKKKKKK * 10}, JK */
		0xFC, 0x7E, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0x7E
	};
	struct usba_ep *ep;
	struct device *dev = &udc->pdev->dev;
	int test_mode;

	test_mode = udc->test_mode;

	/* Start from a clean slate */
	reset_all_endpoints(udc);

	switch (test_mode) {
	case 0x0100:
		/* Test_J */
		usba_writel(udc, TST, USBA_TST_J_MODE);
		dev_info(dev, "Entering Test_J mode...\n");
		break;
	case 0x0200:
		/* Test_K */
		usba_writel(udc, TST, USBA_TST_K_MODE);
		dev_info(dev, "Entering Test_K mode...\n");
		break;
	case 0x0300:
		/*
		 * Test_SE0_NAK: Force high-speed mode and set up ep0
		 * for Bulk IN transfers
		 */
		ep = &usba_ep[0];
		usba_writel(udc, TST,
				USBA_BF(SPEED_CFG, USBA_SPEED_CFG_FORCE_HIGH));
		usba_ep_writel(ep, CFG,
				USBA_BF(EPT_SIZE, USBA_EPT_SIZE_64)
				| USBA_EPT_DIR_IN
				| USBA_BF(EPT_TYPE, USBA_EPT_TYPE_BULK)
				| USBA_BF(BK_NUMBER, 1));
		if (!(usba_ep_readl(ep, CFG) & USBA_EPT_MAPPED)) {
			set_protocol_stall(udc, ep);
			dev_err(dev, "Test_SE0_NAK: ep0 not mapped\n");
		} else {
			usba_ep_writel(ep, CTL_ENB, USBA_EPT_ENABLE);
			dev_info(dev, "Entering Test_SE0_NAK mode...\n");
		}
		break;
	case 0x0400:
		/* Test_Packet */
		ep = &usba_ep[0];
		usba_ep_writel(ep, CFG,
				USBA_BF(EPT_SIZE, USBA_EPT_SIZE_64)
				| USBA_EPT_DIR_IN
				| USBA_BF(EPT_TYPE, USBA_EPT_TYPE_BULK)
				| USBA_BF(BK_NUMBER, 1));
		if (!(usba_ep_readl(ep, CFG) & USBA_EPT_MAPPED)) {
			set_protocol_stall(udc, ep);
			dev_err(dev, "Test_Packet: ep0 not mapped\n");
		} else {
			usba_ep_writel(ep, CTL_ENB, USBA_EPT_ENABLE);
			usba_writel(udc, TST, USBA_TST_PKT_MODE);
			copy_to_fifo(ep->fifo, test_packet_buffer,
					sizeof(test_packet_buffer));
			usba_ep_writel(ep, SET_STA, USBA_TX_PK_RDY);
			dev_info(dev, "Entering Test_Packet mode...\n");
		}
		break;
	default:
		dev_err(dev, "Invalid test mode: 0x%04x\n", test_mode);
		return -EINVAL;
	}

	return 0;
}

/* Avoid overly long expressions */
static inline bool feature_is_dev_remote_wakeup(struct usb_ctrlrequest *crq)
{
	if (crq->wValue == __constant_cpu_to_le16(USB_DEVICE_REMOTE_WAKEUP))
		return true;
	return false;
}

static inline bool feature_is_dev_test_mode(struct usb_ctrlrequest *crq)
{
	if (crq->wValue == __constant_cpu_to_le16(USB_DEVICE_TEST_MODE))
		return true;
	return false;
}

static inline bool feature_is_ep_halt(struct usb_ctrlrequest *crq)
{
	if (crq->wValue == __constant_cpu_to_le16(USB_ENDPOINT_HALT))
		return true;
	return false;
}

static int handle_ep0_setup(struct usba_udc *udc, struct usba_ep *ep,
		struct usb_ctrlrequest *crq)
{
	int retval = 0;;

	switch (crq->bRequest) {
	case USB_REQ_GET_STATUS: {
		u16 status;

		if (crq->bRequestType == (USB_DIR_IN | USB_RECIP_DEVICE)) {
			status = cpu_to_le16(udc->devstatus);
		} else if (crq->bRequestType
				== (USB_DIR_IN | USB_RECIP_INTERFACE)) {
			status = __constant_cpu_to_le16(0);
		} else if (crq->bRequestType
				== (USB_DIR_IN | USB_RECIP_ENDPOINT)) {
			struct usba_ep *target;

			target = get_ep_by_addr(udc, le16_to_cpu(crq->wIndex));
			if (!target)
				goto stall;

			status = 0;
			if (is_stalled(udc, target))
				status |= __constant_cpu_to_le16(1);
		} else
			goto delegate;

		/* Write directly to the FIFO. No queueing is done. */
		if (crq->wLength != __constant_cpu_to_le16(sizeof(status)))
			goto stall;
		ep->state = DATA_STAGE_IN;
		__raw_writew(status, ep->fifo);
		usba_ep_writel(ep, SET_STA, USBA_TX_PK_RDY);
		break;
	}

	case USB_REQ_CLEAR_FEATURE: {
		if (crq->bRequestType == USB_RECIP_DEVICE) {
			if (feature_is_dev_remote_wakeup(crq))
				udc->devstatus
					&= ~(1 << USB_DEVICE_REMOTE_WAKEUP);
			else
				/* Can't CLEAR_FEATURE TEST_MODE */
				goto stall;
		} else if (crq->bRequestType == USB_RECIP_ENDPOINT) {
			struct usba_ep *target;

			if (crq->wLength != __constant_cpu_to_le16(0)
					|| !feature_is_ep_halt(crq))
				goto stall;
			target = get_ep_by_addr(udc, le16_to_cpu(crq->wIndex));
			if (!target)
				goto stall;

			usba_ep_writel(target, CLR_STA, USBA_FORCE_STALL);
			if (target->index != 0)
				usba_ep_writel(target, CLR_STA,
						USBA_TOGGLE_CLR);
		} else {
			goto delegate;
		}

		send_status(udc, ep);
		break;
	}

	case USB_REQ_SET_FEATURE: {
		if (crq->bRequestType == USB_RECIP_DEVICE) {
			if (feature_is_dev_test_mode(crq)) {
				send_status(udc, ep);
				ep->state = STATUS_STAGE_TEST;
				udc->test_mode = le16_to_cpu(crq->wIndex);
				return 0;
			} else if (feature_is_dev_remote_wakeup(crq)) {
				udc->devstatus |= 1 << USB_DEVICE_REMOTE_WAKEUP;
			} else {
				goto stall;
			}
		} else if (crq->bRequestType == USB_RECIP_ENDPOINT) {
			struct usba_ep *target;

			if (crq->wLength != __constant_cpu_to_le16(0)
					|| !feature_is_ep_halt(crq))
				goto stall;

			target = get_ep_by_addr(udc, le16_to_cpu(crq->wIndex));
			if (!target)
				goto stall;

			usba_ep_writel(target, SET_STA, USBA_FORCE_STALL);
		} else
			goto delegate;

		send_status(udc, ep);
		break;
	}

	case USB_REQ_SET_ADDRESS:
		if (crq->bRequestType != (USB_DIR_OUT | USB_RECIP_DEVICE))
			goto delegate;

		set_address(udc, le16_to_cpu(crq->wValue));
		send_status(udc, ep);
		ep->state = STATUS_STAGE_ADDR;
		break;

	default:
delegate:
		spin_unlock(&udc->lock);
		retval = udc->driver->setup(&udc->gadget, crq);
		spin_lock(&udc->lock);
	}

	return retval;

stall:
	pr_err("udc: %s: Invalid setup request: %02x.%02x v%04x i%04x l%d, "
		"halting endpoint...\n",
		ep->ep.name, crq->bRequestType, crq->bRequest,
		le16_to_cpu(crq->wValue), le16_to_cpu(crq->wIndex),
		le16_to_cpu(crq->wLength));
	set_protocol_stall(udc, ep);
	return -1;
}

static void usba_control_irq(struct usba_udc *udc, struct usba_ep *ep)
{
	struct usba_request *req;
	u32 epstatus;
	u32 epctrl;

restart:
	epstatus = usba_ep_readl(ep, STA);
	epctrl = usba_ep_readl(ep, CTL);

	DBG(DBG_INT, "%s [%d]: s/%08x c/%08x\n",
			ep->ep.name, ep->state, epstatus, epctrl);

	req = NULL;
	if (!list_empty(&ep->queue))
		req = list_entry(ep->queue.next,
				 struct usba_request, queue);

	if ((epctrl & USBA_TX_PK_RDY) && !(epstatus & USBA_TX_PK_RDY)) {
		if (req->submitted)
			next_fifo_transaction(ep, req);
		else
			submit_request(ep, req);

		if (req->last_transaction) {
			usba_ep_writel(ep, CTL_DIS, USBA_TX_PK_RDY);
			usba_ep_writel(ep, CTL_ENB, USBA_TX_COMPLETE);
		}
		goto restart;
	}
	if ((epstatus & epctrl) & USBA_TX_COMPLETE) {
		usba_ep_writel(ep, CLR_STA, USBA_TX_COMPLETE);

		switch (ep->state) {
		case DATA_STAGE_IN:
			usba_ep_writel(ep, CTL_ENB, USBA_RX_BK_RDY);
			usba_ep_writel(ep, CTL_DIS, USBA_TX_COMPLETE);
			ep->state = STATUS_STAGE_OUT;
			break;
		case STATUS_STAGE_ADDR:
			/* Activate our new address */
			usba_writel(udc, CTRL, (usba_readl(udc, CTRL)
						| USBA_FADDR_EN));
			usba_ep_writel(ep, CTL_DIS, USBA_TX_COMPLETE);
			ep->state = WAIT_FOR_SETUP;
			break;
		case STATUS_STAGE_IN:
			if (req) {
				list_del_init(&req->queue);
				request_complete(ep, req, 0);
				submit_next_request(ep);
			}
			usba_ep_writel(ep, CTL_DIS, USBA_TX_COMPLETE);
			ep->state = WAIT_FOR_SETUP;
			break;
		case STATUS_STAGE_TEST:
			usba_ep_writel(ep, CTL_DIS, USBA_TX_COMPLETE);
			ep->state = WAIT_FOR_SETUP;
			if (do_test_mode(udc))
				set_protocol_stall(udc, ep);
			break;
		default:
			pr_err("udc: %s: TXCOMP: Invalid endpoint state %d, "
				"halting endpoint...\n",
				ep->ep.name, ep->state);
			set_protocol_stall(udc, ep);
			break;
		}

		goto restart;
	}
	if ((epstatus & epctrl) & USBA_RX_BK_RDY) {
		switch (ep->state) {
		case STATUS_STAGE_OUT:
			usba_ep_writel(ep, CLR_STA, USBA_RX_BK_RDY);
			usba_ep_writel(ep, CTL_DIS, USBA_RX_BK_RDY);

			if (req) {
				list_del_init(&req->queue);
				request_complete(ep, req, 0);
			}
			ep->state = WAIT_FOR_SETUP;
			break;

		case DATA_STAGE_OUT:
			receive_data(ep);
			break;

		default:
			usba_ep_writel(ep, CLR_STA, USBA_RX_BK_RDY);
			usba_ep_writel(ep, CTL_DIS, USBA_RX_BK_RDY);
			pr_err("udc: %s: RXRDY: Invalid endpoint state %d, "
				"halting endpoint...\n",
				ep->ep.name, ep->state);
			set_protocol_stall(udc, ep);
			break;
		}

		goto restart;
	}
	if (epstatus & USBA_RX_SETUP) {
		union {
			struct usb_ctrlrequest crq;
			unsigned long data[2];
		} crq;
		unsigned int pkt_len;
		int ret;

		if (ep->state != WAIT_FOR_SETUP) {
			/*
			 * Didn't expect a SETUP packet at this
			 * point. Clean up any pending requests (which
			 * may be successful).
			 */
			int status = -EPROTO;

			/*
			 * RXRDY and TXCOMP are dropped when SETUP
			 * packets arrive.  Just pretend we received
			 * the status packet.
			 */
			if (ep->state == STATUS_STAGE_OUT
					|| ep->state == STATUS_STAGE_IN) {
				usba_ep_writel(ep, CTL_DIS, USBA_RX_BK_RDY);
				status = 0;
			}

			if (req) {
				list_del_init(&req->queue);
				request_complete(ep, req, status);
			}
		}

		pkt_len = USBA_BFEXT(BYTE_COUNT, usba_ep_readl(ep, STA));
		DBG(DBG_HW, "Packet length: %u\n", pkt_len);
		if (pkt_len != sizeof(crq)) {
			pr_warning("udc: Invalid packet length %u "
				"(expected %lu)\n", pkt_len, sizeof(crq));
			set_protocol_stall(udc, ep);
			return;
		}

		DBG(DBG_FIFO, "Copying ctrl request from 0x%p:\n", ep->fifo);
		copy_from_fifo(crq.data, ep->fifo, sizeof(crq));

		/* Free up one bank in the FIFO so that we can
		 * generate or receive a reply right away. */
		usba_ep_writel(ep, CLR_STA, USBA_RX_SETUP);

		/* printk(KERN_DEBUG "setup: %d: %02x.%02x\n",
			ep->state, crq.crq.bRequestType,
			crq.crq.bRequest); */

		if (crq.crq.bRequestType & USB_DIR_IN) {
			/*
			 * The USB 2.0 spec states that "if wLength is
			 * zero, there is no data transfer phase."
			 * However, testusb #14 seems to actually
			 * expect a data phase even if wLength = 0...
			 */
			ep->state = DATA_STAGE_IN;
		} else {
			if (crq.crq.wLength != __constant_cpu_to_le16(0))
				ep->state = DATA_STAGE_OUT;
			else
				ep->state = STATUS_STAGE_IN;
		}

		ret = -1;
		if (ep->index == 0)
			ret = handle_ep0_setup(udc, ep, &crq.crq);
		else {
			spin_unlock(&udc->lock);
			ret = udc->driver->setup(&udc->gadget, &crq.crq);
			spin_lock(&udc->lock);
		}

		DBG(DBG_BUS, "req %02x.%02x, length %d, state %d, ret %d\n",
			crq.crq.bRequestType, crq.crq.bRequest,
			le16_to_cpu(crq.crq.wLength), ep->state, ret);

		if (ret < 0) {
			/* Let the host know that we failed */
			set_protocol_stall(udc, ep);
		}
	}
}

static void usba_ep_irq(struct usba_udc *udc, struct usba_ep *ep)
{
	struct usba_request *req;
	u32 epstatus;
	u32 epctrl;

	epstatus = usba_ep_readl(ep, STA);
	epctrl = usba_ep_readl(ep, CTL);

	DBG(DBG_INT, "%s: interrupt, status: 0x%08x\n", ep->ep.name, epstatus);

	while ((epctrl & USBA_TX_PK_RDY) && !(epstatus & USBA_TX_PK_RDY)) {
		DBG(DBG_BUS, "%s: TX PK ready\n", ep->ep.name);

		if (list_empty(&ep->queue)) {
			dev_warn(&udc->pdev->dev, "ep_irq: queue empty\n");
			usba_ep_writel(ep, CTL_DIS, USBA_TX_PK_RDY);
			return;
		}

		req = list_entry(ep->queue.next, struct usba_request, queue);

		if (req->using_dma) {
			/* Send a zero-length packet */
			usba_ep_writel(ep, SET_STA,
					USBA_TX_PK_RDY);
			usba_ep_writel(ep, CTL_DIS,
					USBA_TX_PK_RDY);
			list_del_init(&req->queue);
			submit_next_request(ep);
			request_complete(ep, req, 0);
		} else {
			if (req->submitted)
				next_fifo_transaction(ep, req);
			else
				submit_request(ep, req);

			if (req->last_transaction) {
				list_del_init(&req->queue);
				submit_next_request(ep);
				request_complete(ep, req, 0);
			}
		}

		epstatus = usba_ep_readl(ep, STA);
		epctrl = usba_ep_readl(ep, CTL);
	}
	if ((epstatus & epctrl) & USBA_RX_BK_RDY) {
		DBG(DBG_BUS, "%s: RX data ready\n", ep->ep.name);
		receive_data(ep);
		usba_ep_writel(ep, CLR_STA, USBA_RX_BK_RDY);
	}
}

static void usba_dma_irq(struct usba_udc *udc, struct usba_ep *ep)
{
	struct usba_request *req;
	u32 status, control, pending;

	status = usba_dma_readl(ep, STATUS);
	control = usba_dma_readl(ep, CONTROL);
#ifdef CONFIG_USB_GADGET_DEBUG_FS
	ep->last_dma_status = status;
#endif
	pending = status & control;
	DBG(DBG_INT | DBG_DMA, "dma irq, s/%#08x, c/%#08x\n", status, control);

	if (status & USBA_DMA_CH_EN) {
		dev_err(&udc->pdev->dev,
			"DMA_CH_EN is set after transfer is finished!\n");
		dev_err(&udc->pdev->dev,
			"status=%#08x, pending=%#08x, control=%#08x\n",
			status, pending, control);

		/*
		 * try to pretend nothing happened. We might have to
		 * do something here...
		 */
	}

	if (list_empty(&ep->queue))
		/* Might happen if a reset comes along at the right moment */
		return;

	if (pending & (USBA_DMA_END_TR_ST | USBA_DMA_END_BUF_ST)) {
		req = list_entry(ep->queue.next, struct usba_request, queue);
		usba_update_req(ep, req, status);

		list_del_init(&req->queue);
		submit_next_request(ep);
		request_complete(ep, req, 0);
	}
}

static irqreturn_t usba_udc_irq(int irq, void *devid)
{
	struct usba_udc *udc = devid;
	u32 status;
	u32 dma_status;
	u32 ep_status;

	spin_lock(&udc->lock);

	status = usba_readl(udc, INT_STA);
	DBG(DBG_INT, "irq, status=%#08x\n", status);

	if (status & USBA_DET_SUSPEND) {
		usba_writel(udc, INT_CLR, USBA_DET_SUSPEND);
		DBG(DBG_BUS, "Suspend detected\n");
		if (udc->gadget.speed != USB_SPEED_UNKNOWN
				&& udc->driver && udc->driver->suspend) {
			spin_unlock(&udc->lock);
			udc->driver->suspend(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	if (status & USBA_WAKE_UP) {
		usba_writel(udc, INT_CLR, USBA_WAKE_UP);
		DBG(DBG_BUS, "Wake Up CPU detected\n");
	}

	if (status & USBA_END_OF_RESUME) {
		usba_writel(udc, INT_CLR, USBA_END_OF_RESUME);
		DBG(DBG_BUS, "Resume detected\n");
		if (udc->gadget.speed != USB_SPEED_UNKNOWN
				&& udc->driver && udc->driver->resume) {
			spin_unlock(&udc->lock);
			udc->driver->resume(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	dma_status = USBA_BFEXT(DMA_INT, status);
	if (dma_status) {
		int i;

		for (i = 1; i < USBA_NR_ENDPOINTS; i++)
			if (dma_status & (1 << i))
				usba_dma_irq(udc, &usba_ep[i]);
	}

	ep_status = USBA_BFEXT(EPT_INT, status);
	if (ep_status) {
		int i;

		for (i = 0; i < USBA_NR_ENDPOINTS; i++)
			if (ep_status & (1 << i)) {
				if (ep_is_control(&usba_ep[i]))
					usba_control_irq(udc, &usba_ep[i]);
				else
					usba_ep_irq(udc, &usba_ep[i]);
			}
	}

	if (status & USBA_END_OF_RESET) {
		struct usba_ep *ep0;

		usba_writel(udc, INT_CLR, USBA_END_OF_RESET);
		reset_all_endpoints(udc);

		if (status & USBA_HIGH_SPEED) {
			DBG(DBG_BUS, "High-speed bus reset detected\n");
			udc->gadget.speed = USB_SPEED_HIGH;
		} else {
			DBG(DBG_BUS, "Full-speed bus reset detected\n");
			udc->gadget.speed = USB_SPEED_FULL;
		}

		ep0 = &usba_ep[0];
		ep0->desc = &usba_ep0_desc;
		ep0->state = WAIT_FOR_SETUP;
		usba_ep_writel(ep0, CFG,
				(USBA_BF(EPT_SIZE, EP0_EPT_SIZE)
				| USBA_BF(EPT_TYPE, USBA_EPT_TYPE_CONTROL)
				| USBA_BF(BK_NUMBER, USBA_BK_NUMBER_ONE)));
		usba_ep_writel(ep0, CTL_ENB,
				USBA_EPT_ENABLE | USBA_RX_SETUP);
		usba_writel(udc, INT_ENB,
				(usba_readl(udc, INT_ENB)
				| USBA_BF(EPT_INT, 1)
				| USBA_DET_SUSPEND
				| USBA_END_OF_RESUME));

		if (!(usba_ep_readl(ep0, CFG) & USBA_EPT_MAPPED))
			dev_warn(&udc->pdev->dev,
				 "WARNING: EP0 configuration is invalid!\n");
	}

	spin_unlock(&udc->lock);

	return IRQ_HANDLED;
}

static irqreturn_t usba_vbus_irq(int irq, void *devid)
{
	struct usba_udc *udc = devid;
	int vbus;

	/* debounce */
	udelay(10);

	spin_lock(&udc->lock);

	/* May happen if Vbus pin toggles during probe() */
	if (!udc->driver)
		goto out;

	vbus = gpio_get_value(udc->vbus_pin);
	if (vbus != udc->vbus_prev) {
		if (vbus) {
			usba_writel(udc, CTRL, USBA_EN_USBA);
			usba_writel(udc, INT_ENB, USBA_END_OF_RESET);
		} else {
			udc->gadget.speed = USB_SPEED_UNKNOWN;
			reset_all_endpoints(udc);
			usba_writel(udc, CTRL, 0);
			spin_unlock(&udc->lock);
			udc->driver->disconnect(&udc->gadget);
			spin_lock(&udc->lock);
		}
		udc->vbus_prev = vbus;
	}

out:
	spin_unlock(&udc->lock);

	return IRQ_HANDLED;
}

int usb_gadget_register_driver(struct usb_gadget_driver *driver)
{
	struct usba_udc *udc = &the_udc;
	unsigned long flags;
	int ret;

	if (!udc->pdev)
		return -ENODEV;

	spin_lock_irqsave(&udc->lock, flags);
	if (udc->driver) {
		spin_unlock_irqrestore(&udc->lock, flags);
		return -EBUSY;
	}

	udc->devstatus = 1 << USB_DEVICE_SELF_POWERED;
	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;
	spin_unlock_irqrestore(&udc->lock, flags);

	clk_enable(udc->pclk);
	clk_enable(udc->hclk);

	ret = driver->bind(&udc->gadget);
	if (ret) {
		DBG(DBG_ERR, "Could not bind to driver %s: error %d\n",
			driver->driver.name, ret);
		goto err_driver_bind;
	}

	DBG(DBG_GADGET, "registered driver `%s'\n", driver->driver.name);

	udc->vbus_prev = 0;
	if (udc->vbus_pin != -1)
		enable_irq(gpio_to_irq(udc->vbus_pin));

	/* If Vbus is present, enable the controller and wait for reset */
	spin_lock_irqsave(&udc->lock, flags);
	if (vbus_is_present(udc) && udc->vbus_prev == 0) {
		usba_writel(udc, CTRL, USBA_EN_USBA);
		usba_writel(udc, INT_ENB, USBA_END_OF_RESET);
	}
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;

err_driver_bind:
	udc->driver = NULL;
	udc->gadget.dev.driver = NULL;
	return ret;
}
EXPORT_SYMBOL(usb_gadget_register_driver);

int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	struct usba_udc *udc = &the_udc;
	unsigned long flags;

	if (!udc->pdev)
		return -ENODEV;
	if (driver != udc->driver)
		return -EINVAL;

	if (udc->vbus_pin != -1)
		disable_irq(gpio_to_irq(udc->vbus_pin));

	spin_lock_irqsave(&udc->lock, flags);
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	reset_all_endpoints(udc);
	spin_unlock_irqrestore(&udc->lock, flags);

	/* This will also disable the DP pullup */
	usba_writel(udc, CTRL, 0);

	driver->unbind(&udc->gadget);
	udc->gadget.dev.driver = NULL;
	udc->driver = NULL;

	clk_disable(udc->hclk);
	clk_disable(udc->pclk);

	DBG(DBG_GADGET, "unregistered driver `%s'\n", driver->driver.name);

	return 0;
}
EXPORT_SYMBOL(usb_gadget_unregister_driver);

static int __init usba_udc_probe(struct platform_device *pdev)
{
	struct usba_platform_data *pdata = pdev->dev.platform_data;
	struct resource *regs, *fifo;
	struct clk *pclk, *hclk;
	struct usba_udc *udc = &the_udc;
	int irq, ret, i;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, CTRL_IOMEM_ID);
	fifo = platform_get_resource(pdev, IORESOURCE_MEM, FIFO_IOMEM_ID);
	if (!regs || !fifo)
		return -ENXIO;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	pclk = clk_get(&pdev->dev, "pclk");
	if (IS_ERR(pclk))
		return PTR_ERR(pclk);
	hclk = clk_get(&pdev->dev, "hclk");
	if (IS_ERR(hclk)) {
		ret = PTR_ERR(hclk);
		goto err_get_hclk;
	}

	udc->pdev = pdev;
	udc->pclk = pclk;
	udc->hclk = hclk;
	udc->vbus_pin = -1;

	ret = -ENOMEM;
	udc->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!udc->regs) {
		dev_err(&pdev->dev, "Unable to map I/O memory, aborting.\n");
		goto err_map_regs;
	}
	dev_info(&pdev->dev, "MMIO registers at 0x%08lx mapped at %p\n",
		 (unsigned long)regs->start, udc->regs);
	udc->fifo = ioremap(fifo->start, fifo->end - fifo->start + 1);
	if (!udc->fifo) {
		dev_err(&pdev->dev, "Unable to map FIFO, aborting.\n");
		goto err_map_fifo;
	}
	dev_info(&pdev->dev, "FIFO at 0x%08lx mapped at %p\n",
		 (unsigned long)fifo->start, udc->fifo);

	device_initialize(&udc->gadget.dev);
	udc->gadget.dev.parent = &pdev->dev;
	udc->gadget.dev.dma_mask = pdev->dev.dma_mask;

	platform_set_drvdata(pdev, udc);

	/* Make sure we start from a clean slate */
	clk_enable(pclk);
	usba_writel(udc, CTRL, 0);
	clk_disable(pclk);

	INIT_LIST_HEAD(&usba_ep[0].ep.ep_list);
	usba_ep[0].ep_regs = udc->regs + USBA_EPT_BASE(0);
	usba_ep[0].dma_regs = udc->regs + USBA_DMA_BASE(0);
	usba_ep[0].fifo = udc->fifo + USBA_FIFO_BASE(0);
	for (i = 1; i < ARRAY_SIZE(usba_ep); i++) {
		struct usba_ep *ep = &usba_ep[i];

		ep->ep_regs = udc->regs + USBA_EPT_BASE(i);
		ep->dma_regs = udc->regs + USBA_DMA_BASE(i);
		ep->fifo = udc->fifo + USBA_FIFO_BASE(i);

		list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);
	}

	ret = request_irq(irq, usba_udc_irq, 0, "atmel_usba_udc", udc);
	if (ret) {
		dev_err(&pdev->dev, "Cannot request irq %d (error %d)\n",
			irq, ret);
		goto err_request_irq;
	}
	udc->irq = irq;

	ret = device_add(&udc->gadget.dev);
	if (ret) {
		dev_dbg(&pdev->dev, "Could not add gadget: %d\n", ret);
		goto err_device_add;
	}

	if (pdata && pdata->vbus_pin != GPIO_PIN_NONE) {
		if (!gpio_request(pdata->vbus_pin, "atmel_usba_udc")) {
			udc->vbus_pin = pdata->vbus_pin;

			ret = request_irq(gpio_to_irq(udc->vbus_pin),
					usba_vbus_irq, 0,
					"atmel_usba_udc", udc);
			if (ret) {
				gpio_free(udc->vbus_pin);
				udc->vbus_pin = -1;
				dev_warn(&udc->pdev->dev,
					 "failed to request vbus irq; "
					 "assuming always on\n");
			} else {
				disable_irq(gpio_to_irq(udc->vbus_pin));
			}
		}
	}

	usba_init_debugfs(udc);
	for (i = 1; i < ARRAY_SIZE(usba_ep); i++)
		usba_ep_init_debugfs(udc, &usba_ep[i]);

	return 0;

err_device_add:
	free_irq(irq, udc);
err_request_irq:
	iounmap(udc->fifo);
err_map_fifo:
	iounmap(udc->regs);
err_map_regs:
	clk_put(hclk);
err_get_hclk:
	clk_put(pclk);

	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int __exit usba_udc_remove(struct platform_device *pdev)
{
	struct usba_udc *udc;
	int i;

	udc = platform_get_drvdata(pdev);

	for (i = 1; i < ARRAY_SIZE(usba_ep); i++)
		usba_ep_cleanup_debugfs(&usba_ep[i]);
	usba_cleanup_debugfs(udc);

	if (udc->vbus_pin != -1)
		gpio_free(udc->vbus_pin);

	free_irq(udc->irq, udc);
	iounmap(udc->fifo);
	iounmap(udc->regs);
	clk_put(udc->hclk);
	clk_put(udc->pclk);

	device_unregister(&udc->gadget.dev);

	return 0;
}

static struct platform_driver udc_driver = {
	.remove		= __exit_p(usba_udc_remove),
	.driver		= {
		.name		= "atmel_usba_udc",
		.owner		= THIS_MODULE,
	},
};

static int __init udc_init(void)
{
	return platform_driver_probe(&udc_driver, usba_udc_probe);
}
module_init(udc_init);

static void __exit udc_exit(void)
{
	platform_driver_unregister(&udc_driver);
}
module_exit(udc_exit);

MODULE_DESCRIPTION("Atmel USBA UDC driver");
MODULE_AUTHOR("Haavard Skinnemoen <hskinnemoen@atmel.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atmel_usba_udc");
