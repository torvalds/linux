// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Atmel USBA high speed USB device controller
 *
 * Copyright (C) 2005-2007 Atmel Corporation
 */
#include <linux/clk.h>
#include <linux/clk/at91_pmc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/ctype.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/irq.h>
#include <linux/gpio/consumer.h>

#include "atmel_usba_udc.h"
#define USBA_VBUS_IRQFLAGS (IRQF_ONESHOT \
			   | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING)

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
		req_copy = kmemdup(req, sizeof(*req_copy), GFP_ATOMIC);
		if (!req_copy)
			goto fail;
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

	inode_lock(file_inode(file));
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
	inode_unlock(file_inode(file));

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

	inode_lock(inode);
	udc = inode->i_private;
	data = kmalloc(inode->i_size, GFP_KERNEL);
	if (!data)
		goto out;

	spin_lock_irq(&udc->lock);
	for (i = 0; i < inode->i_size / 4; i++)
		data[i] = readl_relaxed(udc->regs + i * 4);
	spin_unlock_irq(&udc->lock);

	file->private_data = data;
	ret = 0;

out:
	inode_unlock(inode);

	return ret;
}

static ssize_t regs_dbg_read(struct file *file, char __user *buf,
		size_t nbytes, loff_t *ppos)
{
	struct inode *inode = file_inode(file);
	int ret;

	inode_lock(inode);
	ret = simple_read_from_buffer(buf, nbytes, ppos,
			file->private_data,
			file_inode(file)->i_size);
	inode_unlock(inode);

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
	ep->debugfs_dir = ep_root;

	debugfs_create_file("queue", 0400, ep_root, ep, &queue_dbg_fops);
	if (ep->can_dma)
		debugfs_create_u32("dma_status", 0400, ep_root,
				   &ep->last_dma_status);
	if (ep_is_control(ep))
		debugfs_create_u32("state", 0400, ep_root, &ep->state);
}

static void usba_ep_cleanup_debugfs(struct usba_ep *ep)
{
	debugfs_remove_recursive(ep->debugfs_dir);
}

static void usba_init_debugfs(struct usba_udc *udc)
{
	struct dentry *root;
	struct resource *regs_resource;

	root = debugfs_create_dir(udc->gadget.name, NULL);
	udc->debugfs_root = root;

	regs_resource = platform_get_resource(udc->pdev, IORESOURCE_MEM,
				CTRL_IOMEM_ID);

	if (regs_resource) {
		debugfs_create_file_size("regs", 0400, root, udc,
					 &regs_dbg_fops,
					 resource_size(regs_resource));
	}

	usba_ep_init_debugfs(udc, to_usba_ep(udc->gadget.ep0));
}

static void usba_cleanup_debugfs(struct usba_udc *udc)
{
	usba_ep_cleanup_debugfs(to_usba_ep(udc->gadget.ep0));
	debugfs_remove_recursive(udc->debugfs_root);
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

static ushort fifo_mode;

module_param(fifo_mode, ushort, 0x0);
MODULE_PARM_DESC(fifo_mode, "Endpoint configuration mode");

/* mode 0 - uses autoconfig */

/* mode 1 - fits in 8KB, generic max fifo configuration */
static struct usba_fifo_cfg mode_1_cfg[] = {
{ .hw_ep_num = 0, .fifo_size = 64,	.nr_banks = 1, },
{ .hw_ep_num = 1, .fifo_size = 1024,	.nr_banks = 2, },
{ .hw_ep_num = 2, .fifo_size = 1024,	.nr_banks = 1, },
{ .hw_ep_num = 3, .fifo_size = 1024,	.nr_banks = 1, },
{ .hw_ep_num = 4, .fifo_size = 1024,	.nr_banks = 1, },
{ .hw_ep_num = 5, .fifo_size = 1024,	.nr_banks = 1, },
{ .hw_ep_num = 6, .fifo_size = 1024,	.nr_banks = 1, },
};

/* mode 2 - fits in 8KB, performance max fifo configuration */
static struct usba_fifo_cfg mode_2_cfg[] = {
{ .hw_ep_num = 0, .fifo_size = 64,	.nr_banks = 1, },
{ .hw_ep_num = 1, .fifo_size = 1024,	.nr_banks = 3, },
{ .hw_ep_num = 2, .fifo_size = 1024,	.nr_banks = 2, },
{ .hw_ep_num = 3, .fifo_size = 1024,	.nr_banks = 2, },
};

/* mode 3 - fits in 8KB, mixed fifo configuration */
static struct usba_fifo_cfg mode_3_cfg[] = {
{ .hw_ep_num = 0, .fifo_size = 64,	.nr_banks = 1, },
{ .hw_ep_num = 1, .fifo_size = 1024,	.nr_banks = 2, },
{ .hw_ep_num = 2, .fifo_size = 512,	.nr_banks = 2, },
{ .hw_ep_num = 3, .fifo_size = 512,	.nr_banks = 2, },
{ .hw_ep_num = 4, .fifo_size = 512,	.nr_banks = 2, },
{ .hw_ep_num = 5, .fifo_size = 512,	.nr_banks = 2, },
{ .hw_ep_num = 6, .fifo_size = 512,	.nr_banks = 2, },
};

/* mode 4 - fits in 8KB, custom fifo configuration */
static struct usba_fifo_cfg mode_4_cfg[] = {
{ .hw_ep_num = 0, .fifo_size = 64,	.nr_banks = 1, },
{ .hw_ep_num = 1, .fifo_size = 512,	.nr_banks = 2, },
{ .hw_ep_num = 2, .fifo_size = 512,	.nr_banks = 2, },
{ .hw_ep_num = 3, .fifo_size = 8,	.nr_banks = 2, },
{ .hw_ep_num = 4, .fifo_size = 512,	.nr_banks = 2, },
{ .hw_ep_num = 5, .fifo_size = 512,	.nr_banks = 2, },
{ .hw_ep_num = 6, .fifo_size = 16,	.nr_banks = 2, },
{ .hw_ep_num = 7, .fifo_size = 8,	.nr_banks = 2, },
{ .hw_ep_num = 8, .fifo_size = 8,	.nr_banks = 2, },
};
/* Add additional configurations here */

static int usba_config_fifo_table(struct usba_udc *udc)
{
	int n;

	switch (fifo_mode) {
	default:
		fifo_mode = 0;
	case 0:
		udc->fifo_cfg = NULL;
		n = 0;
		break;
	case 1:
		udc->fifo_cfg = mode_1_cfg;
		n = ARRAY_SIZE(mode_1_cfg);
		break;
	case 2:
		udc->fifo_cfg = mode_2_cfg;
		n = ARRAY_SIZE(mode_2_cfg);
		break;
	case 3:
		udc->fifo_cfg = mode_3_cfg;
		n = ARRAY_SIZE(mode_3_cfg);
		break;
	case 4:
		udc->fifo_cfg = mode_4_cfg;
		n = ARRAY_SIZE(mode_4_cfg);
		break;
	}
	DBG(DBG_HW, "Setup fifo_mode %d\n", fifo_mode);

	return n;
}

static inline u32 usba_int_enb_get(struct usba_udc *udc)
{
	return udc->int_enb_cache;
}

static inline void usba_int_enb_set(struct usba_udc *udc, u32 val)
{
	usba_writel(udc, INT_ENB, val);
	udc->int_enb_cache = val;
}

static int vbus_is_present(struct usba_udc *udc)
{
	if (udc->vbus_pin)
		return gpiod_get_value(udc->vbus_pin);

	/* No Vbus detection: Assume always present */
	return 1;
}

static void toggle_bias(struct usba_udc *udc, int is_on)
{
	if (udc->errata && udc->errata->toggle_bias)
		udc->errata->toggle_bias(udc, is_on);
}

static void generate_bias_pulse(struct usba_udc *udc)
{
	if (!udc->bias_pulse_needed)
		return;

	if (udc->errata && udc->errata->pulse_bias)
		udc->errata->pulse_bias(udc);

	udc->bias_pulse_needed = false;
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

	memcpy_toio(ep->fifo, req->req.buf + req->req.actual, transaction_len);
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

		memcpy_fromio(req->req.buf + req->req.actual,
				ep->fifo, bytecount);
		req->req.actual += bytecount;

		usba_ep_writel(ep, CLR_STA, USBA_RX_BK_RDY);

		if (is_complete) {
			DBG(DBG_QUEUE, "%s: request done\n", ep->ep.name);
			req->req.status = 0;
			list_del_init(&req->queue);
			usba_ep_writel(ep, CTL_DIS, USBA_RX_BK_RDY);
			spin_unlock(&udc->lock);
			usb_gadget_giveback_request(&ep->ep, &req->req);
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

	if (req->using_dma)
		usb_gadget_unmap_request(&udc->gadget, &req->req, ep->is_in);

	DBG(DBG_GADGET | DBG_REQ,
		"%s: req %p complete: status %d, actual %u\n",
		ep->ep.name, req, req->req.status, req->req.actual);

	spin_unlock(&udc->lock);
	usb_gadget_giveback_request(&ep->ep, &req->req);
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
	unsigned long flags, maxpacket;
	unsigned int nr_trans;

	DBG(DBG_GADGET, "%s: ep_enable: desc=%p\n", ep->ep.name, desc);

	maxpacket = usb_endpoint_maxp(desc);

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

	DBG(DBG_ERR, "%s: EPT_CFG = 0x%lx (maxpacket = %lu)\n",
			ep->ep.name, ep->ept_cfg, maxpacket);

	if (usb_endpoint_dir_in(desc)) {
		ep->is_in = 1;
		ep->ept_cfg |= USBA_EPT_DIR_IN;
	}

	switch (usb_endpoint_type(desc)) {
	case USB_ENDPOINT_XFER_CONTROL:
		ep->ept_cfg |= USBA_BF(EPT_TYPE, USBA_EPT_TYPE_CONTROL);
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
		nr_trans = usb_endpoint_maxp_mult(desc);
		if (nr_trans > 3)
			return -EINVAL;

		ep->is_isoc = 1;
		ep->ept_cfg |= USBA_BF(EPT_TYPE, USBA_EPT_TYPE_ISO);
		ep->ept_cfg |= USBA_BF(NB_TRANS, nr_trans);

		break;
	case USB_ENDPOINT_XFER_BULK:
		ep->ept_cfg |= USBA_BF(EPT_TYPE, USBA_EPT_TYPE_BULK);
		break;
	case USB_ENDPOINT_XFER_INT:
		ep->ept_cfg |= USBA_BF(EPT_TYPE, USBA_EPT_TYPE_INT);
		break;
	}

	spin_lock_irqsave(&ep->udc->lock, flags);

	ep->ep.desc = desc;
	ep->ep.maxpacket = maxpacket;

	usba_ep_writel(ep, CFG, ep->ept_cfg);
	usba_ep_writel(ep, CTL_ENB, USBA_EPT_ENABLE);

	if (ep->can_dma) {
		u32 ctrl;

		usba_int_enb_set(udc, usba_int_enb_get(udc) |
				      USBA_BF(EPT_INT, 1 << ep->index) |
				      USBA_BF(DMA_INT, 1 << ep->index));
		ctrl = USBA_AUTO_VALID | USBA_INTDIS_DMA;
		usba_ep_writel(ep, CTL_ENB, ctrl);
	} else {
		usba_int_enb_set(udc, usba_int_enb_get(udc) |
				      USBA_BF(EPT_INT, 1 << ep->index));
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	DBG(DBG_HW, "EPT_CFG%d after init: %#08lx\n", ep->index,
			(unsigned long)usba_ep_readl(ep, CFG));
	DBG(DBG_HW, "INT_ENB after init: %#08lx\n",
			(unsigned long)usba_int_enb_get(udc));

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

	if (!ep->ep.desc) {
		spin_unlock_irqrestore(&udc->lock, flags);
		/* REVISIT because this driver disables endpoints in
		 * reset_all_endpoints() before calling disconnect(),
		 * most gadget drivers would trigger this non-error ...
		 */
		if (udc->gadget.speed != USB_SPEED_UNKNOWN)
			DBG(DBG_ERR, "ep_disable: %s not enabled\n",
					ep->ep.name);
		return -EINVAL;
	}
	ep->ep.desc = NULL;

	list_splice_init(&ep->queue, &req_list);
	if (ep->can_dma) {
		usba_dma_writel(ep, CONTROL, 0);
		usba_dma_writel(ep, ADDRESS, 0);
		usba_dma_readl(ep, STATUS);
	}
	usba_ep_writel(ep, CTL_DIS, USBA_EPT_ENABLE);
	usba_int_enb_set(udc, usba_int_enb_get(udc) &
			      ~USBA_BF(EPT_INT, 1 << ep->index));

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

	DBG(DBG_DMA, "%s: req l/%u d/%pad %c%c%c\n",
		ep->ep.name, req->req.length, &req->req.dma,
		req->req.zero ? 'Z' : 'z',
		req->req.short_not_ok ? 'S' : 's',
		req->req.no_interrupt ? 'I' : 'i');

	if (req->req.length > 0x10000) {
		/* Lengths from 0 to 65536 (inclusive) are supported */
		DBG(DBG_ERR, "invalid request length %u\n", req->req.length);
		return -EINVAL;
	}

	ret = usb_gadget_map_request(&udc->gadget, &req->req, ep->is_in);
	if (ret)
		return ret;

	req->using_dma = 1;
	req->ctrl = USBA_BF(DMA_BUF_LEN, req->req.length)
			| USBA_DMA_CH_EN | USBA_DMA_END_BUF_IE
			| USBA_DMA_END_BUF_EN;

	if (!ep->is_in)
		req->ctrl |= USBA_DMA_END_TR_EN | USBA_DMA_END_TR_IE;

	/*
	 * Add this request to the queue and submit for DMA if
	 * possible. Check if we're still alive first -- we may have
	 * received a reset since last time we checked.
	 */
	ret = -ESHUTDOWN;
	spin_lock_irqsave(&udc->lock, flags);
	if (ep->ep.desc) {
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

	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN ||
	    !ep->ep.desc)
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
	if (ep->ep.desc) {
		list_add_tail(&req->queue, &ep->queue);

		if ((!ep_is_control(ep) && ep->is_in) ||
			(ep_is_control(ep)
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
	struct usba_request *req;
	unsigned long flags;
	u32 status;

	DBG(DBG_GADGET | DBG_QUEUE, "ep_dequeue: %s, req %p\n",
			ep->ep.name, req);

	spin_lock_irqsave(&udc->lock, flags);

	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}

	if (&req->req != _req) {
		spin_unlock_irqrestore(&udc->lock, flags);
		return -EINVAL;
	}

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

	if (!ep->ep.desc) {
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

	gadget->is_selfpowered = (is_selfpowered != 0);
	spin_lock_irqsave(&udc->lock, flags);
	if (is_selfpowered)
		udc->devstatus |= 1 << USB_DEVICE_SELF_POWERED;
	else
		udc->devstatus &= ~(1 << USB_DEVICE_SELF_POWERED);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int atmel_usba_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver);
static int atmel_usba_stop(struct usb_gadget *gadget);

static struct usb_ep *atmel_usba_match_ep(struct usb_gadget *gadget,
				struct usb_endpoint_descriptor	*desc,
				struct usb_ss_ep_comp_descriptor *ep_comp)
{
	struct usb_ep	*_ep;
	struct usba_ep *ep;

	/* Look at endpoints until an unclaimed one looks usable */
	list_for_each_entry(_ep, &gadget->ep_list, ep_list) {
		if (usb_gadget_ep_match_desc(gadget, _ep, desc, ep_comp))
			goto found_ep;
	}
	/* Fail */
	return NULL;

found_ep:

	if (fifo_mode == 0) {
		/* Optimize hw fifo size based on ep type and other info */
		ep = to_usba_ep(_ep);

		switch (usb_endpoint_type(desc)) {
		case USB_ENDPOINT_XFER_CONTROL:
			break;

		case USB_ENDPOINT_XFER_ISOC:
			ep->fifo_size = 1024;
			ep->nr_banks = 2;
			break;

		case USB_ENDPOINT_XFER_BULK:
			ep->fifo_size = 512;
			ep->nr_banks = 1;
			break;

		case USB_ENDPOINT_XFER_INT:
			if (desc->wMaxPacketSize == 0)
				ep->fifo_size =
				    roundup_pow_of_two(_ep->maxpacket_limit);
			else
				ep->fifo_size =
				    roundup_pow_of_two(le16_to_cpu(desc->wMaxPacketSize));
			ep->nr_banks = 1;
			break;
		}

		/* It might be a little bit late to set this */
		usb_ep_set_maxpacket_limit(&ep->ep, ep->fifo_size);

		/* Generate ept_cfg basd on FIFO size and number of banks */
		if (ep->fifo_size  <= 8)
			ep->ept_cfg = USBA_BF(EPT_SIZE, USBA_EPT_SIZE_8);
		else
			/* LSB is bit 1, not 0 */
			ep->ept_cfg =
				USBA_BF(EPT_SIZE, fls(ep->fifo_size - 1) - 3);

		ep->ept_cfg |= USBA_BF(BK_NUMBER, ep->nr_banks);

		ep->udc->configured_ep++;
	}

	return _ep;
}

static const struct usb_gadget_ops usba_udc_ops = {
	.get_frame		= usba_udc_get_frame,
	.wakeup			= usba_udc_wakeup,
	.set_selfpowered	= usba_udc_set_selfpowered,
	.udc_start		= atmel_usba_start,
	.udc_stop		= atmel_usba_stop,
	.match_ep		= atmel_usba_match_ep,
};

static struct usb_endpoint_descriptor usba_ep0_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0,
	.bmAttributes = USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize = cpu_to_le16(64),
	/* FIXME: I have no idea what to put here */
	.bInterval = 1,
};

static struct usb_gadget usba_gadget_template = {
	.ops		= &usba_udc_ops,
	.max_speed	= USB_SPEED_HIGH,
	.name		= "atmel_usba_udc",
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
}

static struct usba_ep *get_ep_by_addr(struct usba_udc *udc, u16 wIndex)
{
	struct usba_ep *ep;

	if ((wIndex & USB_ENDPOINT_NUMBER_MASK) == 0)
		return to_usba_ep(udc->gadget.ep0);

	list_for_each_entry (ep, &udc->gadget.ep_list, ep.ep_list) {
		u8 bEndpointAddress;

		if (!ep->ep.desc)
			continue;
		bEndpointAddress = ep->ep.desc->bEndpointAddress;
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
		ep = &udc->usba_ep[0];
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
		ep = &udc->usba_ep[0];
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
			memcpy_toio(ep->fifo, test_packet_buffer,
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
	if (crq->wValue == cpu_to_le16(USB_DEVICE_REMOTE_WAKEUP))
		return true;
	return false;
}

static inline bool feature_is_dev_test_mode(struct usb_ctrlrequest *crq)
{
	if (crq->wValue == cpu_to_le16(USB_DEVICE_TEST_MODE))
		return true;
	return false;
}

static inline bool feature_is_ep_halt(struct usb_ctrlrequest *crq)
{
	if (crq->wValue == cpu_to_le16(USB_ENDPOINT_HALT))
		return true;
	return false;
}

static int handle_ep0_setup(struct usba_udc *udc, struct usba_ep *ep,
		struct usb_ctrlrequest *crq)
{
	int retval = 0;

	switch (crq->bRequest) {
	case USB_REQ_GET_STATUS: {
		u16 status;

		if (crq->bRequestType == (USB_DIR_IN | USB_RECIP_DEVICE)) {
			status = cpu_to_le16(udc->devstatus);
		} else if (crq->bRequestType
				== (USB_DIR_IN | USB_RECIP_INTERFACE)) {
			status = cpu_to_le16(0);
		} else if (crq->bRequestType
				== (USB_DIR_IN | USB_RECIP_ENDPOINT)) {
			struct usba_ep *target;

			target = get_ep_by_addr(udc, le16_to_cpu(crq->wIndex));
			if (!target)
				goto stall;

			status = 0;
			if (is_stalled(udc, target))
				status |= cpu_to_le16(1);
		} else
			goto delegate;

		/* Write directly to the FIFO. No queueing is done. */
		if (crq->wLength != cpu_to_le16(sizeof(status)))
			goto stall;
		ep->state = DATA_STAGE_IN;
		writew_relaxed(status, ep->fifo);
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

			if (crq->wLength != cpu_to_le16(0)
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

			if (crq->wLength != cpu_to_le16(0)
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
			pr_warn("udc: Invalid packet length %u (expected %zu)\n",
				pkt_len, sizeof(crq));
			set_protocol_stall(udc, ep);
			return;
		}

		DBG(DBG_FIFO, "Copying ctrl request from 0x%p:\n", ep->fifo);
		memcpy_fromio(crq.data, ep->fifo, sizeof(crq));

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
			if (crq.crq.wLength != cpu_to_le16(0))
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
	u32 status, int_enb;
	u32 dma_status;
	u32 ep_status;

	spin_lock(&udc->lock);

	int_enb = usba_int_enb_get(udc);
	status = usba_readl(udc, INT_STA) & (int_enb | USBA_HIGH_SPEED);
	DBG(DBG_INT, "irq, status=%#08x\n", status);

	if (status & USBA_DET_SUSPEND) {
		toggle_bias(udc, 0);
		usba_writel(udc, INT_CLR, USBA_DET_SUSPEND);
		usba_int_enb_set(udc, int_enb | USBA_WAKE_UP);
		udc->bias_pulse_needed = true;
		DBG(DBG_BUS, "Suspend detected\n");
		if (udc->gadget.speed != USB_SPEED_UNKNOWN
				&& udc->driver && udc->driver->suspend) {
			spin_unlock(&udc->lock);
			udc->driver->suspend(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}

	if (status & USBA_WAKE_UP) {
		toggle_bias(udc, 1);
		usba_writel(udc, INT_CLR, USBA_WAKE_UP);
		usba_int_enb_set(udc, int_enb & ~USBA_WAKE_UP);
		DBG(DBG_BUS, "Wake Up CPU detected\n");
	}

	if (status & USBA_END_OF_RESUME) {
		usba_writel(udc, INT_CLR, USBA_END_OF_RESUME);
		generate_bias_pulse(udc);
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

		for (i = 1; i <= USBA_NR_DMAS; i++)
			if (dma_status & (1 << i))
				usba_dma_irq(udc, &udc->usba_ep[i]);
	}

	ep_status = USBA_BFEXT(EPT_INT, status);
	if (ep_status) {
		int i;

		for (i = 0; i < udc->num_ep; i++)
			if (ep_status & (1 << i)) {
				if (ep_is_control(&udc->usba_ep[i]))
					usba_control_irq(udc, &udc->usba_ep[i]);
				else
					usba_ep_irq(udc, &udc->usba_ep[i]);
			}
	}

	if (status & USBA_END_OF_RESET) {
		struct usba_ep *ep0, *ep;
		int i, n;

		usba_writel(udc, INT_CLR, USBA_END_OF_RESET);
		generate_bias_pulse(udc);
		reset_all_endpoints(udc);

		if (udc->gadget.speed != USB_SPEED_UNKNOWN && udc->driver) {
			udc->gadget.speed = USB_SPEED_UNKNOWN;
			spin_unlock(&udc->lock);
			usb_gadget_udc_reset(&udc->gadget, udc->driver);
			spin_lock(&udc->lock);
		}

		if (status & USBA_HIGH_SPEED)
			udc->gadget.speed = USB_SPEED_HIGH;
		else
			udc->gadget.speed = USB_SPEED_FULL;
		DBG(DBG_BUS, "%s bus reset detected\n",
		    usb_speed_string(udc->gadget.speed));

		ep0 = &udc->usba_ep[0];
		ep0->ep.desc = &usba_ep0_desc;
		ep0->state = WAIT_FOR_SETUP;
		usba_ep_writel(ep0, CFG,
				(USBA_BF(EPT_SIZE, EP0_EPT_SIZE)
				| USBA_BF(EPT_TYPE, USBA_EPT_TYPE_CONTROL)
				| USBA_BF(BK_NUMBER, USBA_BK_NUMBER_ONE)));
		usba_ep_writel(ep0, CTL_ENB,
				USBA_EPT_ENABLE | USBA_RX_SETUP);
		usba_int_enb_set(udc, int_enb | USBA_BF(EPT_INT, 1) |
				      USBA_DET_SUSPEND | USBA_END_OF_RESUME);

		/*
		 * Unclear why we hit this irregularly, e.g. in usbtest,
		 * but it's clearly harmless...
		 */
		if (!(usba_ep_readl(ep0, CFG) & USBA_EPT_MAPPED))
			dev_err(&udc->pdev->dev,
				"ODD: EP0 configuration is invalid!\n");

		/* Preallocate other endpoints */
		n = fifo_mode ? udc->num_ep : udc->configured_ep;
		for (i = 1; i < n; i++) {
			ep = &udc->usba_ep[i];
			usba_ep_writel(ep, CFG, ep->ept_cfg);
			if (!(usba_ep_readl(ep, CFG) & USBA_EPT_MAPPED))
				dev_err(&udc->pdev->dev,
					"ODD: EP%d configuration is invalid!\n", i);
		}
	}

	spin_unlock(&udc->lock);

	return IRQ_HANDLED;
}

static int start_clock(struct usba_udc *udc)
{
	int ret;

	if (udc->clocked)
		return 0;

	ret = clk_prepare_enable(udc->pclk);
	if (ret)
		return ret;
	ret = clk_prepare_enable(udc->hclk);
	if (ret) {
		clk_disable_unprepare(udc->pclk);
		return ret;
	}

	udc->clocked = true;
	return 0;
}

static void stop_clock(struct usba_udc *udc)
{
	if (!udc->clocked)
		return;

	clk_disable_unprepare(udc->hclk);
	clk_disable_unprepare(udc->pclk);

	udc->clocked = false;
}

static int usba_start(struct usba_udc *udc)
{
	unsigned long flags;
	int ret;

	ret = start_clock(udc);
	if (ret)
		return ret;

	spin_lock_irqsave(&udc->lock, flags);
	toggle_bias(udc, 1);
	usba_writel(udc, CTRL, USBA_ENABLE_MASK);
	usba_int_enb_set(udc, USBA_END_OF_RESET);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static void usba_stop(struct usba_udc *udc)
{
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	reset_all_endpoints(udc);

	/* This will also disable the DP pullup */
	toggle_bias(udc, 0);
	usba_writel(udc, CTRL, USBA_DISABLE_MASK);
	spin_unlock_irqrestore(&udc->lock, flags);

	stop_clock(udc);
}

static irqreturn_t usba_vbus_irq_thread(int irq, void *devid)
{
	struct usba_udc *udc = devid;
	int vbus;

	/* debounce */
	udelay(10);

	mutex_lock(&udc->vbus_mutex);

	vbus = vbus_is_present(udc);
	if (vbus != udc->vbus_prev) {
		if (vbus) {
			usba_start(udc);
		} else {
			usba_stop(udc);

			if (udc->driver->disconnect)
				udc->driver->disconnect(&udc->gadget);
		}
		udc->vbus_prev = vbus;
	}

	mutex_unlock(&udc->vbus_mutex);
	return IRQ_HANDLED;
}

static int atmel_usba_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	int ret;
	struct usba_udc *udc = container_of(gadget, struct usba_udc, gadget);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);
	udc->devstatus = 1 << USB_DEVICE_SELF_POWERED;
	udc->driver = driver;
	spin_unlock_irqrestore(&udc->lock, flags);

	mutex_lock(&udc->vbus_mutex);

	if (udc->vbus_pin)
		enable_irq(gpiod_to_irq(udc->vbus_pin));

	/* If Vbus is present, enable the controller and wait for reset */
	udc->vbus_prev = vbus_is_present(udc);
	if (udc->vbus_prev) {
		ret = usba_start(udc);
		if (ret)
			goto err;
	}

	mutex_unlock(&udc->vbus_mutex);
	return 0;

err:
	if (udc->vbus_pin)
		disable_irq(gpiod_to_irq(udc->vbus_pin));

	mutex_unlock(&udc->vbus_mutex);

	spin_lock_irqsave(&udc->lock, flags);
	udc->devstatus &= ~(1 << USB_DEVICE_SELF_POWERED);
	udc->driver = NULL;
	spin_unlock_irqrestore(&udc->lock, flags);
	return ret;
}

static int atmel_usba_stop(struct usb_gadget *gadget)
{
	struct usba_udc *udc = container_of(gadget, struct usba_udc, gadget);

	if (udc->vbus_pin)
		disable_irq(gpiod_to_irq(udc->vbus_pin));

	if (fifo_mode == 0)
		udc->configured_ep = 1;

	usba_stop(udc);

	udc->driver = NULL;

	return 0;
}

static void at91sam9rl_toggle_bias(struct usba_udc *udc, int is_on)
{
	regmap_update_bits(udc->pmc, AT91_CKGR_UCKR, AT91_PMC_BIASEN,
			   is_on ? AT91_PMC_BIASEN : 0);
}

static void at91sam9g45_pulse_bias(struct usba_udc *udc)
{
	regmap_update_bits(udc->pmc, AT91_CKGR_UCKR, AT91_PMC_BIASEN, 0);
	regmap_update_bits(udc->pmc, AT91_CKGR_UCKR, AT91_PMC_BIASEN,
			   AT91_PMC_BIASEN);
}

static const struct usba_udc_errata at91sam9rl_errata = {
	.toggle_bias = at91sam9rl_toggle_bias,
};

static const struct usba_udc_errata at91sam9g45_errata = {
	.pulse_bias = at91sam9g45_pulse_bias,
};

static const struct of_device_id atmel_udc_dt_ids[] = {
	{ .compatible = "atmel,at91sam9rl-udc", .data = &at91sam9rl_errata },
	{ .compatible = "atmel,at91sam9g45-udc", .data = &at91sam9g45_errata },
	{ .compatible = "atmel,sama5d3-udc" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, atmel_udc_dt_ids);

static struct usba_ep * atmel_udc_of_init(struct platform_device *pdev,
						    struct usba_udc *udc)
{
	u32 val;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct device_node *pp;
	int i, ret;
	struct usba_ep *eps, *ep;

	match = of_match_node(atmel_udc_dt_ids, np);
	if (!match)
		return ERR_PTR(-EINVAL);

	udc->errata = match->data;
	udc->pmc = syscon_regmap_lookup_by_compatible("atmel,at91sam9g45-pmc");
	if (IS_ERR(udc->pmc))
		udc->pmc = syscon_regmap_lookup_by_compatible("atmel,at91sam9rl-pmc");
	if (IS_ERR(udc->pmc))
		udc->pmc = syscon_regmap_lookup_by_compatible("atmel,at91sam9x5-pmc");
	if (udc->errata && IS_ERR(udc->pmc))
		return ERR_CAST(udc->pmc);

	udc->num_ep = 0;

	udc->vbus_pin = devm_gpiod_get_optional(&pdev->dev, "atmel,vbus",
						GPIOD_IN);

	if (fifo_mode == 0) {
		pp = NULL;
		while ((pp = of_get_next_child(np, pp)))
			udc->num_ep++;
		udc->configured_ep = 1;
	} else {
		udc->num_ep = usba_config_fifo_table(udc);
	}

	eps = devm_kcalloc(&pdev->dev, udc->num_ep, sizeof(struct usba_ep),
			   GFP_KERNEL);
	if (!eps)
		return ERR_PTR(-ENOMEM);

	udc->gadget.ep0 = &eps[0].ep;

	INIT_LIST_HEAD(&eps[0].ep.ep_list);

	pp = NULL;
	i = 0;
	while ((pp = of_get_next_child(np, pp)) && i < udc->num_ep) {
		ep = &eps[i];

		ret = of_property_read_u32(pp, "reg", &val);
		if (ret) {
			dev_err(&pdev->dev, "of_probe: reg error(%d)\n", ret);
			goto err;
		}
		ep->index = fifo_mode ? udc->fifo_cfg[i].hw_ep_num : val;

		ret = of_property_read_u32(pp, "atmel,fifo-size", &val);
		if (ret) {
			dev_err(&pdev->dev, "of_probe: fifo-size error(%d)\n", ret);
			goto err;
		}
		if (fifo_mode) {
			if (val < udc->fifo_cfg[i].fifo_size) {
				dev_warn(&pdev->dev,
					 "Using max fifo-size value from DT\n");
				ep->fifo_size = val;
			} else {
				ep->fifo_size = udc->fifo_cfg[i].fifo_size;
			}
		} else {
			ep->fifo_size = val;
		}

		ret = of_property_read_u32(pp, "atmel,nb-banks", &val);
		if (ret) {
			dev_err(&pdev->dev, "of_probe: nb-banks error(%d)\n", ret);
			goto err;
		}
		if (fifo_mode) {
			if (val < udc->fifo_cfg[i].nr_banks) {
				dev_warn(&pdev->dev,
					 "Using max nb-banks value from DT\n");
				ep->nr_banks = val;
			} else {
				ep->nr_banks = udc->fifo_cfg[i].nr_banks;
			}
		} else {
			ep->nr_banks = val;
		}

		ep->can_dma = of_property_read_bool(pp, "atmel,can-dma");
		ep->can_isoc = of_property_read_bool(pp, "atmel,can-isoc");

		sprintf(ep->name, "ep%d", ep->index);
		ep->ep.name = ep->name;

		ep->ep_regs = udc->regs + USBA_EPT_BASE(i);
		ep->dma_regs = udc->regs + USBA_DMA_BASE(i);
		ep->fifo = udc->fifo + USBA_FIFO_BASE(i);
		ep->ep.ops = &usba_ep_ops;
		usb_ep_set_maxpacket_limit(&ep->ep, ep->fifo_size);
		ep->udc = udc;
		INIT_LIST_HEAD(&ep->queue);

		if (ep->index == 0) {
			ep->ep.caps.type_control = true;
		} else {
			ep->ep.caps.type_iso = ep->can_isoc;
			ep->ep.caps.type_bulk = true;
			ep->ep.caps.type_int = true;
		}

		ep->ep.caps.dir_in = true;
		ep->ep.caps.dir_out = true;

		if (fifo_mode != 0) {
			/*
			 * Generate ept_cfg based on FIFO size and
			 * banks number
			 */
			if (ep->fifo_size  <= 8)
				ep->ept_cfg = USBA_BF(EPT_SIZE, USBA_EPT_SIZE_8);
			else
				/* LSB is bit 1, not 0 */
				ep->ept_cfg =
				  USBA_BF(EPT_SIZE, fls(ep->fifo_size - 1) - 3);

			ep->ept_cfg |= USBA_BF(BK_NUMBER, ep->nr_banks);
		}

		if (i)
			list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);

		i++;
	}

	if (i == 0) {
		dev_err(&pdev->dev, "of_probe: no endpoint specified\n");
		ret = -EINVAL;
		goto err;
	}

	return eps;
err:
	return ERR_PTR(ret);
}

static int usba_udc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct clk *pclk, *hclk;
	struct usba_udc *udc;
	int irq, ret, i;

	udc = devm_kzalloc(&pdev->dev, sizeof(*udc), GFP_KERNEL);
	if (!udc)
		return -ENOMEM;

	udc->gadget = usba_gadget_template;
	INIT_LIST_HEAD(&udc->gadget.ep_list);

	res = platform_get_resource(pdev, IORESOURCE_MEM, CTRL_IOMEM_ID);
	udc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(udc->regs))
		return PTR_ERR(udc->regs);
	dev_info(&pdev->dev, "MMIO registers at %pR mapped at %p\n",
		 res, udc->regs);

	res = platform_get_resource(pdev, IORESOURCE_MEM, FIFO_IOMEM_ID);
	udc->fifo = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(udc->fifo))
		return PTR_ERR(udc->fifo);
	dev_info(&pdev->dev, "FIFO at %pR mapped at %p\n", res, udc->fifo);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(pclk))
		return PTR_ERR(pclk);
	hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(hclk))
		return PTR_ERR(hclk);

	spin_lock_init(&udc->lock);
	mutex_init(&udc->vbus_mutex);
	udc->pdev = pdev;
	udc->pclk = pclk;
	udc->hclk = hclk;

	platform_set_drvdata(pdev, udc);

	/* Make sure we start from a clean slate */
	ret = clk_prepare_enable(pclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable pclk, aborting.\n");
		return ret;
	}

	usba_writel(udc, CTRL, USBA_DISABLE_MASK);
	clk_disable_unprepare(pclk);

	udc->usba_ep = atmel_udc_of_init(pdev, udc);

	toggle_bias(udc, 0);

	if (IS_ERR(udc->usba_ep))
		return PTR_ERR(udc->usba_ep);

	ret = devm_request_irq(&pdev->dev, irq, usba_udc_irq, 0,
				"atmel_usba_udc", udc);
	if (ret) {
		dev_err(&pdev->dev, "Cannot request irq %d (error %d)\n",
			irq, ret);
		return ret;
	}
	udc->irq = irq;

	if (udc->vbus_pin) {
		irq_set_status_flags(gpiod_to_irq(udc->vbus_pin), IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(&pdev->dev,
				gpiod_to_irq(udc->vbus_pin), NULL,
				usba_vbus_irq_thread, USBA_VBUS_IRQFLAGS,
				"atmel_usba_udc", udc);
		if (ret) {
			udc->vbus_pin = NULL;
			dev_warn(&udc->pdev->dev,
				 "failed to request vbus irq; "
				 "assuming always on\n");
		}
	}

	ret = usb_add_gadget_udc(&pdev->dev, &udc->gadget);
	if (ret)
		return ret;
	device_init_wakeup(&pdev->dev, 1);

	usba_init_debugfs(udc);
	for (i = 1; i < udc->num_ep; i++)
		usba_ep_init_debugfs(udc, &udc->usba_ep[i]);

	return 0;
}

static int usba_udc_remove(struct platform_device *pdev)
{
	struct usba_udc *udc;
	int i;

	udc = platform_get_drvdata(pdev);

	device_init_wakeup(&pdev->dev, 0);
	usb_del_gadget_udc(&udc->gadget);

	for (i = 1; i < udc->num_ep; i++)
		usba_ep_cleanup_debugfs(&udc->usba_ep[i]);
	usba_cleanup_debugfs(udc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int usba_udc_suspend(struct device *dev)
{
	struct usba_udc *udc = dev_get_drvdata(dev);

	/* Not started */
	if (!udc->driver)
		return 0;

	mutex_lock(&udc->vbus_mutex);

	if (!device_may_wakeup(dev)) {
		usba_stop(udc);
		goto out;
	}

	/*
	 * Device may wake up. We stay clocked if we failed
	 * to request vbus irq, assuming always on.
	 */
	if (udc->vbus_pin) {
		usba_stop(udc);
		enable_irq_wake(gpiod_to_irq(udc->vbus_pin));
	}

out:
	mutex_unlock(&udc->vbus_mutex);
	return 0;
}

static int usba_udc_resume(struct device *dev)
{
	struct usba_udc *udc = dev_get_drvdata(dev);

	/* Not started */
	if (!udc->driver)
		return 0;

	if (device_may_wakeup(dev) && udc->vbus_pin)
		disable_irq_wake(gpiod_to_irq(udc->vbus_pin));

	/* If Vbus is present, enable the controller and wait for reset */
	mutex_lock(&udc->vbus_mutex);
	udc->vbus_prev = vbus_is_present(udc);
	if (udc->vbus_prev)
		usba_start(udc);
	mutex_unlock(&udc->vbus_mutex);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(usba_udc_pm_ops, usba_udc_suspend, usba_udc_resume);

static struct platform_driver udc_driver = {
	.remove		= usba_udc_remove,
	.driver		= {
		.name		= "atmel_usba_udc",
		.pm		= &usba_udc_pm_ops,
		.of_match_table	= atmel_udc_dt_ids,
	},
};

module_platform_driver_probe(udc_driver, usba_udc_probe);

MODULE_DESCRIPTION("Atmel USBA UDC driver");
MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atmel_usba_udc");
