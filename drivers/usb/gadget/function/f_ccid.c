// SPDX-License-Identifier: GPL-2.0-only
/*
 * f_ccid.c -- CCID function Driver
 *
 * Copyright (c) 2011, 2013, 2017, 2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/usb/ccid_desc.h>
#include <linux/usb/composite.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#include "f_ccid.h"

#define BULK_IN_BUFFER_SIZE sizeof(struct ccid_bulk_in_header)
#define BULK_OUT_BUFFER_SIZE 1024
#define CTRL_BUF_SIZE	4
#define FUNCTION_NAME	"ccid"
#define MAX_INST_NAME_LEN	40
#define CCID_CTRL_DEV_NAME	"ccid_ctrl"
#define CCID_BULK_DEV_NAME	"ccid_bulk"
#define CCID_NOTIFY_INTERVAL	5
#define CCID_NOTIFY_MAXPACKET	4

/* number of tx requests to allocate */
#define TX_REQ_MAX 4

struct ccid_ctrl_dev {
	atomic_t opened;
	struct list_head tx_q;
	wait_queue_head_t tx_wait_q;
	unsigned char buf[CTRL_BUF_SIZE];
	int tx_ctrl_done;
	struct cdev cdev;
};

struct ccid_bulk_dev {
	atomic_t error;
	atomic_t opened;
	atomic_t rx_req_busy;
	wait_queue_head_t read_wq;
	wait_queue_head_t write_wq;
	struct usb_request *rx_req;
	int rx_done;
	struct list_head tx_idle;
	struct cdev cdev;
};

struct ccid_opts {
	struct usb_function_instance func_inst;
	struct f_ccid *ccid;
};

struct f_ccid {
	struct usb_function function;
	int ifc_id;
	spinlock_t lock;
	atomic_t online;
	/* usb eps*/
	struct usb_ep *notify;
	struct usb_ep *in;
	struct usb_ep *out;
	struct usb_request *notify_req;
	struct ccid_ctrl_dev ctrl_dev;
	struct ccid_bulk_dev bulk_dev;
	int dtr_state;
};

#define MAX_INSTANCES		4

static int major;
static struct class *ccid_class;
static DEFINE_IDA(ccid_ida);
static DEFINE_MUTEX(ccid_ida_lock);

static inline struct f_ccid *ctrl_dev_to_ccid(struct ccid_ctrl_dev *d)
{
	return container_of(d, struct f_ccid, ctrl_dev);
}

static inline struct f_ccid *bulk_dev_to_ccid(struct ccid_bulk_dev *d)
{
	return container_of(d, struct f_ccid, bulk_dev);
}

/* Interface Descriptor: */
static struct usb_interface_descriptor ccid_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bNumEndpoints =	3,
	.bInterfaceClass =	USB_CLASS_CSCID,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
};
/* CCID Class Descriptor */
static struct usb_ccid_class_descriptor ccid_class_desc = {
	.bLength =		sizeof(ccid_class_desc),
	.bDescriptorType =	CCID_DECRIPTOR_TYPE,
	.bcdCCID =		CCID1_10,
	.bMaxSlotIndex =	0,
	/* This value indicates what voltages the CCID can supply to slots */
	.bVoltageSupport =	VOLTS_3_0,
	.dwProtocols =		PROTOCOL_TO,
	/* Default ICC clock frequency in KHz */
	.dwDefaultClock =	3580,
	/* Maximum supported ICC clock frequency in KHz */
	.dwMaximumClock =	3580,
	.bNumClockSupported =	0,
	/* Default ICC I/O data rate in bps */
	.dwDataRate =		9600,
	/* Maximum supported ICC I/O data rate in bps */
	.dwMaxDataRate =	9600,
	.bNumDataRatesSupported = 0,
	.dwMaxIFSD =		0,
	.dwSynchProtocols =	0,
	.dwMechanical =		0,
	/* This value indicates what intelligent features the CCID has */
	.dwFeatures =		CCID_FEATURES_EXC_SAPDU |
				CCID_FEATURES_AUTO_PNEGO |
				CCID_FEATURES_AUTO_BAUD |
				CCID_FEATURES_AUTO_CLOCK |
				CCID_FEATURES_AUTO_VOLT |
				CCID_FEATURES_AUTO_ACTIV |
				CCID_FEATURES_AUTO_PCONF,
	/* extended APDU level Message Length */
	.dwMaxCCIDMessageLength = 0x200,
	.bClassGetResponse =	0x0,
	.bClassEnvelope =	0x0,
	.wLcdLayout =		0,
	.bPINSupport =		0,
	.bMaxCCIDBusySlots =	1
};
/* Full speed support: */
static struct usb_endpoint_descriptor ccid_fs_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(CCID_NOTIFY_MAXPACKET),
	.bInterval =		1 << CCID_NOTIFY_INTERVAL,
};

static struct usb_endpoint_descriptor ccid_fs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize   =	cpu_to_le16(64),
};

static struct usb_endpoint_descriptor ccid_fs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize   =	cpu_to_le16(64),
};

static struct usb_descriptor_header *ccid_fs_descs[] = {
	(struct usb_descriptor_header *) &ccid_interface_desc,
	(struct usb_descriptor_header *) &ccid_class_desc,
	(struct usb_descriptor_header *) &ccid_fs_notify_desc,
	(struct usb_descriptor_header *) &ccid_fs_in_desc,
	(struct usb_descriptor_header *) &ccid_fs_out_desc,
	NULL,
};

/* High speed support: */
static struct usb_endpoint_descriptor ccid_hs_notify_desc  = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(CCID_NOTIFY_MAXPACKET),
	.bInterval =		CCID_NOTIFY_INTERVAL + 4,
};

static struct usb_endpoint_descriptor ccid_hs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor ccid_hs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *ccid_hs_descs[] = {
	(struct usb_descriptor_header *) &ccid_interface_desc,
	(struct usb_descriptor_header *) &ccid_class_desc,
	(struct usb_descriptor_header *) &ccid_hs_notify_desc,
	(struct usb_descriptor_header *) &ccid_hs_in_desc,
	(struct usb_descriptor_header *) &ccid_hs_out_desc,
	NULL,
};

/* Super speed support: */
static struct usb_endpoint_descriptor ccid_ss_notify_desc  = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(CCID_NOTIFY_MAXPACKET),
	.bInterval =		CCID_NOTIFY_INTERVAL + 4,
};

static struct usb_ss_ep_comp_descriptor ccid_ss_notify_comp_desc = {
	.bLength =		sizeof(ccid_ss_notify_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =	0, */
};

static struct usb_endpoint_descriptor ccid_ss_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ccid_ss_in_comp_desc = {
	.bLength =		sizeof(ccid_ss_in_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =	0, */
};

static struct usb_endpoint_descriptor ccid_ss_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ccid_ss_out_comp_desc = {
	.bLength =		sizeof(ccid_ss_out_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =	0, */
};

static struct usb_descriptor_header *ccid_ss_descs[] = {
	(struct usb_descriptor_header *) &ccid_interface_desc,
	(struct usb_descriptor_header *) &ccid_class_desc,
	(struct usb_descriptor_header *) &ccid_ss_notify_desc,
	(struct usb_descriptor_header *) &ccid_ss_notify_comp_desc,
	(struct usb_descriptor_header *) &ccid_ss_in_desc,
	(struct usb_descriptor_header *) &ccid_ss_in_comp_desc,
	(struct usb_descriptor_header *) &ccid_ss_out_desc,
	(struct usb_descriptor_header *) &ccid_ss_out_comp_desc,
	NULL,
};

static inline struct f_ccid *func_to_ccid(struct usb_function *f)
{
	return container_of(f, struct f_ccid, function);
}

static void ccid_req_put(struct f_ccid *ccid_dev, struct list_head *head,
		struct usb_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&ccid_dev->lock, flags);
	list_add_tail(&req->list, head);
	spin_unlock_irqrestore(&ccid_dev->lock, flags);
}

static struct usb_request *ccid_req_get(struct f_ccid *ccid_dev,
					struct list_head *head)
{
	unsigned long flags;
	struct usb_request *req = NULL;

	spin_lock_irqsave(&ccid_dev->lock, flags);
	if (!list_empty(head)) {
		req = list_first_entry(head, struct usb_request, list);
		list_del(&req->list);
	}
	spin_unlock_irqrestore(&ccid_dev->lock, flags);
	return req;
}

static void ccid_notify_complete(struct usb_ep *ep, struct usb_request *req)
{
	switch (req->status) {
	case -ECONNRESET:
	case -ESHUTDOWN:
	case 0:
		break;
	default:
		pr_err("CCID notify ep error %d\n", req->status);
	}
}

static void ccid_bulk_complete_in(struct usb_ep *ep, struct usb_request *req)
{
	struct f_ccid *ccid_dev = req->context;
	struct ccid_bulk_dev *bulk_dev = &ccid_dev->bulk_dev;

	if (req->status != 0)
		atomic_set(&bulk_dev->error, 1);

	ccid_req_put(ccid_dev, &bulk_dev->tx_idle, req);
	wake_up(&bulk_dev->write_wq);
}

static void ccid_bulk_complete_out(struct usb_ep *ep, struct usb_request *req)
{
	struct f_ccid *ccid_dev = req->context;
	struct ccid_bulk_dev *bulk_dev = &ccid_dev->bulk_dev;

	if (req->status != 0)
		atomic_set(&bulk_dev->error, 1);

	bulk_dev->rx_done = 1;
	wake_up(&bulk_dev->read_wq);
}

static struct usb_request *
ccid_request_alloc(struct usb_ep *ep, size_t len, gfp_t kmalloc_flags)
{
	struct usb_request *req;

	req = usb_ep_alloc_request(ep, kmalloc_flags);

	if (req != NULL) {
		req->length = len;
		req->buf = kmalloc(len, kmalloc_flags);
		if (req->buf == NULL) {
			usb_ep_free_request(ep, req);
			req = NULL;
		}
	}

	return req ? req : ERR_PTR(-ENOMEM);
}

static void ccid_request_free(struct usb_request *req, struct usb_ep *ep)
{
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

static int
ccid_function_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct f_ccid *ccid_dev = container_of(f, struct f_ccid, function);
	struct ccid_ctrl_dev *ctrl_dev = &ccid_dev->ctrl_dev;
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request      *req = cdev->req;
	int ret = -EOPNOTSUPP;
	u16 w_index = le16_to_cpu(ctrl->wIndex);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u16 w_length = le16_to_cpu(ctrl->wLength);

	if (!atomic_read(&ccid_dev->online))
		return -ENOTCONN;

	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| CCIDGENERICREQ_ABORT:
		if (w_length != 0)
			goto invalid;
		ctrl_dev->buf[0] = CCIDGENERICREQ_ABORT;
		ctrl_dev->buf[1] = w_value & 0xFF;
		ctrl_dev->buf[2] = (w_value >> 8) & 0xFF;
		ctrl_dev->buf[3] = 0x00;
		ctrl_dev->tx_ctrl_done = 1;
		wake_up(&ctrl_dev->tx_wait_q);
		ret = 0;
		break;

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| CCIDGENERICREQ_GET_CLOCK_FREQUENCIES:
		*(u32 *) req->buf =
				cpu_to_le32(ccid_class_desc.dwDefaultClock);
		ret = min_t(u32, w_length,
				sizeof(ccid_class_desc.dwDefaultClock));
		break;

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| CCIDGENERICREQ_GET_DATA_RATES:
		*(u32 *) req->buf = cpu_to_le32(ccid_class_desc.dwDataRate);
		ret = min_t(u32, w_length, sizeof(ccid_class_desc.dwDataRate));
		break;

	default:
invalid:
	pr_debug("invalid control req%02x.%02x v%04x i%04x l%d\n",
		ctrl->bRequestType, ctrl->bRequest,
		w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (ret >= 0) {
		pr_debug("ccid req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->length = ret;
		ret = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (ret < 0)
			pr_err("ccid ep0 enqueue err %d\n", ret);
	}

	return ret;
}

static void ccid_function_disable(struct usb_function *f)
{
	struct f_ccid *ccid_dev = func_to_ccid(f);
	struct ccid_bulk_dev *bulk_dev = &ccid_dev->bulk_dev;
	struct ccid_ctrl_dev *ctrl_dev = &ccid_dev->ctrl_dev;

	/* Disable endpoints */
	usb_ep_disable(ccid_dev->notify);
	usb_ep_disable(ccid_dev->in);
	usb_ep_disable(ccid_dev->out);

	ccid_dev->dtr_state = 0;
	atomic_set(&ccid_dev->online, 0);
	/* Wake up threads */
	wake_up(&bulk_dev->write_wq);
	wake_up(&bulk_dev->read_wq);
	wake_up(&ctrl_dev->tx_wait_q);

}

static int
ccid_function_set_alt(struct usb_function *f, unsigned int intf,
		unsigned int alt)
{
	struct f_ccid *ccid_dev = func_to_ccid(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret = 0;

	/* choose the descriptors and enable endpoints */
	ret = config_ep_by_speed(cdev->gadget, f, ccid_dev->notify);
	if (ret) {
		ccid_dev->notify->desc = NULL;
		pr_err("%s: config_ep_by_speed failed for ep#%s, err#%d\n",
				__func__, ccid_dev->notify->name, ret);
		return ret;
	}
	ret = usb_ep_enable(ccid_dev->notify);
	if (ret) {
		pr_err("%s: usb ep#%s enable failed, err#%d\n",
				__func__, ccid_dev->notify->name, ret);
		return ret;
	}
	ccid_dev->notify->driver_data = ccid_dev;

	ret = config_ep_by_speed(cdev->gadget, f, ccid_dev->in);
	if (ret) {
		ccid_dev->in->desc = NULL;
		pr_err("%s: config_ep_by_speed failed for ep#%s, err#%d\n",
				__func__, ccid_dev->in->name, ret);
		goto disable_ep_notify;
	}
	ret = usb_ep_enable(ccid_dev->in);
	if (ret) {
		pr_err("%s: usb ep#%s enable failed, err#%d\n",
				__func__, ccid_dev->in->name, ret);
		goto disable_ep_notify;
	}

	ret = config_ep_by_speed(cdev->gadget, f, ccid_dev->out);
	if (ret) {
		ccid_dev->out->desc = NULL;
		pr_err("%s: config_ep_by_speed failed for ep#%s, err#%d\n",
				__func__, ccid_dev->out->name, ret);
		goto disable_ep_in;
	}
	ret = usb_ep_enable(ccid_dev->out);
	if (ret) {
		pr_err("%s: usb ep#%s enable failed, err#%d\n",
				__func__, ccid_dev->out->name, ret);
		goto disable_ep_in;
	}
	ccid_dev->dtr_state = 1;
	atomic_set(&ccid_dev->online, 1);
	return ret;

disable_ep_in:
	usb_ep_disable(ccid_dev->in);
disable_ep_notify:
	usb_ep_disable(ccid_dev->notify);
	ccid_dev->notify->driver_data = NULL;
	return ret;
}

static void ccid_function_unbind(struct usb_configuration *c,
					struct usb_function *f)
{
	struct f_ccid *ccid_dev = func_to_ccid(f);
	struct ccid_bulk_dev *bulk_dev = &ccid_dev->bulk_dev;
	struct usb_request *req;

	/* Free endpoint related requests */
	ccid_request_free(ccid_dev->notify_req, ccid_dev->notify);
	if (!atomic_read(&bulk_dev->rx_req_busy))
		ccid_request_free(bulk_dev->rx_req, ccid_dev->out);
	while ((req = ccid_req_get(ccid_dev, &bulk_dev->tx_idle)))
		ccid_request_free(req, ccid_dev->in);

	usb_free_all_descriptors(f);
}

static int ccid_function_bind(struct usb_configuration *c,
					struct usb_function *f)
{
	struct f_ccid *ccid_dev = func_to_ccid(f);
	struct usb_ep *ep;
	struct usb_composite_dev *cdev = c->cdev;
	struct ccid_bulk_dev *bulk_dev = &ccid_dev->bulk_dev;
	struct usb_request *req;
	int ret = -ENODEV;
	int i;

	ccid_dev->ifc_id = usb_interface_id(c, f);
	if (ccid_dev->ifc_id < 0) {
		pr_err("%s: unable to allocate ifc id, err:%d\n",
				__func__, ccid_dev->ifc_id);
		return ccid_dev->ifc_id;
	}
	ccid_interface_desc.bInterfaceNumber = ccid_dev->ifc_id;

	ep = usb_ep_autoconfig(cdev->gadget, &ccid_fs_notify_desc);
	if (!ep) {
		pr_err("%s: usb epnotify autoconfig failed\n", __func__);
		return -ENODEV;
	}
	ccid_dev->notify = ep;
	ep->driver_data = cdev;

	ep = usb_ep_autoconfig(cdev->gadget, &ccid_fs_in_desc);
	if (!ep) {
		pr_err("%s: usb epin autoconfig failed\n", __func__);
		ret = -ENODEV;
		goto ep_auto_in_fail;
	}
	ccid_dev->in = ep;
	ep->driver_data = cdev;

	ep = usb_ep_autoconfig(cdev->gadget, &ccid_fs_out_desc);
	if (!ep) {
		pr_err("%s: usb epout autoconfig failed\n", __func__);
		ret = -ENODEV;
		goto ep_auto_out_fail;
	}
	ccid_dev->out = ep;
	ep->driver_data = cdev;

	/*
	 * support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	ccid_hs_in_desc.bEndpointAddress = ccid_fs_in_desc.bEndpointAddress;
	ccid_hs_out_desc.bEndpointAddress = ccid_fs_out_desc.bEndpointAddress;
	ccid_hs_notify_desc.bEndpointAddress =
					ccid_fs_notify_desc.bEndpointAddress;


	ccid_ss_in_desc.bEndpointAddress = ccid_fs_in_desc.bEndpointAddress;
	ccid_ss_out_desc.bEndpointAddress = ccid_fs_out_desc.bEndpointAddress;
	ccid_ss_notify_desc.bEndpointAddress =
					ccid_fs_notify_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, ccid_fs_descs, ccid_hs_descs,
						ccid_ss_descs, ccid_ss_descs);
	if (ret)
		goto assign_desc_fail;

	pr_debug("%s: CCID %s Speed, IN:%s OUT:%s\n", __func__,
			gadget_is_dualspeed(cdev->gadget) ? "dual" : "full",
			ccid_dev->in->name, ccid_dev->out->name);

	ccid_dev->notify_req = ccid_request_alloc(ccid_dev->notify,
			sizeof(struct usb_ccid_notification), GFP_KERNEL);
	if (IS_ERR(ccid_dev->notify_req)) {
		pr_err("%s: unable to allocate memory for notify req\n",
				__func__);
		goto notify_alloc_fail;
	}
	ccid_dev->notify_req->complete = ccid_notify_complete;
	ccid_dev->notify_req->context = ccid_dev;

	/* now allocate requests for our endpoints */
	req = ccid_request_alloc(ccid_dev->out, BULK_OUT_BUFFER_SIZE,
							GFP_KERNEL);
	if (IS_ERR(req)) {
		pr_err("%s: unable to allocate memory for out req\n",
				__func__);
		ret = PTR_ERR(req);
		goto out_alloc_fail;
	}
	req->complete = ccid_bulk_complete_out;
	req->context = ccid_dev;
	bulk_dev->rx_req = req;

	for (i = 0; i < TX_REQ_MAX; i++) {
		req = ccid_request_alloc(ccid_dev->in, BULK_IN_BUFFER_SIZE,
				GFP_KERNEL);
		if (IS_ERR(req)) {
			pr_err("%s: unable to allocate memory for in req\n",
					__func__);
			ret = PTR_ERR(req);
			goto in_alloc_fail;
		}
		req->complete = ccid_bulk_complete_in;
		req->context = ccid_dev;
		ccid_req_put(ccid_dev, &bulk_dev->tx_idle, req);
	}

	return 0;

in_alloc_fail:
	ccid_request_free(bulk_dev->rx_req, ccid_dev->out);
out_alloc_fail:
	ccid_request_free(ccid_dev->notify_req, ccid_dev->notify);
notify_alloc_fail:
	usb_free_all_descriptors(f);
assign_desc_fail:
	ccid_dev->out->driver_data = NULL;
	ccid_dev->out = NULL;
ep_auto_out_fail:
	ccid_dev->in->driver_data = NULL;
	ccid_dev->in = NULL;
ep_auto_in_fail:
	ccid_dev->notify->driver_data = NULL;
	ccid_dev->notify = NULL;

	return ret;
}

static int ccid_bulk_open(struct inode *inode, struct file *fp)
{
	struct ccid_bulk_dev *bulk_dev =  container_of(inode->i_cdev,
						struct ccid_bulk_dev, cdev);
	struct f_ccid *ccid_dev = bulk_dev_to_ccid(bulk_dev);
	unsigned long flags;

	if (!atomic_read(&ccid_dev->online)) {
		pr_debug("%s: USB cable not connected\n", __func__);
		return -ENODEV;
	}

	if (atomic_read(&bulk_dev->opened)) {
		pr_debug("%s: bulk device is already opened\n", __func__);
		return -EBUSY;
	}
	atomic_set(&bulk_dev->opened, 1);
	/* clear the error latch */
	atomic_set(&bulk_dev->error, 0);
	spin_lock_irqsave(&ccid_dev->lock, flags);
	fp->private_data = ccid_dev;
	spin_unlock_irqrestore(&ccid_dev->lock, flags);

	return 0;
}

static int ccid_bulk_release(struct inode *ip, struct file *fp)
{
	struct f_ccid *ccid_dev =  fp->private_data;
	struct ccid_bulk_dev *bulk_dev = &ccid_dev->bulk_dev;

	atomic_set(&bulk_dev->opened, 0);
	return 0;
}

static ssize_t ccid_bulk_read(struct file *fp, char __user *buf,
				size_t count, loff_t *pos)
{
	struct f_ccid *ccid_dev =  fp->private_data;
	struct ccid_bulk_dev *bulk_dev = &ccid_dev->bulk_dev;
	struct usb_request *req;
	int r = count, xfer, len;
	int ret;
	unsigned long flags;

	pr_debug("%s: %zu bytes\n", __func__, count);

	if (count > BULK_OUT_BUFFER_SIZE) {
		pr_err("%s: max_buffer_size:%d given_pkt_size:%zu\n",
				__func__, BULK_OUT_BUFFER_SIZE, count);
		return -ENOMEM;
	}

	if (atomic_read(&bulk_dev->error)) {
		r = -EIO;
		pr_err("%s bulk_dev_error\n", __func__);
		goto done;
	}

	len = ALIGN(count, ccid_dev->out->maxpacket);
requeue_req:
	spin_lock_irqsave(&ccid_dev->lock, flags);
	if (!atomic_read(&ccid_dev->online)) {
		spin_unlock_irqrestore(&ccid_dev->lock, flags);
		pr_debug("%s: USB cable not connected\n", __func__);
		return -ENODEV;
	}
	/* queue a request */
	req = bulk_dev->rx_req;
	req->length = len;
	bulk_dev->rx_done = 0;
	spin_unlock_irqrestore(&ccid_dev->lock, flags);
	ret = usb_ep_queue(ccid_dev->out, req, GFP_KERNEL);
	if (ret < 0) {
		r = -EIO;
		pr_err("%s usb ep queue failed\n", __func__);
		atomic_set(&bulk_dev->error, 1);
		goto done;
	}
	/* wait for a request to complete */
	ret = wait_event_interruptible(bulk_dev->read_wq, bulk_dev->rx_done ||
					atomic_read(&bulk_dev->error) ||
					!atomic_read(&ccid_dev->online));
	if (ret < 0) {
		atomic_set(&bulk_dev->error, 1);
		r = ret;
		usb_ep_dequeue(ccid_dev->out, req);
		goto done;
	}
	if (!atomic_read(&bulk_dev->error)) {
		spin_lock_irqsave(&ccid_dev->lock, flags);
		if (!atomic_read(&ccid_dev->online)) {
			spin_unlock_irqrestore(&ccid_dev->lock, flags);
			pr_debug("%s: USB cable not connected\n", __func__);
			r = -ENODEV;
			goto done;
		}
		/* If we got a 0-len packet, throw it back and try again. */
		if (req->actual == 0) {
			spin_unlock_irqrestore(&ccid_dev->lock, flags);
			goto requeue_req;
		}
		if (req->actual > count)
			pr_err("%s More data received(%d) than required(%zu)\n",
						__func__, req->actual, count);
		xfer = (req->actual < count) ? req->actual : count;
		atomic_set(&bulk_dev->rx_req_busy, 1);
		spin_unlock_irqrestore(&ccid_dev->lock, flags);

		if (copy_to_user(buf, req->buf, xfer))
			r = -EFAULT;

		spin_lock_irqsave(&ccid_dev->lock, flags);
		atomic_set(&bulk_dev->rx_req_busy, 0);
		if (!atomic_read(&ccid_dev->online)) {
			ccid_request_free(bulk_dev->rx_req, ccid_dev->out);
			spin_unlock_irqrestore(&ccid_dev->lock, flags);
			pr_debug("%s: USB cable not connected\n", __func__);
			r = -ENODEV;
			goto done;
		} else {
			r = xfer;
		}
		spin_unlock_irqrestore(&ccid_dev->lock, flags);
	} else {
		r = -EIO;
	}
done:
	pr_debug("%s returning %d\n", __func__, r);
	return r;
}

static ssize_t ccid_bulk_write(struct file *fp, const char __user *buf,
				 size_t count, loff_t *pos)
{
	struct f_ccid *ccid_dev =  fp->private_data;
	struct ccid_bulk_dev *bulk_dev = &ccid_dev->bulk_dev;
	struct usb_request *req = 0;
	int r = count;
	int ret;
	unsigned long flags;

	pr_debug("%s: %zu bytes\n", __func__, count);

	if (!atomic_read(&ccid_dev->online)) {
		pr_debug("%s: USB cable not connected\n", __func__);
		return -ENODEV;
	}

	if (!count) {
		pr_err("%s: zero length ctrl pkt\n", __func__);
		return -ENODEV;
	}
	if (count > BULK_IN_BUFFER_SIZE) {
		pr_err("%s: max_buffer_size:%zu given_pkt_size:%zu\n",
				__func__, BULK_IN_BUFFER_SIZE, count);
		return -ENOMEM;
	}


	/* get an idle tx request to use */
	ret = wait_event_interruptible(bulk_dev->write_wq,
		((req = ccid_req_get(ccid_dev, &bulk_dev->tx_idle)) ||
		 atomic_read(&bulk_dev->error)));

	if (ret < 0) {
		r = ret;
		goto done;
	}

	if (!req || atomic_read(&bulk_dev->error)) {
		pr_err(" %s dev->error\n", __func__);
		r = -EIO;
		goto done;
	}
	if (copy_from_user(req->buf, buf, count)) {
		if (!atomic_read(&ccid_dev->online)) {
			pr_debug("%s: USB cable not connected\n",
						__func__);
			ccid_request_free(req, ccid_dev->in);
			r = -ENODEV;
		} else {
			ccid_req_put(ccid_dev, &bulk_dev->tx_idle, req);
			r = -EFAULT;
		}
		goto done;
	}
	req->length = count;
	ret = usb_ep_queue(ccid_dev->in, req, GFP_KERNEL);
	if (ret < 0) {
		pr_debug("%s: xfer error %d\n", __func__, ret);
		atomic_set(&bulk_dev->error, 1);
		ccid_req_put(ccid_dev, &bulk_dev->tx_idle, req);
		r = -EIO;
		spin_lock_irqsave(&ccid_dev->lock, flags);
		if (!atomic_read(&ccid_dev->online)) {
			spin_unlock_irqrestore(&ccid_dev->lock, flags);
			pr_debug("%s: USB cable not connected\n",
							__func__);
			while ((req = ccid_req_get(ccid_dev,
						&bulk_dev->tx_idle)))
				ccid_request_free(req, ccid_dev->in);
			r = -ENODEV;
		}
		spin_unlock_irqrestore(&ccid_dev->lock, flags);
		goto done;
	}
done:
	pr_debug("%s returning %d\n", __func__, r);
	return r;
}

static const struct file_operations ccid_bulk_fops = {
	.owner = THIS_MODULE,
	.read = ccid_bulk_read,
	.write = ccid_bulk_write,
	.open = ccid_bulk_open,
	.release = ccid_bulk_release,
};

static int ccid_ctrl_open(struct inode *inode, struct file *fp)
{
	struct ccid_ctrl_dev *ctrl_dev =  container_of(inode->i_cdev,
						struct ccid_ctrl_dev, cdev);
	struct f_ccid *ccid_dev = ctrl_dev_to_ccid(ctrl_dev);
	unsigned long flags;

	if (!atomic_read(&ccid_dev->online)) {
		pr_debug("%s: USB cable not connected\n", __func__);
		return -ENODEV;
	}
	if (atomic_read(&ctrl_dev->opened)) {
		pr_debug("%s: ctrl device is already opened\n", __func__);
		return -EBUSY;
	}
	atomic_set(&ctrl_dev->opened, 1);
	spin_lock_irqsave(&ccid_dev->lock, flags);
	fp->private_data = ccid_dev;
	spin_unlock_irqrestore(&ccid_dev->lock, flags);

	return 0;
}


static int ccid_ctrl_release(struct inode *inode, struct file *fp)
{
	struct f_ccid *ccid_dev = fp->private_data;
	struct ccid_ctrl_dev *ctrl_dev = &ccid_dev->ctrl_dev;

	atomic_set(&ctrl_dev->opened, 0);

	return 0;
}

static ssize_t ccid_ctrl_read(struct file *fp, char __user *buf,
		      size_t count, loff_t *ppos)
{
	struct f_ccid *ccid_dev = fp->private_data;
	struct ccid_ctrl_dev *ctrl_dev = &ccid_dev->ctrl_dev;
	int ret = 0;

	if (!atomic_read(&ccid_dev->online)) {
		pr_debug("%s: USB cable not connected\n", __func__);
		return -ENODEV;
	}
	if (count > CTRL_BUF_SIZE)
		count = CTRL_BUF_SIZE;

	ret = wait_event_interruptible(ctrl_dev->tx_wait_q,
					 ctrl_dev->tx_ctrl_done ||
					!atomic_read(&ccid_dev->online));
	if (ret < 0)
		return ret;
	ctrl_dev->tx_ctrl_done = 0;

	if (!atomic_read(&ccid_dev->online)) {
		pr_debug("%s: USB cable not connected\n", __func__);
		return -ENODEV;
	}
	ret = copy_to_user(buf, ctrl_dev->buf, count);
	if (ret)
		return -EFAULT;

	return count;
}

static long
ccid_ctrl_ioctl(struct file *fp, unsigned int cmd, u_long arg)
{
	struct f_ccid *ccid_dev = fp->private_data;
	struct usb_request              *req = ccid_dev->notify_req;
	struct usb_ccid_notification     *ccid_notify = req->buf;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	switch (cmd) {
	case CCID_NOTIFY_CARD:
		if (copy_from_user(ccid_notify, argp,
				sizeof(struct usb_ccid_notification)))
			return -EFAULT;
		req->length = 2;
		break;
	case CCID_NOTIFY_HWERROR:
		if (copy_from_user(ccid_notify, argp,
				sizeof(struct usb_ccid_notification)))
			return -EFAULT;
		req->length = 4;
		break;
	case CCID_READ_DTR:
		if (copy_to_user((int *)arg, &ccid_dev->dtr_state, sizeof(int)))
			return -EFAULT;
		return 0;
	}
	ret = usb_ep_queue(ccid_dev->notify, ccid_dev->notify_req, GFP_KERNEL);
	if (ret < 0) {
		pr_err("ccid notify ep enqueue error %d\n", ret);
		return ret;
	}
	return 0;
}

static const struct file_operations ccid_ctrl_fops = {
	.owner		= THIS_MODULE,
	.open		= ccid_ctrl_open,
	.release	= ccid_ctrl_release,
	.read		= ccid_ctrl_read,
	.unlocked_ioctl	= ccid_ctrl_ioctl,
};

static int ccid_cdev_init(struct cdev *cdev, const struct file_operations *fops,
		const char *name)
{
	struct device *dev;
	int ret, minor;

	minor = ida_simple_get(&ccid_ida, 0, MAX_INSTANCES, GFP_KERNEL);
	if (minor < 0) {
		pr_err("%s: No more minor numbers left! rc:%d\n", __func__,
				minor);
		return minor;
	}

	cdev_init(cdev, fops);
	ret = cdev_add(cdev, MKDEV(major, minor), 1);
	if (ret) {
		pr_err("Failed to add cdev for (%s)\n", name);
		goto err_cdev_add;
	}

	dev = device_create(ccid_class, NULL, MKDEV(major, minor), NULL, name);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_create_dev;
	}

	return 0;

err_create_dev:
	cdev_del(cdev);
err_cdev_add:
	ida_simple_remove(&ccid_ida, minor);
	return ret;
}

static void ccid_cdev_free(struct cdev *cdev)
{
	int minor = MINOR(cdev->dev);

	device_destroy(ccid_class, cdev->dev);
	cdev_del(cdev);
	ida_simple_remove(&ccid_ida, minor);
}

static void ccid_free_func(struct usb_function *f)
{ }

static int ccid_bind_config(struct f_ccid *ccid_dev)
{
	ccid_dev->function.name = FUNCTION_NAME;
	ccid_dev->function.bind = ccid_function_bind;
	ccid_dev->function.unbind = ccid_function_unbind;
	ccid_dev->function.set_alt = ccid_function_set_alt;
	ccid_dev->function.setup = ccid_function_setup;
	ccid_dev->function.disable = ccid_function_disable;
	ccid_dev->function.free_func = ccid_free_func;

	return 0;
}

static int ccid_alloc_chrdev_region(void)
{
	int ret;
	dev_t dev;

	ccid_class = class_create(THIS_MODULE, "ccid_usb");
	if (IS_ERR(ccid_class)) {
		ret = PTR_ERR(ccid_class);
		ccid_class = NULL;
		pr_err("%s: class_create() failed:%d\n", __func__, ret);
		return ret;
	}

	ret = alloc_chrdev_region(&dev, 0, MAX_INSTANCES, "ccid_usb");
	if (ret) {
		pr_err("%s: alloc_chrdev_region() failed:%d\n", __func__, ret);
		class_destroy(ccid_class);
		ccid_class = NULL;
		return ret;
	}

	major = MAJOR(dev);

	return 0;
}

static void ccid_free_chrdev_region(void)
{
	mutex_lock(&ccid_ida_lock);
	if (ida_is_empty(&ccid_ida)) {
		if (major) {
			unregister_chrdev_region(MKDEV(major, 0),
					MAX_INSTANCES);
			major = 0;
		}

		if (ccid_class) {
			class_destroy(ccid_class);
			ccid_class = NULL;
		}
	}
	mutex_unlock(&ccid_ida_lock);
}

static struct f_ccid *ccid_setup(void)
{
	struct f_ccid  *ccid_dev;
	int ret;

	ccid_dev = kzalloc(sizeof(*ccid_dev), GFP_KERNEL);
	if (!ccid_dev) {
		ret = -ENOMEM;
		goto error;
	}

	spin_lock_init(&ccid_dev->lock);
	INIT_LIST_HEAD(&ccid_dev->ctrl_dev.tx_q);
	init_waitqueue_head(&ccid_dev->ctrl_dev.tx_wait_q);
	init_waitqueue_head(&ccid_dev->bulk_dev.read_wq);
	init_waitqueue_head(&ccid_dev->bulk_dev.write_wq);
	INIT_LIST_HEAD(&ccid_dev->bulk_dev.tx_idle);

	mutex_lock(&ccid_ida_lock);
	if (ida_is_empty(&ccid_ida)) {
		ret = ccid_alloc_chrdev_region();
		if (ret) {
			mutex_unlock(&ccid_ida_lock);
			goto err_chrdev;
		}
	}
	mutex_unlock(&ccid_ida_lock);

	ret = ccid_cdev_init(&ccid_dev->ctrl_dev.cdev, &ccid_ctrl_fops,
			CCID_CTRL_DEV_NAME);
	if (ret) {
		pr_err("%s: ccid_ctrl_device_init failed, err:%d\n",
				__func__, ret);
		goto err_ctrl_init;
	}

	ret = ccid_cdev_init(&ccid_dev->bulk_dev.cdev, &ccid_bulk_fops,
			CCID_BULK_DEV_NAME);
	if (ret) {
		pr_err("%s: ccid_bulk_device_init failed, err:%d\n",
				__func__, ret);
		goto err_bulk_init;
	}

	return ccid_dev;
err_bulk_init:
	ccid_cdev_free(&ccid_dev->ctrl_dev.cdev);
err_ctrl_init:
	ccid_free_chrdev_region();
err_chrdev:
	kfree(ccid_dev);
error:
	pr_err("ccid gadget driver failed to initialize\n");
	return ERR_PTR(ret);
}

static inline struct ccid_opts *to_ccid_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct ccid_opts,
			    func_inst.group);
}

static void ccid_attr_release(struct config_item *item)
{
	struct ccid_opts *opts = to_ccid_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations ccid_item_ops = {
	.release        = ccid_attr_release,
};

static struct config_item_type ccid_func_type = {
	.ct_item_ops    = &ccid_item_ops,
	.ct_owner       = THIS_MODULE,
};

static int ccid_set_inst_name(struct usb_function_instance *fi,
	const char *name)
{
	int name_len;
	struct f_ccid *ccid;
	struct ccid_opts *opts = container_of(fi, struct ccid_opts, func_inst);

	name_len = strlen(name) + 1;
	if (name_len > MAX_INST_NAME_LEN)
		return -ENAMETOOLONG;

	ccid = ccid_setup();
	if (IS_ERR(ccid))
		return PTR_ERR(ccid);

	opts->ccid = ccid;

	return 0;
}

static void ccid_free_inst(struct usb_function_instance *f)
{
	struct ccid_opts *opts = container_of(f, struct ccid_opts, func_inst);

	if (!opts->ccid)
		return;

	ccid_cdev_free(&opts->ccid->ctrl_dev.cdev);
	ccid_cdev_free(&opts->ccid->bulk_dev.cdev);
	ccid_free_chrdev_region();

	kfree(opts->ccid);
	kfree(opts);
}


static struct usb_function_instance *ccid_alloc_inst(void)
{
	struct ccid_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	opts->func_inst.set_inst_name = ccid_set_inst_name;
	opts->func_inst.free_func_inst = ccid_free_inst;
	config_group_init_type_name(&opts->func_inst.group, "",
				    &ccid_func_type);

	return &opts->func_inst;
}

static struct usb_function *ccid_alloc(struct usb_function_instance *fi)
{
	struct ccid_opts *opts;
	int ret;

	opts = container_of(fi, struct ccid_opts, func_inst);

	ret = ccid_bind_config(opts->ccid);
	if (ret)
		return ERR_PTR(ret);

	return &opts->ccid->function;
}

DECLARE_USB_FUNCTION_INIT(ccid, ccid_alloc_inst, ccid_alloc);
MODULE_DESCRIPTION("USB CCID function Driver");
MODULE_LICENSE("GPL");
