// SPDX-License-Identifier: GPL-2.0-only
/* drivers/usb/gadget/f_diag.c
 * Diag Function Device - Route ARM9 and ARM11 DIAG messages
 * between HOST and DEVICE.
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2008-2020, The Linux Foundation. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/ratelimit.h>

#include <linux/usb/usbdiag.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/kmemleak.h>

#define MAX_INST_NAME_LEN	40

/* dload specific suppot */
#define PID_MAGIC_ID		0x71432909
#define SERIAL_NUM_MAGIC_ID	0x61945374
#define SERIAL_NUMBER_LENGTH	128

struct dload_struct {
	u32	pid;
	char	serial_number[SERIAL_NUMBER_LENGTH];
	u32	pid_magic;
	u32	serial_magic;
};

/* for configfs support */
struct diag_opts {
	struct usb_function_instance func_inst;
	char *name;
	struct dload_struct dload;
};

static inline struct diag_opts *to_diag_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct diag_opts,
			    func_inst.group);
}

static DEFINE_SPINLOCK(ch_lock);
static LIST_HEAD(usb_diag_ch_list);

static struct dload_struct __iomem *diag_dload;

static struct usb_interface_descriptor intf_desc = {
	.bLength            =	sizeof(intf_desc),
	.bDescriptorType    =	USB_DT_INTERFACE,
	.bNumEndpoints      =	2,
	.bInterfaceClass    =	USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass =	USB_SUBCLASS_VENDOR_SPEC,
	.bInterfaceProtocol =	0x30,
};

static struct usb_endpoint_descriptor hs_bulk_in_desc = {
	.bLength          =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType  =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes     =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize   =	cpu_to_le16(512),
	.bInterval        =	0,
};
static struct usb_endpoint_descriptor fs_bulk_in_desc = {
	.bLength          =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType  =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes     =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize   =	cpu_to_le16(64),
	.bInterval        =	0,
};

static struct usb_endpoint_descriptor hs_bulk_out_desc = {
	.bLength          =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType  =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes     =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize   =	cpu_to_le16(512),
	.bInterval        =	0,
};

static struct usb_endpoint_descriptor fs_bulk_out_desc = {
	.bLength          =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType  =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes     =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize   =	cpu_to_le16(64),
	.bInterval        =	0,
};

static struct usb_endpoint_descriptor ss_bulk_in_desc = {
	.bLength          =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType  =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes     =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize   =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_bulk_in_comp_desc = {
	.bLength =		sizeof(ss_bulk_in_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =	0, */
};

static struct usb_endpoint_descriptor ss_bulk_out_desc = {
	.bLength          =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType  =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes     =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize   =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_bulk_out_comp_desc = {
	.bLength =		sizeof(ss_bulk_out_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =	0, */
};

static struct usb_descriptor_header *fs_diag_desc[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &fs_bulk_in_desc,
	(struct usb_descriptor_header *) &fs_bulk_out_desc,
	NULL,
	};
static struct usb_descriptor_header *hs_diag_desc[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &hs_bulk_in_desc,
	(struct usb_descriptor_header *) &hs_bulk_out_desc,
	NULL,
};

static struct usb_descriptor_header *ss_diag_desc[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &ss_bulk_in_desc,
	(struct usb_descriptor_header *) &ss_bulk_in_comp_desc,
	(struct usb_descriptor_header *) &ss_bulk_out_desc,
	(struct usb_descriptor_header *) &ss_bulk_out_comp_desc,
	NULL,
};

/**
 * struct diag_context - USB diag function driver private structure
 * @function: function structure for USB interface
 * @out: USB OUT endpoint struct
 * @in: USB IN endpoint struct
 * @in_desc: USB IN endpoint descriptor struct
 * @out_desc: USB OUT endpoint descriptor struct
 * @read_pool: List of requests used for Rx (OUT ep)
 * @write_pool: List of requests used for Tx (IN ep)
 * @lock: Spinlock to proctect read_pool, write_pool lists
 * @cdev: USB composite device struct
 * @ch: USB diag channel
 *
 */
struct diag_context {
	struct usb_function function;
	struct usb_ep *out;
	struct usb_ep *in;
	struct list_head read_pool;
	struct list_head write_pool;
	spinlock_t lock;
	unsigned int configured;
	struct usb_composite_dev *cdev;
	struct usb_diag_ch *ch;
	struct kref kref;

	/* pkt counters */
	unsigned long dpkts_tolaptop;
	unsigned long dpkts_tomodem;
	unsigned int dpkts_tolaptop_pending;

	/* A list node inside the diag_dev_list */
	struct list_head list_item;
};

static struct list_head diag_dev_list;

static inline struct diag_context *func_to_diag(struct usb_function *f)
{
	return container_of(f, struct diag_context, function);
}

/* Called with ctxt->lock held; i.e. only use with kref_put_lock() */
static void diag_context_release(struct kref *kref)
{
	struct diag_context *ctxt =
		container_of(kref, struct diag_context, kref);

	spin_unlock(&ctxt->lock);
	kfree(ctxt);
}

static void diag_update_pid_and_serial_num(struct diag_context *ctxt)
{
	struct usb_composite_dev *cdev = ctxt->cdev;
	struct usb_gadget_strings **table;
	struct usb_string *s;
	struct usb_gadget_string_container *uc;
	struct dload_struct local_diag_dload = { 0 };

	/*
	 * update pid and serial number to dload only if diag
	 * interface is zeroth interface.
	 */
	if (intf_desc.bInterfaceNumber)
		return;

	if (!diag_dload) {
		pr_debug("%s: unable to update PID and serial_no\n", __func__);
		return;
	}

	/* update pid */
	local_diag_dload.pid = cdev->desc.idProduct;
	local_diag_dload.pid_magic = PID_MAGIC_ID;
	local_diag_dload.serial_magic = SERIAL_NUM_MAGIC_ID;

	list_for_each_entry(uc, &cdev->gstrings, list) {
		table = (struct usb_gadget_strings **)uc->stash;
		if (!table) {
			pr_err("%s: can't update dload cookie\n", __func__);
			break;
		}

		for (s = (*table)->strings; s && s->s; s++) {
			if (s->id == cdev->desc.iSerialNumber) {
				strscpy(local_diag_dload.serial_number, s->s,
					SERIAL_NUMBER_LENGTH);
				goto update_dload;
			}
		}

	}

update_dload:
	pr_debug("%s: dload:%pK pid:%x serial_num:%s\n",
				__func__, diag_dload, local_diag_dload.pid,
				local_diag_dload.serial_number);

	memcpy_toio(diag_dload, &local_diag_dload, sizeof(local_diag_dload));
}

static void diag_write_complete(struct usb_ep *ep,
		struct usb_request *req)
{
	struct diag_context *ctxt = ep->driver_data;
	struct diag_request *d_req = req->context;
	unsigned long flags;

	ctxt->dpkts_tolaptop_pending--;

	if (!req->status)
		ctxt->dpkts_tolaptop++;

	spin_lock_irqsave(&ctxt->lock, flags);
	list_add_tail(&req->list, &ctxt->write_pool);
	if (req->length != 0) {
		d_req->actual = req->actual;
		d_req->status = req->status;
	}
	spin_unlock_irqrestore(&ctxt->lock, flags);

	if (ctxt->ch && ctxt->ch->notify)
		ctxt->ch->notify(ctxt->ch->priv, USB_DIAG_WRITE_DONE, d_req);

	kref_put_lock(&ctxt->kref, diag_context_release, &ctxt->lock);
}

static void diag_read_complete(struct usb_ep *ep,
		struct usb_request *req)
{
	struct diag_context *ctxt = ep->driver_data;
	struct diag_request *d_req = req->context;
	unsigned long flags;

	d_req->actual = req->actual;
	d_req->status = req->status;

	spin_lock_irqsave(&ctxt->lock, flags);
	list_add_tail(&req->list, &ctxt->read_pool);
	spin_unlock_irqrestore(&ctxt->lock, flags);

	ctxt->dpkts_tomodem++;

	if (ctxt->ch && ctxt->ch->notify)
		ctxt->ch->notify(ctxt->ch->priv, USB_DIAG_READ_DONE, d_req);

	kref_put_lock(&ctxt->kref, diag_context_release, &ctxt->lock);
}

/**
 * usb_diag_open() - Open a diag channel over USB
 * @name: Name of the channel
 * @priv: Private structure pointer which will be passed in notify()
 * @notify: Callback function to receive notifications
 *
 * This function iterates overs the available channels and returns
 * the channel handler if the name matches. The notify callback is called
 * for CONNECT, DISCONNECT, READ_DONE and WRITE_DONE events.
 *
 */
struct usb_diag_ch *usb_diag_open(const char *name, void *priv,
		void (*notify)(void *, unsigned int, struct diag_request *))
{
	struct usb_diag_ch *ch;
	unsigned long flags;
	int found = 0;
	bool connected = false;
	struct diag_context  *dev;

	spin_lock_irqsave(&ch_lock, flags);
	/* Check if we already have a channel with this name */
	list_for_each_entry(ch, &usb_diag_ch_list, list) {
		if (!strcmp(name, ch->name)) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&ch_lock, flags);

	if (!found) {
		ch = kzalloc(sizeof(*ch), GFP_KERNEL);
		if (!ch)
			return ERR_PTR(-ENOMEM);
	}

	ch->name = name;
	ch->priv = priv;
	ch->notify = notify;

	if (!found) {
		spin_lock_irqsave(&ch_lock, flags);
		list_add_tail(&ch->list, &usb_diag_ch_list);
		spin_unlock_irqrestore(&ch_lock, flags);
	}

	if (ch->priv_usb) {
		dev = ch->priv_usb;
		spin_lock_irqsave(&dev->lock, flags);
		connected = dev->configured;
		spin_unlock_irqrestore(&dev->lock, flags);
	}

	if (ch->notify && connected)
		ch->notify(priv, USB_DIAG_CONNECT, NULL);

	return ch;
}
EXPORT_SYMBOL(usb_diag_open);

/**
 * usb_diag_close() - Close a diag channel over USB
 * @ch: Channel handler
 *
 * This function closes the diag channel.
 *
 */
void usb_diag_close(struct usb_diag_ch *ch)
{
	struct diag_context *dev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ch_lock, flags);
	ch->priv = NULL;
	ch->notify = NULL;
	/* Free-up the resources if channel is no more active */
	list_del(&ch->list);
	list_for_each_entry(dev, &diag_dev_list, list_item)
		if (dev->ch == ch)
			dev->ch = NULL;
	kfree(ch);

	spin_unlock_irqrestore(&ch_lock, flags);
}
EXPORT_SYMBOL(usb_diag_close);

static void free_reqs(struct diag_context *ctxt)
{
	struct list_head *act, *tmp;
	struct usb_request *req;

	list_for_each_safe(act, tmp, &ctxt->write_pool) {
		req = list_entry(act, struct usb_request, list);
		list_del(&req->list);
		usb_ep_free_request(ctxt->in, req);
	}

	list_for_each_safe(act, tmp, &ctxt->read_pool) {
		req = list_entry(act, struct usb_request, list);
		list_del(&req->list);
		usb_ep_free_request(ctxt->out, req);
	}
}

/**
 * usb_diag_alloc_req() - Allocate USB requests
 * @ch: Channel handler
 * @n_write: Number of requests for Tx
 * @n_read: Number of requests for Rx
 *
 * This function allocate read and write USB requests for the interface
 * associated with this channel. The actual buffer is not allocated.
 * The buffer is passed by diag char driver.
 *
 */
int usb_diag_alloc_req(struct usb_diag_ch *ch, int n_write, int n_read)
{
	struct diag_context *ctxt = ch->priv_usb;
	struct usb_request *req;
	int i;
	unsigned long flags;

	if (!ctxt)
		return -ENODEV;

	spin_lock_irqsave(&ctxt->lock, flags);
	/* Free previous session's stale requests */
	free_reqs(ctxt);
	for (i = 0; i < n_write; i++) {
		req = usb_ep_alloc_request(ctxt->in, GFP_ATOMIC);
		if (!req)
			goto fail;
		kmemleak_not_leak(req);
		req->complete = diag_write_complete;
		req->zero = true;
		list_add_tail(&req->list, &ctxt->write_pool);
	}

	for (i = 0; i < n_read; i++) {
		req = usb_ep_alloc_request(ctxt->out, GFP_ATOMIC);
		if (!req)
			goto fail;
		kmemleak_not_leak(req);
		req->complete = diag_read_complete;
		list_add_tail(&req->list, &ctxt->read_pool);
	}
	spin_unlock_irqrestore(&ctxt->lock, flags);
	return 0;
fail:
	free_reqs(ctxt);
	spin_unlock_irqrestore(&ctxt->lock, flags);
	return -ENOMEM;

}
EXPORT_SYMBOL(usb_diag_alloc_req);
#define DWC3_MAX_REQUEST_SIZE (16 * 1024 * 1024)
/**
 * usb_diag_request_size - Max request size for controller
 * @ch: Channel handler
 *
 * Infom max request size so that diag driver can split packets
 * in chunks of max size which controller can handle.
 */
int usb_diag_request_size(struct usb_diag_ch *ch)
{
	return DWC3_MAX_REQUEST_SIZE;
}
EXPORT_SYMBOL(usb_diag_request_size);

/**
 * usb_diag_read() - Read data from USB diag channel
 * @ch: Channel handler
 * @d_req: Diag request struct
 *
 * Enqueue a request on OUT endpoint of the interface corresponding to this
 * channel. This function returns proper error code when interface is not
 * in configured state, no Rx requests available and ep queue is failed.
 *
 * This function operates asynchronously. READ_DONE event is notified after
 * completion of OUT request.
 *
 */
int usb_diag_read(struct usb_diag_ch *ch, struct diag_request *d_req)
{
	struct diag_context *ctxt = ch->priv_usb;
	unsigned long flags;
	struct usb_request *req;
	struct usb_ep *out;
	static DEFINE_RATELIMIT_STATE(rl, 10*HZ, 1);

	if (!ctxt)
		return -ENODEV;

	spin_lock_irqsave(&ctxt->lock, flags);

	if (!ctxt->configured || !ctxt->out) {
		spin_unlock_irqrestore(&ctxt->lock, flags);
		return -EIO;
	}

	out = ctxt->out;

	if (list_empty(&ctxt->read_pool)) {
		spin_unlock_irqrestore(&ctxt->lock, flags);
		ERROR(ctxt->cdev, "%s: no requests available\n", __func__);
		return -EAGAIN;
	}

	req = list_first_entry(&ctxt->read_pool, struct usb_request, list);
	list_del(&req->list);
	kref_get(&ctxt->kref); /* put called in complete callback */
	spin_unlock_irqrestore(&ctxt->lock, flags);

	req->buf = d_req->buf;
	req->length = d_req->length;
	req->context = d_req;

	/* make sure context is still valid after releasing lock */
	if (ctxt != ch->priv_usb) {
		usb_ep_free_request(out, req);
		kref_put_lock(&ctxt->kref, diag_context_release, &ctxt->lock);
		return -EIO;
	}

	if (usb_ep_queue(out, req, GFP_ATOMIC)) {
		/* If error add the link to linked list again*/
		spin_lock_irqsave(&ctxt->lock, flags);
		list_add_tail(&req->list, &ctxt->read_pool);
		/* 1 error message for every 10 sec */
		if (__ratelimit(&rl))
			ERROR(ctxt->cdev, "%s: cannot queue read request\n",
								__func__);

		if (kref_put(&ctxt->kref, diag_context_release))
			/* diag_context_release called spin_unlock already */
			local_irq_restore(flags);
		else
			spin_unlock_irqrestore(&ctxt->lock, flags);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(usb_diag_read);

/**
 * usb_diag_write() - Write data from USB diag channel
 * @ch: Channel handler
 * @d_req: Diag request struct
 *
 * Enqueue a request on IN endpoint of the interface corresponding to this
 * channel. This function returns proper error code when interface is not
 * in configured state, no Tx requests available and ep queue is failed.
 *
 * This function operates asynchronously. WRITE_DONE event is notified after
 * completion of IN request.
 *
 */
int usb_diag_write(struct usb_diag_ch *ch, struct diag_request *d_req)
{
	struct diag_context *ctxt = ch->priv_usb;
	unsigned long flags;
	struct usb_request *req = NULL;
	struct usb_ep *in;
	static DEFINE_RATELIMIT_STATE(rl, 10*HZ, 1);

	if (!ctxt)
		return -ENODEV;

	spin_lock_irqsave(&ctxt->lock, flags);

	if (!ctxt->configured || !ctxt->in) {
		spin_unlock_irqrestore(&ctxt->lock, flags);
		return -EIO;
	}

	in = ctxt->in;

	if (list_empty(&ctxt->write_pool)) {
		spin_unlock_irqrestore(&ctxt->lock, flags);
		ERROR(ctxt->cdev, "%s: no requests available\n", __func__);
		return -EAGAIN;
	}

	req = list_first_entry(&ctxt->write_pool, struct usb_request, list);
	list_del(&req->list);
	kref_get(&ctxt->kref); /* put called in complete callback */
	spin_unlock_irqrestore(&ctxt->lock, flags);

	req->buf = d_req->buf;
	req->length = d_req->length;
	req->context = d_req;

	/* make sure context is still valid after releasing lock */
	if (ctxt != ch->priv_usb) {
		usb_ep_free_request(in, req);
		kref_put_lock(&ctxt->kref, diag_context_release, &ctxt->lock);
		return -EIO;
	}

	ctxt->dpkts_tolaptop_pending++;
	if (usb_ep_queue(in, req, GFP_ATOMIC)) {
		/* If error add the link to linked list again*/
		spin_lock_irqsave(&ctxt->lock, flags);
		list_add_tail(&req->list, &ctxt->write_pool);
		ctxt->dpkts_tolaptop_pending--;
		/* 1 error message for every 10 sec */
		if (__ratelimit(&rl))
			ERROR(ctxt->cdev, "%s: cannot queue read request\n",
								__func__);

		if (kref_put(&ctxt->kref, diag_context_release))
			/* diag_context_release called spin_unlock already */
			local_irq_restore(flags);
		else
			spin_unlock_irqrestore(&ctxt->lock, flags);
		return -EIO;
	}

	/*
	 * It's possible that both write completion AND unbind could have been
	 * completed asynchronously by this point. Since they both release the
	 * kref, ctxt is _NOT_ guaranteed to be valid here.
	 */

	return 0;
}
EXPORT_SYMBOL(usb_diag_write);

static void diag_function_disable(struct usb_function *f)
{
	struct diag_context  *dev = func_to_diag(f);
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->configured = 0;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (dev->ch && dev->ch->notify)
		dev->ch->notify(dev->ch->priv, USB_DIAG_DISCONNECT, NULL);

	usb_ep_disable(dev->in);
	dev->in->driver_data = NULL;

	usb_ep_disable(dev->out);
	dev->out->driver_data = NULL;
	if (dev->ch)
		dev->ch->priv_usb = NULL;
}

static void diag_free_func(struct usb_function *f)
{
	struct diag_context *ctxt = func_to_diag(f);
	unsigned long flags;

	spin_lock_irqsave(&ctxt->lock, flags);
	list_del(&ctxt->list_item);
	if (kref_put(&ctxt->kref, diag_context_release))
		/* diag_context_release called spin_unlock already */
		local_irq_restore(flags);
	else
		spin_unlock_irqrestore(&ctxt->lock, flags);
}

static int diag_function_set_alt(struct usb_function *f,
		unsigned int intf, unsigned int alt)
{
	struct diag_context  *dev = func_to_diag(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	unsigned long flags;
	int rc = 0;

	if (config_ep_by_speed(cdev->gadget, f, dev->in) ||
	    config_ep_by_speed(cdev->gadget, f, dev->out)) {
		dev->in->desc = NULL;
		dev->out->desc = NULL;
		return -EINVAL;
	}

	if (!dev->ch)
		return -ENODEV;

	/*
	 * Indicate to the diag channel that the active diag device is dev.
	 * Since a few diag devices can point to the same channel.
	 */
	dev->ch->priv_usb = dev;

	dev->in->driver_data = dev;
	rc = usb_ep_enable(dev->in);
	if (rc) {
		ERROR(dev->cdev, "can't enable %s, result %d\n",
						dev->in->name, rc);
		return rc;
	}
	dev->out->driver_data = dev;
	rc = usb_ep_enable(dev->out);
	if (rc) {
		ERROR(dev->cdev, "can't enable %s, result %d\n",
						dev->out->name, rc);
		usb_ep_disable(dev->in);
		return rc;
	}

	dev->dpkts_tolaptop = 0;
	dev->dpkts_tomodem = 0;
	dev->dpkts_tolaptop_pending = 0;

	spin_lock_irqsave(&dev->lock, flags);
	dev->configured = 1;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (dev->ch->notify)
		dev->ch->notify(dev->ch->priv, USB_DIAG_CONNECT, NULL);

	return rc;
}

static void diag_function_unbind(struct usb_configuration *c,
		struct usb_function *f)
{
	struct diag_context *ctxt = func_to_diag(f);
	unsigned long flags;

	usb_free_all_descriptors(f);

	/*
	 * Channel priv_usb may point to other diag function.
	 * Clear the priv_usb only if the channel is used by the
	 * diag dev we unbind here.
	 */
	if (ctxt->ch && ctxt->ch->priv_usb == ctxt)
		ctxt->ch->priv_usb = NULL;

	spin_lock_irqsave(&ctxt->lock, flags);
	/* Free any pending USB requests from last session */
	free_reqs(ctxt);
	spin_unlock_irqrestore(&ctxt->lock, flags);
}

static int diag_function_bind(struct usb_configuration *c,
		struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct diag_context *ctxt = func_to_diag(f);
	struct usb_ep *ep;
	int status = -ENODEV;

	ctxt->cdev = c->cdev;

	intf_desc.bInterfaceNumber =  usb_interface_id(c, f);

	ep = usb_ep_autoconfig(cdev->gadget, &fs_bulk_in_desc);
	if (!ep)
		goto fail;
	ctxt->in = ep;
	ep->driver_data = ctxt;

	ep = usb_ep_autoconfig(cdev->gadget, &fs_bulk_out_desc);
	if (!ep)
		goto fail;
	ctxt->out = ep;
	ep->driver_data = ctxt;

	hs_bulk_in_desc.bEndpointAddress =
			fs_bulk_in_desc.bEndpointAddress;
	hs_bulk_out_desc.bEndpointAddress =
			fs_bulk_out_desc.bEndpointAddress;
	ss_bulk_in_desc.bEndpointAddress =
			fs_bulk_in_desc.bEndpointAddress;
	ss_bulk_out_desc.bEndpointAddress =
			fs_bulk_out_desc.bEndpointAddress;

	status = usb_assign_descriptors(f, fs_diag_desc, hs_diag_desc,
				ss_diag_desc, ss_diag_desc);
	if (status)
		goto fail;

	/* Allow only first diag channel to update pid and serial no */
	if (ctxt == list_first_entry(&diag_dev_list,
				struct diag_context, list_item))
		diag_update_pid_and_serial_num(ctxt);

	return 0;
fail:
	if (ctxt->out)
		ctxt->out->driver_data = NULL;
	if (ctxt->in)
		ctxt->in->driver_data = NULL;
	return status;

}

static struct diag_context *diag_context_init(const char *name)
{
	struct diag_context *dev;
	struct usb_diag_ch *_ch;
	int found = 0;
	unsigned long flags;

	pr_debug("%s called for channel:%s\n", __func__, name);

	list_for_each_entry(_ch, &usb_diag_ch_list, list) {
		if (!strcmp(name, _ch->name)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		pr_warn("%s: unable to get diag usb channel\n", __func__);

		_ch = kzalloc(sizeof(*_ch), GFP_KERNEL);
		if (_ch == NULL)
			return ERR_PTR(-ENOMEM);

		_ch->name = name;

		spin_lock_irqsave(&ch_lock, flags);
		list_add_tail(&_ch->list, &usb_diag_ch_list);
		spin_unlock_irqrestore(&ch_lock, flags);
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	list_add_tail(&dev->list_item, &diag_dev_list);

	/*
	 * A few diag devices can point to the same channel, in case that
	 * the diag devices belong to different configurations, however
	 * only the active diag device will claim the channel by setting
	 * the ch->priv_usb (see diag_function_set_alt).
	 */
	dev->ch = _ch;

	dev->function.name = _ch->name;
	dev->function.bind = diag_function_bind;
	dev->function.unbind = diag_function_unbind;
	dev->function.set_alt = diag_function_set_alt;
	dev->function.disable = diag_function_disable;
	dev->function.free_func = diag_free_func;
	kref_init(&dev->kref);
	spin_lock_init(&dev->lock);
	INIT_LIST_HEAD(&dev->read_pool);
	INIT_LIST_HEAD(&dev->write_pool);

	return dev;
}

#if defined(CONFIG_DEBUG_FS)
static char debug_buffer[PAGE_SIZE];

static ssize_t debug_read_stats(struct file *file, char __user *ubuf,
		size_t count, loff_t *ppos)
{
	char *buf = debug_buffer;
	int temp = 0;
	struct usb_diag_ch *ch;

	list_for_each_entry(ch, &usb_diag_ch_list, list) {
		struct diag_context *ctxt = ch->priv_usb;
		unsigned long flags;

		if (ctxt) {
			spin_lock_irqsave(&ctxt->lock, flags);
			temp += scnprintf(buf + temp, PAGE_SIZE - temp,
					"---Name: %s---\n"
					"endpoints: %s, %s\n"
					"dpkts_tolaptop: %lu\n"
					"dpkts_tomodem:  %lu\n"
					"pkts_tolaptop_pending: %u\n",
					ch->name,
					ctxt->in->name, ctxt->out->name,
					ctxt->dpkts_tolaptop,
					ctxt->dpkts_tomodem,
					ctxt->dpkts_tolaptop_pending);
			spin_unlock_irqrestore(&ctxt->lock, flags);
		}
	}

	return simple_read_from_buffer(ubuf, count, ppos, buf, temp);
}

static ssize_t debug_reset_stats(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct usb_diag_ch *ch;

	list_for_each_entry(ch, &usb_diag_ch_list, list) {
		struct diag_context *ctxt = ch->priv_usb;
		unsigned long flags;

		if (ctxt) {
			spin_lock_irqsave(&ctxt->lock, flags);
			ctxt->dpkts_tolaptop = 0;
			ctxt->dpkts_tomodem = 0;
			ctxt->dpkts_tolaptop_pending = 0;
			spin_unlock_irqrestore(&ctxt->lock, flags);
		}
	}

	return count;
}

static int debug_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations debug_fdiag_ops = {
	.open = debug_open,
	.read = debug_read_stats,
	.write = debug_reset_stats,
};

struct dentry *dent_diag;
static void fdiag_debugfs_init(void)
{
	struct dentry *dent_diag_status;

	dent_diag = debugfs_create_dir("usb_diag", 0);
	if (!dent_diag || IS_ERR(dent_diag))
		return;

	dent_diag_status = debugfs_create_file("status", 0444, dent_diag, 0,
			&debug_fdiag_ops);

	if (!dent_diag_status || IS_ERR(dent_diag_status)) {
		debugfs_remove(dent_diag);
		dent_diag = NULL;
		return;
	}
}

static void fdiag_debugfs_remove(void)
{
	debugfs_remove_recursive(dent_diag);
}
#else
static inline void fdiag_debugfs_init(void) {}
static inline void fdiag_debugfs_remove(void) {}
#endif

static void diag_opts_release(struct config_item *item)
{
	struct diag_opts *opts = to_diag_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations diag_item_ops = {
	.release	= diag_opts_release,
};

static ssize_t diag_pid_show(struct config_item *item, char *page)
{
	struct dload_struct local_dload_struct;

	if (!diag_dload) {
		pr_warn("%s: diag_dload mem region not defined\n", __func__);
		return -EINVAL;
	}

	memcpy_fromio(&local_dload_struct.pid, &diag_dload->pid,
			sizeof(local_dload_struct.pid));

	return scnprintf(page, PAGE_SIZE, "%x\n", local_dload_struct.pid);
}

static ssize_t diag_pid_store(struct config_item *item, const char *page,
		size_t len)
{
	int ret;
	u32 pid;

	if (!diag_dload) {
		pr_warn("%s: diag_dload mem region not defined\n", __func__);
		return 0;
	}

	ret = kstrtou32(page, 0, &pid);
	if (ret)
		return ret;

	memcpy_toio(&diag_dload->pid, &pid, sizeof(pid));

	pid = PID_MAGIC_ID;
	memcpy_toio(&diag_dload->pid_magic, &pid, sizeof(pid));

	return len;
}

CONFIGFS_ATTR(diag_, pid);

static ssize_t diag_serial_show(struct config_item *item, char *page)
{
	struct dload_struct local_dload_struct;

	if (!diag_dload) {
		pr_warn("%s: diag_dload mem region not defined\n", __func__);
		return -EINVAL;
	}

	memcpy_fromio(&local_dload_struct.serial_number,
			&diag_dload->serial_number,
			SERIAL_NUMBER_LENGTH);

	return scnprintf(page, PAGE_SIZE, "%s\n",
			local_dload_struct.serial_number);
}

static ssize_t diag_serial_store(struct config_item *item, const char *page,
		size_t len)
{
	u32 magic;
	char *p;
	char serial_number[SERIAL_NUMBER_LENGTH] = {0};

	if (!diag_dload) {
		pr_warn("%s: diag_dload mem region not defined\n", __func__);
		return 0;
	}

	strscpy(serial_number, page, SERIAL_NUMBER_LENGTH);
	p = strnchr(serial_number, SERIAL_NUMBER_LENGTH, '\n');
	if (p)
		*p = '\0';

	memcpy_toio(&diag_dload->serial_number, serial_number,
			SERIAL_NUMBER_LENGTH);

	magic = SERIAL_NUM_MAGIC_ID;
	memcpy_toio(&diag_dload->serial_magic, &magic, sizeof(magic));

	return len;
}

CONFIGFS_ATTR(diag_, serial);

static struct configfs_attribute *diag_attrs[] = {
	&diag_attr_pid,
	&diag_attr_serial,
	NULL,
};

static struct config_item_type diag_func_type = {
	.ct_item_ops	= &diag_item_ops,
	.ct_attrs	= diag_attrs,
	.ct_owner	= THIS_MODULE,
};

static int diag_set_inst_name(struct usb_function_instance *fi,
	const char *name)
{
	struct diag_opts *opts = container_of(fi, struct diag_opts, func_inst);
	char *ptr;
	int name_len;

	name_len = strlen(name) + 1;
	if (name_len > MAX_INST_NAME_LEN)
		return -ENAMETOOLONG;

	ptr = kstrndup(name, name_len, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	opts->name = ptr;

	return 0;
}

static void diag_free_inst(struct usb_function_instance *f)
{
	struct diag_opts *opts;

	opts = container_of(f, struct diag_opts, func_inst);
	kfree(opts->name);
	kfree(opts);
}

static struct usb_function_instance *diag_alloc_inst(void)
{
	struct diag_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	opts->func_inst.set_inst_name = diag_set_inst_name;
	opts->func_inst.free_func_inst = diag_free_inst;
	config_group_init_type_name(&opts->func_inst.group, "",
				    &diag_func_type);

	return &opts->func_inst;
}

static struct usb_function *diag_alloc(struct usb_function_instance *fi)
{
	struct diag_opts *opts;
	struct diag_context *dev;

	opts = container_of(fi, struct diag_opts, func_inst);

	dev = diag_context_init(opts->name);
	if (IS_ERR(dev))
		return ERR_CAST(dev);

	return &dev->function;
}

DECLARE_USB_FUNCTION(diag, diag_alloc_inst, diag_alloc);

static int __init diag_init(void)
{
	struct device_node *np;
	int ret;

	INIT_LIST_HEAD(&diag_dev_list);

	fdiag_debugfs_init();

	ret = usb_function_register(&diagusb_func);
	if (ret) {
		pr_err("%s: failed to register diag %d\n", __func__, ret);
		return ret;
	}

	np = of_find_compatible_node(NULL, NULL, "qcom,msm-imem-diag-dload");
	if (!np)
		np = of_find_compatible_node(NULL, NULL, "qcom,android-usb");

	if (!np)
		pr_warn("diag: failed to find diag_dload imem node\n");

	diag_dload  = np ? of_iomap(np, 0) : NULL;

	return ret;
}

static void __exit diag_exit(void)
{
	struct list_head *act, *tmp;
	struct usb_diag_ch *_ch;
	unsigned long flags;

	if (diag_dload)
		iounmap(diag_dload);

	usb_function_unregister(&diagusb_func);

	fdiag_debugfs_remove();

	list_for_each_safe(act, tmp, &usb_diag_ch_list) {
		_ch = list_entry(act, struct usb_diag_ch, list);

		spin_lock_irqsave(&ch_lock, flags);
		/* Free if diagchar is not using the channel anymore */
		if (!_ch->priv) {
			list_del(&_ch->list);
			kfree(_ch);
		}
		spin_unlock_irqrestore(&ch_lock, flags);
	}

}

module_init(diag_init);
module_exit(diag_exit);

MODULE_DESCRIPTION("Diag function driver");
MODULE_LICENSE("GPL");
