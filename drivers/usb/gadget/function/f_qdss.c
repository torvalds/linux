// SPDX-License-Identifier: GPL-2.0-only
/*
 * f_qdss.c -- QDSS function Driver
 *
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/usb/usb_qdss.h>
#include <linux/usb/cdc.h>
#include <linux/usb/dwc3-msm.h>

#include "f_qdss.h"

static void *_qdss_ipc_log;

#define NUM_PAGES	10 /* # of pages for ipc logging */

#ifdef CONFIG_DYNAMIC_DEBUG
#define qdss_log(fmt, ...) do { \
	ipc_log_string(_qdss_ipc_log, "%s: " fmt,  __func__, ##__VA_ARGS__); \
	dynamic_pr_debug("%s: " fmt, __func__, ##__VA_ARGS__); \
} while (0)
#else
#define qdss_log(fmt, ...) \
	ipc_log_string(_qdss_ipc_log, "%s: " fmt,  __func__, ##__VA_ARGS__)
#endif

static DEFINE_SPINLOCK(channel_lock);
static LIST_HEAD(usb_qdss_ch_list);

static struct usb_interface_descriptor qdss_data_intf_desc = {
	.bLength            =	sizeof(qdss_data_intf_desc),
	.bDescriptorType    =	USB_DT_INTERFACE,
	.bAlternateSetting  =   0,
	.bNumEndpoints      =	1,
	.bInterfaceClass    =	USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass =	USB_SUBCLASS_VENDOR_SPEC,
	.bInterfaceProtocol =	0x70,
};

static struct usb_endpoint_descriptor qdss_hs_data_desc = {
	.bLength              =	 USB_DT_ENDPOINT_SIZE,
	.bDescriptorType      =	 USB_DT_ENDPOINT,
	.bEndpointAddress     =	 USB_DIR_IN,
	.bmAttributes         =	 USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize       =	 cpu_to_le16(512),
};

static struct usb_endpoint_descriptor qdss_ss_data_desc = {
	.bLength              =	 USB_DT_ENDPOINT_SIZE,
	.bDescriptorType      =	 USB_DT_ENDPOINT,
	.bEndpointAddress     =	 USB_DIR_IN,
	.bmAttributes         =  USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize       =	 cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor qdss_data_ep_comp_desc = {
	.bLength              =	 sizeof(qdss_data_ep_comp_desc),
	.bDescriptorType      =	 USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst            =	 1,
	.bmAttributes         =	 0,
	.wBytesPerInterval    =	 0,
};

static struct usb_interface_descriptor qdss_ctrl_intf_desc = {
	.bLength            =	sizeof(qdss_ctrl_intf_desc),
	.bDescriptorType    =	USB_DT_INTERFACE,
	.bAlternateSetting  =   0,
	.bNumEndpoints      =	2,
	.bInterfaceClass    =	USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass =	USB_SUBCLASS_VENDOR_SPEC,
	.bInterfaceProtocol =	0x70,
};

static struct usb_endpoint_descriptor qdss_hs_ctrl_in_desc = {
	.bLength            =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    =	USB_DT_ENDPOINT,
	.bEndpointAddress   =	USB_DIR_IN,
	.bmAttributes       =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize     =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor qdss_ss_ctrl_in_desc = {
	.bLength            =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    =	USB_DT_ENDPOINT,
	.bEndpointAddress   =	USB_DIR_IN,
	.bmAttributes       =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize     =	cpu_to_le16(1024),
};

static struct usb_endpoint_descriptor qdss_hs_ctrl_out_desc = {
	.bLength            =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    =	USB_DT_ENDPOINT,
	.bEndpointAddress   =	USB_DIR_OUT,
	.bmAttributes       =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize     =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor qdss_ss_ctrl_out_desc = {
	.bLength            =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    =	USB_DT_ENDPOINT,
	.bEndpointAddress   =	USB_DIR_OUT,
	.bmAttributes       =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize     =	cpu_to_le16(0x400),
};

static struct usb_ss_ep_comp_descriptor qdss_ctrl_in_ep_comp_desc = {
	.bLength            =	sizeof(qdss_ctrl_in_ep_comp_desc),
	.bDescriptorType    =	USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst          =	0,
	.bmAttributes       =	0,
	.wBytesPerInterval  =	0,
};

static struct usb_ss_ep_comp_descriptor qdss_ctrl_out_ep_comp_desc = {
	.bLength            =	sizeof(qdss_ctrl_out_ep_comp_desc),
	.bDescriptorType    =	USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst          =	0,
	.bmAttributes       =	0,
	.wBytesPerInterval  =	0,
};

/* Full speed support */
static struct usb_endpoint_descriptor qdss_fs_data_desc = {
	.bLength            =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    =	USB_DT_ENDPOINT,
	.bEndpointAddress   =	USB_DIR_IN,
	.bmAttributes       =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize     =	cpu_to_le16(64),
};

static struct usb_endpoint_descriptor qdss_fs_ctrl_in_desc  = {
	.bLength            =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    =	USB_DT_ENDPOINT,
	.bEndpointAddress   =	USB_DIR_IN,
	.bmAttributes       =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize     =	cpu_to_le16(64),
};

static struct usb_endpoint_descriptor qdss_fs_ctrl_out_desc = {
	.bLength            =	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    =	USB_DT_ENDPOINT,
	.bEndpointAddress   =	USB_DIR_OUT,
	.bmAttributes       =	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize     =	cpu_to_le16(64),
};

static struct usb_descriptor_header *qdss_fs_desc[] = {
	(struct usb_descriptor_header *) &qdss_data_intf_desc,
	(struct usb_descriptor_header *) &qdss_fs_data_desc,
	(struct usb_descriptor_header *) &qdss_ctrl_intf_desc,
	(struct usb_descriptor_header *) &qdss_fs_ctrl_in_desc,
	(struct usb_descriptor_header *) &qdss_fs_ctrl_out_desc,
	NULL,
};

static struct usb_descriptor_header *qdss_hs_desc[] = {
	(struct usb_descriptor_header *) &qdss_data_intf_desc,
	(struct usb_descriptor_header *) &qdss_hs_data_desc,
	(struct usb_descriptor_header *) &qdss_ctrl_intf_desc,
	(struct usb_descriptor_header *) &qdss_hs_ctrl_in_desc,
	(struct usb_descriptor_header *) &qdss_hs_ctrl_out_desc,
	NULL,
};

static struct usb_descriptor_header *qdss_ss_desc[] = {
	(struct usb_descriptor_header *) &qdss_data_intf_desc,
	(struct usb_descriptor_header *) &qdss_ss_data_desc,
	(struct usb_descriptor_header *) &qdss_data_ep_comp_desc,
	(struct usb_descriptor_header *) &qdss_ctrl_intf_desc,
	(struct usb_descriptor_header *) &qdss_ss_ctrl_in_desc,
	(struct usb_descriptor_header *) &qdss_ctrl_in_ep_comp_desc,
	(struct usb_descriptor_header *) &qdss_ss_ctrl_out_desc,
	(struct usb_descriptor_header *) &qdss_ctrl_out_ep_comp_desc,
	NULL,
};

static struct usb_descriptor_header *qdss_fs_data_only_desc[] = {
	(struct usb_descriptor_header *) &qdss_data_intf_desc,
	(struct usb_descriptor_header *) &qdss_fs_data_desc,
	NULL,
};

static struct usb_descriptor_header *qdss_hs_data_only_desc[] = {
	(struct usb_descriptor_header *) &qdss_data_intf_desc,
	(struct usb_descriptor_header *) &qdss_hs_data_desc,
	NULL,
};

static struct usb_descriptor_header *qdss_ss_data_only_desc[] = {
	(struct usb_descriptor_header *) &qdss_data_intf_desc,
	(struct usb_descriptor_header *) &qdss_ss_data_desc,
	(struct usb_descriptor_header *) &qdss_data_ep_comp_desc,
	NULL,
};

/* string descriptors: */
#define QDSS_DATA_IDX	0
#define QDSS_CTRL_IDX	1

static struct usb_string qdss_string_defs[] = {
	[QDSS_DATA_IDX].s = "QDSS DATA",
	[QDSS_CTRL_IDX].s = "QDSS CTRL",
	{}, /* end of list */
};

static struct usb_gadget_strings qdss_string_table = {
	.language =		0x0409,
	.strings =		qdss_string_defs,
};

static struct usb_gadget_strings *qdss_strings[] = {
	&qdss_string_table,
	NULL,
};

static void qdss_disable(struct usb_function *f);

static inline struct f_qdss *func_to_qdss(struct usb_function *f)
{
	return container_of(f, struct f_qdss, port.function);
}

static
struct usb_qdss_opts *to_fi_usb_qdss_opts(struct usb_function_instance *fi)
{
	return container_of(fi, struct usb_qdss_opts, func_inst);
}

static inline bool qdss_uses_sw_path(struct f_qdss *qdss)
{
	return (!strcmp(qdss->ch.name, USB_QDSS_CH_MDM) ||
		!strcmp(qdss->ch.name, USB_QDSS_CH_SW));
}

/*----------------------------------------------------------------------*/

static void qdss_write_complete(struct usb_ep *ep,
	struct usb_request *req)
{
	struct f_qdss *qdss = ep->driver_data;
	struct qdss_req *qreq = req->context;
	struct qdss_request *d_req = qreq->qdss_req;
	struct usb_ep *in;
	enum qdss_state state;
	unsigned long flags;

	in = qdss->port.data;
	state = USB_QDSS_DATA_WRITE_DONE;

	qdss_log("channel:%s ep:%s req:%pK req->status:%d req->length:%d\n",
		qdss->ch.name, ep->name, req, req->status, req->length);
	spin_lock_irqsave(&qdss->lock, flags);
	list_move_tail(&qreq->list, &qdss->data_write_pool);

	/*
	 * When channel is closed, we move all queued requests to
	 * dequeued_data_pool list and wait for it to be drained.
	 * Signal the completion here if the channel is closed
	 * and both queued & dequeued lists are empty.
	 */
	if (!qdss->opened && list_empty(&qdss->dequeued_data_pool) &&
			list_empty(&qdss->queued_data_pool))
		complete(&qdss->dequeue_done);

	if (req->length != 0) {
		d_req->actual = req->actual;
		d_req->status = req->status;
	}
	spin_unlock_irqrestore(&qdss->lock, flags);

	if (qdss->ch.notify)
		qdss->ch.notify(qdss->ch.priv, state, d_req, NULL);
}

static void qdss_free_reqs(struct f_qdss *qdss)
{
	struct list_head *act, *tmp;
	struct qdss_req *qreq;
	int data_write_req = 0;
	unsigned long flags;

	lockdep_assert_held(&qdss->mutex);

	spin_lock_irqsave(&qdss->lock, flags);

	list_for_each_safe(act, tmp, &qdss->data_write_pool) {
		qreq = list_entry(act, struct qdss_req, list);
		list_del(&qreq->list);
		usb_ep_free_request(qdss->port.data, qreq->usb_req);
		kfree(qreq);
		data_write_req++;
	}

	qdss_log("channel:%s data_write_req:%d freed\n", qdss->ch.name,
							data_write_req);
	spin_unlock_irqrestore(&qdss->lock, flags);
}

void usb_qdss_free_req(struct usb_qdss_ch *ch)
{
	struct f_qdss *qdss = container_of(ch, struct f_qdss, ch);

	if (!ch) {
		pr_err("%s: ch is NULL\n", __func__);
		return;
	}

	mutex_lock(&qdss->mutex);
	if (!qdss->opened)
		pr_err("%s: channel %s closed\n", __func__, ch->name);
	else
		qdss_free_reqs(qdss);
	mutex_unlock(&qdss->mutex);
}
EXPORT_SYMBOL(usb_qdss_free_req);

int usb_qdss_alloc_req(struct usb_qdss_ch *ch, int no_write_buf)
{
	struct f_qdss *qdss = container_of(ch, struct f_qdss, ch);
	struct usb_request *req;
	struct usb_ep *in;
	struct list_head *list_pool;
	int i;
	struct qdss_req *qreq;
	unsigned long flags;

	if (!ch) {
		pr_err("%s: ch is NULL\n", __func__);
		return -EINVAL;
	}

	qdss_log("channel:%s num_write_buf:%d\n", ch->name, no_write_buf);

	if (!qdss) {
		pr_err("%s: %s closed\n", __func__, ch->name);
		return -ENODEV;
	}

	mutex_lock(&qdss->mutex);

	in = qdss->port.data;
	list_pool = &qdss->data_write_pool;

	for (i = 0; i < no_write_buf; i++) {
		qreq = kzalloc(sizeof(struct qdss_req), GFP_KERNEL);
		if (!qreq)
			goto fail;

		req = usb_ep_alloc_request(in, GFP_ATOMIC);
		if (!req) {
			pr_err("%s: ctrl_in allocation err\n", __func__);
			kfree(qreq);
			goto fail;
		}
		spin_lock_irqsave(&qdss->lock, flags);
		qreq->usb_req = req;
		req->context = qreq;
		req->complete = qdss_write_complete;
		list_add_tail(&qreq->list, list_pool);
		spin_unlock_irqrestore(&qdss->lock, flags);
	}

	mutex_unlock(&qdss->mutex);
	return 0;

fail:
	qdss_free_reqs(qdss);
	mutex_unlock(&qdss->mutex);
	return -ENOMEM;
}
EXPORT_SYMBOL(usb_qdss_alloc_req);

static void clear_eps(struct usb_function *f)
{
	struct f_qdss *qdss = func_to_qdss(f);

	qdss_log("channel:%s\n", qdss->ch.name);

	if (qdss->port.ctrl_in)
		qdss->port.ctrl_in->driver_data = NULL;
	if (qdss->port.ctrl_out)
		qdss->port.ctrl_out->driver_data = NULL;
	if (qdss->port.data) {
		if (!qdss_uses_sw_path(qdss)) {
			msm_ep_clear_ops(qdss->port.data);
			msm_ep_set_mode(qdss->port.data, USB_EP_NONE);
		}
		qdss->port.data->driver_data = NULL;
	}
}

static void clear_desc(struct usb_gadget *gadget, struct usb_function *f)
{
	struct f_qdss *qdss = func_to_qdss(f);

	qdss_log("channel:%s\n", qdss->ch.name);

	usb_free_all_descriptors(f);
}

static int qdss_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_gadget *gadget = c->cdev->gadget;
	struct f_qdss *qdss = func_to_qdss(f);
	struct usb_ep *ep;
	int iface, id, ret;

	qdss_log("channel:%s\n", qdss->ch.name);

	/* Allocate data I/F */
	iface = usb_interface_id(c, f);
	if (iface < 0) {
		pr_err("interface allocation error\n");
		return iface;
	}
	qdss_data_intf_desc.bInterfaceNumber = iface;
	qdss->data_iface_id = iface;

	if (!qdss_string_defs[QDSS_DATA_IDX].id) {
		id = usb_string_id(c->cdev);
		if (id < 0)
			return id;
		qdss_string_defs[QDSS_DATA_IDX].id = id;
		qdss_data_intf_desc.iInterface = id;
	}

	if (qdss->debug_inface_enabled) {
		/* Allocate ctrl I/F */
		iface = usb_interface_id(c, f);
		if (iface < 0) {
			pr_err("interface allocation error\n");
			return iface;
		}
		qdss_ctrl_intf_desc.bInterfaceNumber = iface;
		qdss->ctrl_iface_id = iface;

		if (!qdss_string_defs[QDSS_CTRL_IDX].id) {
			id = usb_string_id(c->cdev);
			if (id < 0)
				return id;
			qdss_string_defs[QDSS_CTRL_IDX].id = id;
			qdss_ctrl_intf_desc.iInterface = id;
		}
	}

	/* for non-accelerated path keep tx fifo size 1k */
	if (qdss_uses_sw_path(qdss))
		qdss_data_ep_comp_desc.bMaxBurst = 0;

	ep = usb_ep_autoconfig(gadget, &qdss_fs_data_desc);
	if (!ep) {
		pr_err("%s: ep_autoconfig error\n", __func__);
		goto clear_ep;
	}
	qdss->port.data = ep;
	ep->driver_data = qdss;

	if (!qdss_uses_sw_path(qdss)) {
		ret = msm_ep_set_mode(qdss->port.data, qdss->ch.ch_type);
		if (ret < 0)
			goto clear_ep;

		msm_ep_update_ops(qdss->port.data);
	}

	if (qdss->debug_inface_enabled) {
		ep = usb_ep_autoconfig(gadget, &qdss_fs_ctrl_in_desc);
		if (!ep) {
			pr_err("%s: ep_autoconfig error\n", __func__);
			goto clear_ep;
		}

		qdss->port.ctrl_in = ep;
		ep->driver_data = qdss;

		ep = usb_ep_autoconfig(gadget, &qdss_fs_ctrl_out_desc);
		if (!ep) {
			pr_err("%s: ep_autoconfig error\n", __func__);
			goto clear_ep;
		}
		qdss->port.ctrl_out = ep;
		ep->driver_data = qdss;
	}

	if (!qdss_uses_sw_path(qdss)) {
		ret = alloc_hw_req(qdss->port.data);
		if (ret) {
			pr_err("%s: alloc_sps_req error (%d)\n",
							__func__, ret);
			goto clear_ep;
		}
	}

	/* update hs/ss descriptors */
	qdss_hs_data_desc.bEndpointAddress =
		qdss_ss_data_desc.bEndpointAddress =
			qdss_fs_data_desc.bEndpointAddress;
	if (qdss->debug_inface_enabled) {
		qdss_hs_ctrl_in_desc.bEndpointAddress =
			qdss_ss_ctrl_in_desc.bEndpointAddress =
				qdss_fs_ctrl_in_desc.bEndpointAddress;
		qdss_hs_ctrl_out_desc.bEndpointAddress =
			qdss_ss_ctrl_out_desc.bEndpointAddress =
				qdss_fs_ctrl_out_desc.bEndpointAddress;
	}

	if (qdss->debug_inface_enabled)
		ret = usb_assign_descriptors(f, qdss_fs_desc, qdss_hs_desc,
				qdss_ss_desc, qdss_ss_desc);
	else
		ret = usb_assign_descriptors(f, qdss_fs_data_only_desc,
				qdss_hs_data_only_desc, qdss_ss_data_only_desc,
				qdss_ss_data_only_desc);

	if (ret)
		goto fail;

	return 0;

fail:
	/* check if usb_request allocated */
	if (qdss->endless_req) {
		usb_ep_free_request(qdss->port.data,
				qdss->endless_req);
		qdss->endless_req = NULL;
	}

clear_ep:
	clear_eps(f);

	return -EOPNOTSUPP;
}


static void qdss_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_qdss  *qdss = func_to_qdss(f);
	struct usb_gadget *gadget = c->cdev->gadget;

	qdss_log("channel:%s\n", qdss->ch.name);

	qdss_disable(f);
	flush_workqueue(qdss->wq);

	if (qdss->endless_req) {
		usb_ep_free_request(qdss->port.data,
				qdss->endless_req);
		qdss->endless_req = NULL;
	}

	/* Reset string ids */
	qdss_string_defs[QDSS_DATA_IDX].id = 0;
	qdss_string_defs[QDSS_CTRL_IDX].id = 0;

	clear_eps(f);
	clear_desc(gadget, f);
}

static void qdss_eps_disable(struct usb_function *f)
{
	struct f_qdss  *qdss = func_to_qdss(f);

	qdss_log("channel:%s\n", qdss->ch.name);

	if (qdss->ctrl_in_enabled) {
		usb_ep_disable(qdss->port.ctrl_in);
		qdss->ctrl_in_enabled = 0;
	}

	if (qdss->ctrl_out_enabled) {
		usb_ep_disable(qdss->port.ctrl_out);
		qdss->ctrl_out_enabled = 0;
	}

	if (qdss->data_enabled) {
		usb_ep_disable(qdss->port.data);
		qdss->data_enabled = 0;
	}
}

static void usb_qdss_disconnect_work(struct work_struct *work)
{
	struct f_qdss *qdss;
	int status;

	qdss = container_of(work, struct f_qdss, disconnect_w);
	qdss_log("channel:%s\n", qdss->ch.name);

	/* Notify qdss to cancel all active transfers */
	if (qdss->ch.notify)
		qdss->ch.notify(qdss->ch.priv,
			USB_QDSS_DISCONNECT,
			NULL,
			NULL);

	mutex_lock(&qdss->mutex);

	/* Uninitialized init data i.e. ep specific operation */
	if (qdss->opened && !qdss_uses_sw_path(qdss)) {
		status = set_qdss_data_connection(qdss, 0);
		if (status)
			pr_err("qdss_disconnect error\n");
	}

	/*
	 * Decrement usage count which was incremented
	 * before calling connect work
	 */
	usb_gadget_autopm_put_async(qdss->gadget);

	mutex_unlock(&qdss->mutex);
}

static void qdss_disable(struct usb_function *f)
{
	struct f_qdss	*qdss = func_to_qdss(f);
	unsigned long flags;

	qdss_log("channel:%s\n", qdss->ch.name);
	spin_lock_irqsave(&qdss->lock, flags);
	if (!qdss->usb_connected) {
		spin_unlock_irqrestore(&qdss->lock, flags);
		return;
	}

	qdss->usb_connected = 0;
	spin_unlock_irqrestore(&qdss->lock, flags);
	/*cancell all active xfers*/
	qdss_eps_disable(f);
	queue_work(qdss->wq, &qdss->disconnect_w);
}

static void usb_qdss_connect_work(struct work_struct *work)
{
	struct f_qdss *qdss;
	int status;
	struct usb_request *req = NULL;
	unsigned long flags;

	qdss = container_of(work, struct f_qdss, connect_w);

	/* If cable is already removed, discard connect_work */
	if (qdss->usb_connected == 0) {
		cancel_work_sync(&qdss->disconnect_w);
		usb_gadget_autopm_put_async(qdss->gadget);
		return;
	}

	mutex_lock(&qdss->mutex);

	qdss_log("channel:%s opened:%d\n", qdss->ch.name, qdss->opened);
	if (!qdss->opened)
		goto unlock_out;

	if (qdss_uses_sw_path(qdss))
		goto notify;

	status = set_qdss_data_connection(qdss, 1);
	if (status) {
		pr_err("set_qdss_data_connection error(%d)\n", status);
		goto unlock_out;
	}

	spin_lock_irqsave(&qdss->lock, flags);
	req = qdss->endless_req;
	spin_unlock_irqrestore(&qdss->lock, flags);
	if (!req)
		goto unlock_out;

	status = usb_ep_queue(qdss->port.data, req, GFP_ATOMIC);
	if (status) {
		pr_err("%s: usb_ep_queue error (%d)\n", __func__, status);
		goto unlock_out;
	}

notify:
	mutex_unlock(&qdss->mutex);
	if (qdss->ch.notify)
		qdss->ch.notify(qdss->ch.priv, USB_QDSS_CONNECT,
						NULL, &qdss->ch);
	return;

unlock_out:
	mutex_unlock(&qdss->mutex);
}

static int qdss_set_alt(struct usb_function *f, unsigned int intf,
				unsigned int alt)
{
	struct f_qdss  *qdss = func_to_qdss(f);
	struct usb_gadget *gadget = f->config->cdev->gadget;
	int ret = 0;

	qdss_log("qdss pointer = %pK\n", qdss);
	qdss->gadget = gadget;

	if (alt != 0)
		goto fail1;

	if (gadget->speed < USB_SPEED_HIGH) {
		pr_err("%s: qdss doesn't support USB full or low speed\n",
								__func__);
		ret = -EINVAL;
		goto fail1;
	}

	if (intf == qdss->data_iface_id && !qdss->data_enabled) {
		/* Increment usage count on connect */
		usb_gadget_autopm_get_async(qdss->gadget);

		ret = config_ep_by_speed(gadget, f, qdss->port.data);
		if (ret) {
			pr_err("%s: failed config_ep_by_speed ret:%d\n",
							__func__, ret);
			goto fail;
		}

		ret = usb_ep_enable(qdss->port.data);
		if (ret) {
			pr_err("%s: failed to enable ep ret:%d\n",
							__func__, ret);
			goto fail;
		}

		qdss->port.data->driver_data = qdss;
		qdss->data_enabled = 1;


	} else if ((intf == qdss->ctrl_iface_id) &&
	(qdss->debug_inface_enabled)) {

		if (config_ep_by_speed(gadget, f, qdss->port.ctrl_in)) {
			ret = -EINVAL;
			goto fail1;
		}

		ret = usb_ep_enable(qdss->port.ctrl_in);
		if (ret)
			goto fail1;

		qdss->port.ctrl_in->driver_data = qdss;
		qdss->ctrl_in_enabled = 1;

		if (config_ep_by_speed(gadget, f, qdss->port.ctrl_out)) {
			ret = -EINVAL;
			goto fail1;
		}


		ret = usb_ep_enable(qdss->port.ctrl_out);
		if (ret)
			goto fail1;

		qdss->port.ctrl_out->driver_data = qdss;
		qdss->ctrl_out_enabled = 1;
	}

	if (qdss->debug_inface_enabled) {
		if (qdss->ctrl_out_enabled && qdss->ctrl_in_enabled &&
			qdss->data_enabled) {
			qdss->usb_connected = 1;
			qdss_log("usb_connected INTF enabled\n");
		}
	} else {
		if (qdss->data_enabled) {
			qdss->usb_connected = 1;
			qdss_log("usb_connected INTF disabled\n");
		}
	}

	if (qdss->usb_connected)
		queue_work(qdss->wq, &qdss->connect_w);

	return 0;
fail:
	/* Decrement usage count in case of failure */
	usb_gadget_autopm_put_async(qdss->gadget);
fail1:
	pr_err("%s failed ret:%d\n", __func__, ret);
	qdss_eps_disable(f);
	return ret;
}

static struct f_qdss *alloc_usb_qdss(char *channel_name)
{
	struct f_qdss *qdss;
	int found = 0;
	struct usb_qdss_ch *ch;
	unsigned long flags;

	spin_lock_irqsave(&channel_lock, flags);
	list_for_each_entry(ch, &usb_qdss_ch_list, list) {
		if (!strcmp(channel_name, ch->name)) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&channel_lock, flags);

	if (found) {
		pr_err("%s: (%s) is already available.\n",
				__func__, channel_name);
		return ERR_PTR(-EEXIST);
	}

	qdss = kzalloc(sizeof(struct f_qdss), GFP_KERNEL);
	if (!qdss)
		return ERR_PTR(-ENOMEM);

	qdss->wq = create_singlethread_workqueue(channel_name);
	if (!qdss->wq) {
		kfree(qdss);
		return ERR_PTR(-ENOMEM);
	}

	spin_lock_irqsave(&channel_lock, flags);
	ch = &qdss->ch;
	ch->name = channel_name;

	if (!strcmp(ch->name, USB_QDSS_CH_EBC))
		ch->ch_type = USB_EP_EBC;
	else
		ch->ch_type = USB_EP_NONE;

	list_add_tail(&ch->list, &usb_qdss_ch_list);
	spin_unlock_irqrestore(&channel_lock, flags);

	spin_lock_init(&qdss->lock);
	INIT_LIST_HEAD(&qdss->data_write_pool);
	INIT_LIST_HEAD(&qdss->queued_data_pool);
	INIT_LIST_HEAD(&qdss->dequeued_data_pool);
	INIT_WORK(&qdss->connect_w, usb_qdss_connect_work);
	INIT_WORK(&qdss->disconnect_w, usb_qdss_disconnect_work);
	mutex_init(&qdss->mutex);
	init_completion(&qdss->dequeue_done);

	return qdss;
}

int usb_qdss_write(struct usb_qdss_ch *ch, struct qdss_request *d_req)
{
	struct f_qdss *qdss = container_of(ch, struct f_qdss, ch);
	unsigned long flags;
	struct usb_request *req = NULL;
	struct qdss_req *qreq;

	if (!ch) {
		pr_err("%s: ch is NULL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&qdss->mutex);

	qdss_log("channel:%s d_req:%pK\n", ch->name, d_req);
	spin_lock_irqsave(&qdss->lock, flags);

	if (!qdss->opened || !qdss->usb_connected) {
		spin_unlock_irqrestore(&qdss->lock, flags);
		qdss_log("return -EIO\n");
		mutex_unlock(&qdss->mutex);
		return -EIO;
	}

	if (list_empty(&qdss->data_write_pool)) {
		pr_err("error: usb_qdss_data_write list is empty\n");
		spin_unlock_irqrestore(&qdss->lock, flags);
		mutex_unlock(&qdss->mutex);
		return -EAGAIN;
	}

	qreq = list_first_entry(&qdss->data_write_pool, struct qdss_req,
		list);
	list_move_tail(&qreq->list, &qdss->queued_data_pool);
	spin_unlock_irqrestore(&qdss->lock, flags);

	qreq->qdss_req = d_req;
	req = qreq->usb_req;
	req->buf = d_req->buf;
	req->length = d_req->length;
	req->sg = d_req->sg;
	req->num_sgs = d_req->num_sgs;
	if (req->sg)
		qdss_log("%s: req:%pK req->num_sgs:0x%x\n",
			ch->name, req, req->num_sgs);
	else
		qdss_log("%s: req:%pK rq->length:0x%x\n",
			ch->name, req, req->length);
	if (usb_ep_queue(qdss->port.data, req, GFP_ATOMIC)) {
		spin_lock_irqsave(&qdss->lock, flags);
		/* Remove from queued pool and add back to data pool */
		list_move_tail(&qreq->list, &qdss->data_write_pool);
		spin_unlock_irqrestore(&qdss->lock, flags);
		pr_err("qdss usb_ep_queue failed\n");
		mutex_unlock(&qdss->mutex);
		return -EIO;
	}

	mutex_unlock(&qdss->mutex);
	return 0;
}
EXPORT_SYMBOL(usb_qdss_write);

struct usb_qdss_ch *usb_qdss_open(const char *name, void *priv,
	void (*notify)(void *priv, unsigned int event,
		struct qdss_request *d_req, struct usb_qdss_ch *))
{
	struct usb_qdss_ch *ch;
	struct f_qdss *qdss = NULL;
	unsigned long flags;

	qdss_log("called for channel:%s\n", name);
	if (!notify) {
		pr_err("%s: notification func is missing\n", __func__);
		return NULL;
	}

	spin_lock_irqsave(&channel_lock, flags);
retry:
	/* Check if we already have a channel with this name */
	list_for_each_entry(ch, &usb_qdss_ch_list, list) {
		if (!strcmp(name, ch->name)) {
			qdss = container_of(ch, struct f_qdss, ch);
			break;
		}
	}

	if (!strcmp(name, USB_QDSS_CH_SW) &&
			(!qdss || !qdss->port.function.name)) {
		qdss_log("qdss_sw not added to config, fall back to qdss_mdm\n");
		name = USB_QDSS_CH_MDM;
		qdss = NULL;
		goto retry;
	}

	spin_unlock_irqrestore(&channel_lock, flags);
	if (!qdss) {
		qdss_log("failed to find channel:%s\n", name);
		return NULL;
	}

	mutex_lock(&qdss->mutex);
	qdss_log("qdss ctx found for channel:%s\n", name);
	ch->priv = priv;
	ch->notify = notify;
	qdss->opened = true;
	reinit_completion(&qdss->dequeue_done);

	/* the case USB cabel was connected before qdss called qdss_open */
	if (qdss->usb_connected)
		queue_work(qdss->wq, &qdss->connect_w);

	mutex_unlock(&qdss->mutex);
	return ch;
}
EXPORT_SYMBOL(usb_qdss_open);

void usb_qdss_close(struct usb_qdss_ch *ch)
{
	struct f_qdss *qdss = container_of(ch, struct f_qdss, ch);
	struct usb_gadget *gadget;
	unsigned long flags;
	int status;
	struct qdss_req *qreq;
	bool do_wait;

	if (!ch) {
		pr_err("%s: ch is NULL\n", __func__);
		return;
	}

	qdss_log("channel:%s\n", ch->name);

	mutex_lock(&qdss->mutex);
	if (!qdss->opened) {
		pr_err("%s: channel %s closed\n", __func__, ch->name);
		goto unlock_out;
	}

	spin_lock_irqsave(&qdss->lock, flags);
	qdss->opened = false;
	/*
	 * Some UDCs like DWC3 stop the endpoint transfer upon dequeue
	 * of a request and retire all the previously *started* requests.
	 * This introduces a race between the below dequeue loop and
	 * retiring of all started requests. As soon as we drop the lock
	 * here before dequeue, the request gets retired and UDC thinks
	 * we are dequeuing a request that was not queued before. To
	 * avoid this problem, lets dequeue the requests in the reverse
	 * order.
	 */
	while (!list_empty(&qdss->queued_data_pool)) {
		qreq = list_last_entry(&qdss->queued_data_pool,
				struct qdss_req, list);
		list_move_tail(&qreq->list, &qdss->dequeued_data_pool);
		spin_unlock_irqrestore(&qdss->lock, flags);
		status = usb_ep_dequeue(qdss->port.data, qreq->usb_req);
		qdss_log("dequeue req:%pK status=%d\n", qreq->usb_req, status);
		spin_lock_irqsave(&qdss->lock, flags);
	}

	/*
	 * It's possible that requests may be completed synchronously during
	 * usb_ep_dequeue() and would have already been moved back to
	 * data_write_pool.  So make sure to check that our dequeued_data_pool
	 * is empty. If not, wait for it to happen. The request completion
	 * handler would signal us when this list is empty and channel close
	 * is in progress.
	 */
	do_wait = !list_empty(&qdss->dequeued_data_pool);
	spin_unlock_irqrestore(&qdss->lock, flags);

	if (do_wait) {
		qdss_log("waiting for completion on dequeued requests\n");
		wait_for_completion(&qdss->dequeue_done);
	}

	WARN_ON(!list_empty(&qdss->dequeued_data_pool));

	qdss_free_reqs(qdss);
	ch->notify = NULL;
	if (!qdss->usb_connected || qdss_uses_sw_path(qdss))
		goto unlock_out;

	if (qdss->endless_req)
		usb_ep_dequeue(qdss->port.data, qdss->endless_req);

	gadget = qdss->gadget;

	status = set_qdss_data_connection(qdss, 0);
	if (status)
		pr_err("%s:qdss_disconnect error\n", __func__);

unlock_out:
	mutex_unlock(&qdss->mutex);
}
EXPORT_SYMBOL(usb_qdss_close);

static void qdss_cleanup(void)
{
	struct f_qdss *qdss;
	struct list_head *act, *tmp;
	struct usb_qdss_ch *_ch;
	unsigned long flags;

	qdss_log("cleaning up channel resources.\n");

	list_for_each_safe(act, tmp, &usb_qdss_ch_list) {
		_ch = list_entry(act, struct usb_qdss_ch, list);
		qdss = container_of(_ch, struct f_qdss, ch);
		destroy_workqueue(qdss->wq);
		spin_lock_irqsave(&channel_lock, flags);
		if (!_ch->priv) {
			list_del(&_ch->list);
			kfree(qdss);
		}
		spin_unlock_irqrestore(&channel_lock, flags);
	}
}

static void qdss_free_func(struct usb_function *f)
{
	struct f_qdss *qdss = func_to_qdss(f);

	qdss->debug_inface_enabled = false;
}

static inline struct usb_qdss_opts *to_f_qdss_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct usb_qdss_opts,
			func_inst.group);
}

static void qdss_attr_release(struct config_item *item)
{
	struct usb_qdss_opts *opts = to_f_qdss_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations qdss_item_ops = {
	.release	= qdss_attr_release,
};

static ssize_t qdss_enable_debug_inface_show(struct config_item *item,
			char *page)
{
	return scnprintf(page, PAGE_SIZE, "%s\n",
		(to_f_qdss_opts(item)->usb_qdss->debug_inface_enabled) ?
		"Enabled" : "Disabled");
}

static ssize_t qdss_enable_debug_inface_store(struct config_item *item,
			const char *page, size_t len)
{
	struct f_qdss *qdss = to_f_qdss_opts(item)->usb_qdss;
	unsigned long flags;
	u8 stats;

	if (page == NULL) {
		pr_err("Invalid buffer\n");
		return len;
	}

	if (kstrtou8(page, 0, &stats) != 0 && !(stats == 0 || stats == 1)) {
		pr_err("(%u)Wrong value. enter 0 to disable or 1 to enable.\n",
			stats);
		return len;
	}

	spin_lock_irqsave(&qdss->lock, flags);
	qdss->debug_inface_enabled = stats;
	spin_unlock_irqrestore(&qdss->lock, flags);
	return len;
}

CONFIGFS_ATTR(qdss_, enable_debug_inface);
static struct configfs_attribute *qdss_attrs[] = {
	&qdss_attr_enable_debug_inface,
	NULL,
};

static struct config_item_type qdss_func_type = {
	.ct_item_ops	= &qdss_item_ops,
	.ct_attrs	= qdss_attrs,
	.ct_owner	= THIS_MODULE,
};

static void usb_qdss_free_inst(struct usb_function_instance *fi)
{
	struct usb_qdss_opts *opts;

	opts = container_of(fi, struct usb_qdss_opts, func_inst);
	kfree(opts->usb_qdss);
	kfree(opts);
}

static int usb_qdss_set_inst_name(struct usb_function_instance *f,
				const char *name)
{
	struct usb_qdss_opts *opts =
		container_of(f, struct usb_qdss_opts, func_inst);
	char *ptr;
	size_t name_len;
	struct f_qdss *usb_qdss;

	/* get channel_name as expected input qdss.<channel_name> */
	name_len = strlen(name) + 1;
	if (name_len > 15)
		return -ENAMETOOLONG;

	/* get channel name */
	ptr = kstrndup(name, name_len, GFP_KERNEL);
	if (!ptr) {
		pr_err("error:%ld\n", PTR_ERR(ptr));
		return -ENOMEM;
	}

	opts->channel_name = ptr;
	qdss_log("qdss: channel_name:%s\n", opts->channel_name);

	usb_qdss = alloc_usb_qdss(opts->channel_name);
	if (IS_ERR(usb_qdss)) {
		pr_err("Failed to create usb_qdss port(%s)\n",
				opts->channel_name);
		return -ENOMEM;
	}

	opts->usb_qdss = usb_qdss;
	return 0;
}

static struct usb_function_instance *qdss_alloc_inst(void)
{
	struct usb_qdss_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	opts->func_inst.free_func_inst = usb_qdss_free_inst;
	opts->func_inst.set_inst_name = usb_qdss_set_inst_name;

	config_group_init_type_name(&opts->func_inst.group, "",
				    &qdss_func_type);
	return &opts->func_inst;
}

static struct usb_function *qdss_alloc(struct usb_function_instance *fi)
{
	struct usb_qdss_opts *opts = to_fi_usb_qdss_opts(fi);
	struct f_qdss *usb_qdss = opts->usb_qdss;

	usb_qdss->port.function.name = "usb_qdss";
	usb_qdss->port.function.strings = qdss_strings;
	usb_qdss->port.function.bind = qdss_bind;
	usb_qdss->port.function.unbind = qdss_unbind;
	usb_qdss->port.function.set_alt = qdss_set_alt;
	usb_qdss->port.function.disable = qdss_disable;
	usb_qdss->port.function.setup = NULL;
	usb_qdss->port.function.free_func = qdss_free_func;

	return &usb_qdss->port.function;
}

DECLARE_USB_FUNCTION(qdss, qdss_alloc_inst, qdss_alloc);
static int __init usb_qdss_init(void)
{
	int ret;

	_qdss_ipc_log = ipc_log_context_create(NUM_PAGES, "usb_qdss", 0);
	if (IS_ERR_OR_NULL(_qdss_ipc_log))
		_qdss_ipc_log =  NULL;

	INIT_LIST_HEAD(&usb_qdss_ch_list);
	ret = usb_function_register(&qdssusb_func);
	if (ret) {
		pr_err("%s: failed to register diag %d\n", __func__, ret);
		return ret;
	}
	return ret;
}

static void __exit usb_qdss_exit(void)
{
	ipc_log_context_destroy(_qdss_ipc_log);
	usb_function_unregister(&qdssusb_func);
	qdss_cleanup();
}

module_init(usb_qdss_init);
module_exit(usb_qdss_exit);
MODULE_DESCRIPTION("USB QDSS Function Driver");
MODULE_LICENSE("GPL");
