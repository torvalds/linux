// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus "AP" USB driver for "ES2" controller chips
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 */
#include <linux/kthread.h>
#include <linux/sizes.h>
#include <linux/usb.h>
#include <linux/kfifo.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/greybus.h>
#include <asm/unaligned.h>

#include "arpc.h"
#include "greybus_trace.h"


/* Default timeout for USB vendor requests. */
#define ES2_USB_CTRL_TIMEOUT	500

/* Default timeout for ARPC CPort requests */
#define ES2_ARPC_CPORT_TIMEOUT	500

/* Fixed CPort numbers */
#define ES2_CPORT_CDSI0		16
#define ES2_CPORT_CDSI1		17

/* Memory sizes for the buffers sent to/from the ES2 controller */
#define ES2_GBUF_MSG_SIZE_MAX	2048

/* Memory sizes for the ARPC buffers */
#define ARPC_OUT_SIZE_MAX	U16_MAX
#define ARPC_IN_SIZE_MAX	128

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x18d1, 0x1eaf) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

#define APB1_LOG_SIZE		SZ_16K

/*
 * Number of CPort IN urbs in flight at any point in time.
 * Adjust if we are having stalls in the USB buffer due to not enough urbs in
 * flight.
 */
#define NUM_CPORT_IN_URB	4

/* Number of CPort OUT urbs in flight at any point in time.
 * Adjust if we get messages saying we are out of urbs in the system log.
 */
#define NUM_CPORT_OUT_URB	8

/*
 * Number of ARPC in urbs in flight at any point in time.
 */
#define NUM_ARPC_IN_URB		2

/*
 * @endpoint: bulk in endpoint for CPort data
 * @urb: array of urbs for the CPort in messages
 * @buffer: array of buffers for the @cport_in_urb urbs
 */
struct es2_cport_in {
	__u8 endpoint;
	struct urb *urb[NUM_CPORT_IN_URB];
	u8 *buffer[NUM_CPORT_IN_URB];
};

/**
 * struct es2_ap_dev - ES2 USB Bridge to AP structure
 * @usb_dev: pointer to the USB device we are.
 * @usb_intf: pointer to the USB interface we are bound to.
 * @hd: pointer to our gb_host_device structure
 *
 * @cport_in: endpoint, urbs and buffer for cport in messages
 * @cport_out_endpoint: endpoint for cport out messages
 * @cport_out_urb: array of urbs for the CPort out messages
 * @cport_out_urb_busy: array of flags to see if the @cport_out_urb is busy or
 *			not.
 * @cport_out_urb_cancelled: array of flags indicating whether the
 *			corresponding @cport_out_urb is being cancelled
 * @cport_out_urb_lock: locks the @cport_out_urb_busy "list"
 * @cdsi1_in_use: true if cport CDSI1 is in use
 * @apb_log_task: task pointer for logging thread
 * @apb_log_dentry: file system entry for the log file interface
 * @apb_log_enable_dentry: file system entry for enabling logging
 * @apb_log_fifo: kernel FIFO to carry logged data
 * @arpc_urb: array of urbs for the ARPC in messages
 * @arpc_buffer: array of buffers for the @arpc_urb urbs
 * @arpc_endpoint_in: bulk in endpoint for APBridgeA RPC
 * @arpc_id_cycle: gives an unique id to ARPC
 * @arpc_lock: locks ARPC list
 * @arpcs: list of in progress ARPCs
 */
struct es2_ap_dev {
	struct usb_device *usb_dev;
	struct usb_interface *usb_intf;
	struct gb_host_device *hd;

	struct es2_cport_in cport_in;
	__u8 cport_out_endpoint;
	struct urb *cport_out_urb[NUM_CPORT_OUT_URB];
	bool cport_out_urb_busy[NUM_CPORT_OUT_URB];
	bool cport_out_urb_cancelled[NUM_CPORT_OUT_URB];
	spinlock_t cport_out_urb_lock;

	bool cdsi1_in_use;

	struct task_struct *apb_log_task;
	struct dentry *apb_log_dentry;
	struct dentry *apb_log_enable_dentry;
	DECLARE_KFIFO(apb_log_fifo, char, APB1_LOG_SIZE);

	__u8 arpc_endpoint_in;
	struct urb *arpc_urb[NUM_ARPC_IN_URB];
	u8 *arpc_buffer[NUM_ARPC_IN_URB];

	int arpc_id_cycle;
	spinlock_t arpc_lock;
	struct list_head arpcs;
};

struct arpc {
	struct list_head list;
	struct arpc_request_message *req;
	struct arpc_response_message *resp;
	struct completion response_received;
	bool active;
};

static inline struct es2_ap_dev *hd_to_es2(struct gb_host_device *hd)
{
	return (struct es2_ap_dev *)&hd->hd_priv;
}

static void cport_out_callback(struct urb *urb);
static void usb_log_enable(struct es2_ap_dev *es2);
static void usb_log_disable(struct es2_ap_dev *es2);
static int arpc_sync(struct es2_ap_dev *es2, u8 type, void *payload,
		     size_t size, int *result, unsigned int timeout);

static int output_sync(struct es2_ap_dev *es2, void *req, u16 size, u8 cmd)
{
	struct usb_device *udev = es2->usb_dev;
	u8 *data;
	int retval;

	data = kmemdup(req, size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 cmd,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE,
				 0, 0, data, size, ES2_USB_CTRL_TIMEOUT);
	if (retval < 0)
		dev_err(&udev->dev, "%s: return error %d\n", __func__, retval);
	else
		retval = 0;

	kfree(data);
	return retval;
}

static void ap_urb_complete(struct urb *urb)
{
	struct usb_ctrlrequest *dr = urb->context;

	kfree(dr);
	usb_free_urb(urb);
}

static int output_async(struct es2_ap_dev *es2, void *req, u16 size, u8 cmd)
{
	struct usb_device *udev = es2->usb_dev;
	struct urb *urb;
	struct usb_ctrlrequest *dr;
	u8 *buf;
	int retval;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;

	dr = kmalloc(sizeof(*dr) + size, GFP_ATOMIC);
	if (!dr) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	buf = (u8 *)dr + sizeof(*dr);
	memcpy(buf, req, size);

	dr->bRequest = cmd;
	dr->bRequestType = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE;
	dr->wValue = 0;
	dr->wIndex = 0;
	dr->wLength = cpu_to_le16(size);

	usb_fill_control_urb(urb, udev, usb_sndctrlpipe(udev, 0),
			     (unsigned char *)dr, buf, size,
			     ap_urb_complete, dr);
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval) {
		usb_free_urb(urb);
		kfree(dr);
	}
	return retval;
}

static int output(struct gb_host_device *hd, void *req, u16 size, u8 cmd,
		  bool async)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);

	if (async)
		return output_async(es2, req, size, cmd);

	return output_sync(es2, req, size, cmd);
}

static int es2_cport_in_enable(struct es2_ap_dev *es2,
			       struct es2_cport_in *cport_in)
{
	struct urb *urb;
	int ret;
	int i;

	for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
		urb = cport_in->urb[i];

		ret = usb_submit_urb(urb, GFP_KERNEL);
		if (ret) {
			dev_err(&es2->usb_dev->dev,
				"failed to submit in-urb: %d\n", ret);
			goto err_kill_urbs;
		}
	}

	return 0;

err_kill_urbs:
	for (--i; i >= 0; --i) {
		urb = cport_in->urb[i];
		usb_kill_urb(urb);
	}

	return ret;
}

static void es2_cport_in_disable(struct es2_ap_dev *es2,
				 struct es2_cport_in *cport_in)
{
	struct urb *urb;
	int i;

	for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
		urb = cport_in->urb[i];
		usb_kill_urb(urb);
	}
}

static int es2_arpc_in_enable(struct es2_ap_dev *es2)
{
	struct urb *urb;
	int ret;
	int i;

	for (i = 0; i < NUM_ARPC_IN_URB; ++i) {
		urb = es2->arpc_urb[i];

		ret = usb_submit_urb(urb, GFP_KERNEL);
		if (ret) {
			dev_err(&es2->usb_dev->dev,
				"failed to submit arpc in-urb: %d\n", ret);
			goto err_kill_urbs;
		}
	}

	return 0;

err_kill_urbs:
	for (--i; i >= 0; --i) {
		urb = es2->arpc_urb[i];
		usb_kill_urb(urb);
	}

	return ret;
}

static void es2_arpc_in_disable(struct es2_ap_dev *es2)
{
	struct urb *urb;
	int i;

	for (i = 0; i < NUM_ARPC_IN_URB; ++i) {
		urb = es2->arpc_urb[i];
		usb_kill_urb(urb);
	}
}

static struct urb *next_free_urb(struct es2_ap_dev *es2, gfp_t gfp_mask)
{
	struct urb *urb = NULL;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&es2->cport_out_urb_lock, flags);

	/* Look in our pool of allocated urbs first, as that's the "fastest" */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		if (!es2->cport_out_urb_busy[i] &&
		    !es2->cport_out_urb_cancelled[i]) {
			es2->cport_out_urb_busy[i] = true;
			urb = es2->cport_out_urb[i];
			break;
		}
	}
	spin_unlock_irqrestore(&es2->cport_out_urb_lock, flags);
	if (urb)
		return urb;

	/*
	 * Crap, pool is empty, complain to the syslog and go allocate one
	 * dynamically as we have to succeed.
	 */
	dev_dbg(&es2->usb_dev->dev,
		"No free CPort OUT urbs, having to dynamically allocate one!\n");
	return usb_alloc_urb(0, gfp_mask);
}

static void free_urb(struct es2_ap_dev *es2, struct urb *urb)
{
	unsigned long flags;
	int i;
	/*
	 * See if this was an urb in our pool, if so mark it "free", otherwise
	 * we need to free it ourselves.
	 */
	spin_lock_irqsave(&es2->cport_out_urb_lock, flags);
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		if (urb == es2->cport_out_urb[i]) {
			es2->cport_out_urb_busy[i] = false;
			urb = NULL;
			break;
		}
	}
	spin_unlock_irqrestore(&es2->cport_out_urb_lock, flags);

	/* If urb is not NULL, then we need to free this urb */
	usb_free_urb(urb);
}

/*
 * We (ab)use the operation-message header pad bytes to transfer the
 * cport id in order to minimise overhead.
 */
static void
gb_message_cport_pack(struct gb_operation_msg_hdr *header, u16 cport_id)
{
	header->pad[0] = cport_id;
}

/* Clear the pad bytes used for the CPort id */
static void gb_message_cport_clear(struct gb_operation_msg_hdr *header)
{
	header->pad[0] = 0;
}

/* Extract the CPort id packed into the header, and clear it */
static u16 gb_message_cport_unpack(struct gb_operation_msg_hdr *header)
{
	u16 cport_id = header->pad[0];

	gb_message_cport_clear(header);

	return cport_id;
}

/*
 * Returns zero if the message was successfully queued, or a negative errno
 * otherwise.
 */
static int message_send(struct gb_host_device *hd, u16 cport_id,
			struct gb_message *message, gfp_t gfp_mask)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;
	size_t buffer_size;
	int retval;
	struct urb *urb;
	unsigned long flags;

	/*
	 * The data actually transferred will include an indication
	 * of where the data should be sent.  Do one last check of
	 * the target CPort id before filling it in.
	 */
	if (!cport_id_valid(hd, cport_id)) {
		dev_err(&udev->dev, "invalid cport %u\n", cport_id);
		return -EINVAL;
	}

	/* Find a free urb */
	urb = next_free_urb(es2, gfp_mask);
	if (!urb)
		return -ENOMEM;

	spin_lock_irqsave(&es2->cport_out_urb_lock, flags);
	message->hcpriv = urb;
	spin_unlock_irqrestore(&es2->cport_out_urb_lock, flags);

	/* Pack the cport id into the message header */
	gb_message_cport_pack(message->header, cport_id);

	buffer_size = sizeof(*message->header) + message->payload_size;

	usb_fill_bulk_urb(urb, udev,
			  usb_sndbulkpipe(udev,
					  es2->cport_out_endpoint),
			  message->buffer, buffer_size,
			  cport_out_callback, message);
	urb->transfer_flags |= URB_ZERO_PACKET;

	trace_gb_message_submit(message);

	retval = usb_submit_urb(urb, gfp_mask);
	if (retval) {
		dev_err(&udev->dev, "failed to submit out-urb: %d\n", retval);

		spin_lock_irqsave(&es2->cport_out_urb_lock, flags);
		message->hcpriv = NULL;
		spin_unlock_irqrestore(&es2->cport_out_urb_lock, flags);

		free_urb(es2, urb);
		gb_message_cport_clear(message->header);

		return retval;
	}

	return 0;
}

/*
 * Can not be called in atomic context.
 */
static void message_cancel(struct gb_message *message)
{
	struct gb_host_device *hd = message->operation->connection->hd;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct urb *urb;
	int i;

	might_sleep();

	spin_lock_irq(&es2->cport_out_urb_lock);
	urb = message->hcpriv;

	/* Prevent dynamically allocated urb from being deallocated. */
	usb_get_urb(urb);

	/* Prevent pre-allocated urb from being reused. */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		if (urb == es2->cport_out_urb[i]) {
			es2->cport_out_urb_cancelled[i] = true;
			break;
		}
	}
	spin_unlock_irq(&es2->cport_out_urb_lock);

	usb_kill_urb(urb);

	if (i < NUM_CPORT_OUT_URB) {
		spin_lock_irq(&es2->cport_out_urb_lock);
		es2->cport_out_urb_cancelled[i] = false;
		spin_unlock_irq(&es2->cport_out_urb_lock);
	}

	usb_free_urb(urb);
}

static int es2_cport_allocate(struct gb_host_device *hd, int cport_id,
			      unsigned long flags)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct ida *id_map = &hd->cport_id_map;
	int ida_start, ida_end;

	switch (cport_id) {
	case ES2_CPORT_CDSI0:
	case ES2_CPORT_CDSI1:
		dev_err(&hd->dev, "cport %d not available\n", cport_id);
		return -EBUSY;
	}

	if (flags & GB_CONNECTION_FLAG_OFFLOADED &&
	    flags & GB_CONNECTION_FLAG_CDSI1) {
		if (es2->cdsi1_in_use) {
			dev_err(&hd->dev, "CDSI1 already in use\n");
			return -EBUSY;
		}

		es2->cdsi1_in_use = true;

		return ES2_CPORT_CDSI1;
	}

	if (cport_id < 0) {
		ida_start = 0;
		ida_end = hd->num_cports - 1;
	} else if (cport_id < hd->num_cports) {
		ida_start = cport_id;
		ida_end = cport_id;
	} else {
		dev_err(&hd->dev, "cport %d not available\n", cport_id);
		return -EINVAL;
	}

	return ida_alloc_range(id_map, ida_start, ida_end, GFP_KERNEL);
}

static void es2_cport_release(struct gb_host_device *hd, u16 cport_id)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);

	switch (cport_id) {
	case ES2_CPORT_CDSI1:
		es2->cdsi1_in_use = false;
		return;
	}

	ida_free(&hd->cport_id_map, cport_id);
}

static int cport_enable(struct gb_host_device *hd, u16 cport_id,
			unsigned long flags)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;
	struct gb_apb_request_cport_flags *req;
	u32 connection_flags;
	int ret;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	connection_flags = 0;
	if (flags & GB_CONNECTION_FLAG_CONTROL)
		connection_flags |= GB_APB_CPORT_FLAG_CONTROL;
	if (flags & GB_CONNECTION_FLAG_HIGH_PRIO)
		connection_flags |= GB_APB_CPORT_FLAG_HIGH_PRIO;

	req->flags = cpu_to_le32(connection_flags);

	dev_dbg(&hd->dev, "%s - cport = %u, flags = %02x\n", __func__,
		cport_id, connection_flags);

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      GB_APB_REQUEST_CPORT_FLAGS,
			      USB_DIR_OUT | USB_TYPE_VENDOR |
			      USB_RECIP_INTERFACE, cport_id, 0,
			      req, sizeof(*req), ES2_USB_CTRL_TIMEOUT);
	if (ret < 0) {
		dev_err(&udev->dev, "failed to set cport flags for port %d\n",
			cport_id);
		goto out;
	}

	ret = 0;
out:
	kfree(req);

	return ret;
}

static int es2_cport_connected(struct gb_host_device *hd, u16 cport_id)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct device *dev = &es2->usb_dev->dev;
	struct arpc_cport_connected_req req;
	int ret;

	req.cport_id = cpu_to_le16(cport_id);
	ret = arpc_sync(es2, ARPC_TYPE_CPORT_CONNECTED, &req, sizeof(req),
			NULL, ES2_ARPC_CPORT_TIMEOUT);
	if (ret) {
		dev_err(dev, "failed to set connected state for cport %u: %d\n",
			cport_id, ret);
		return ret;
	}

	return 0;
}

static int es2_cport_flush(struct gb_host_device *hd, u16 cport_id)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct device *dev = &es2->usb_dev->dev;
	struct arpc_cport_flush_req req;
	int ret;

	req.cport_id = cpu_to_le16(cport_id);
	ret = arpc_sync(es2, ARPC_TYPE_CPORT_FLUSH, &req, sizeof(req),
			NULL, ES2_ARPC_CPORT_TIMEOUT);
	if (ret) {
		dev_err(dev, "failed to flush cport %u: %d\n", cport_id, ret);
		return ret;
	}

	return 0;
}

static int es2_cport_shutdown(struct gb_host_device *hd, u16 cport_id,
			      u8 phase, unsigned int timeout)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct device *dev = &es2->usb_dev->dev;
	struct arpc_cport_shutdown_req req;
	int result;
	int ret;

	if (timeout > U16_MAX)
		return -EINVAL;

	req.cport_id = cpu_to_le16(cport_id);
	req.timeout = cpu_to_le16(timeout);
	req.phase = phase;
	ret = arpc_sync(es2, ARPC_TYPE_CPORT_SHUTDOWN, &req, sizeof(req),
			&result, ES2_ARPC_CPORT_TIMEOUT + timeout);
	if (ret) {
		dev_err(dev, "failed to send shutdown over cport %u: %d (%d)\n",
			cport_id, ret, result);
		return ret;
	}

	return 0;
}

static int es2_cport_quiesce(struct gb_host_device *hd, u16 cport_id,
			     size_t peer_space, unsigned int timeout)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct device *dev = &es2->usb_dev->dev;
	struct arpc_cport_quiesce_req req;
	int result;
	int ret;

	if (peer_space > U16_MAX)
		return -EINVAL;

	if (timeout > U16_MAX)
		return -EINVAL;

	req.cport_id = cpu_to_le16(cport_id);
	req.peer_space = cpu_to_le16(peer_space);
	req.timeout = cpu_to_le16(timeout);
	ret = arpc_sync(es2, ARPC_TYPE_CPORT_QUIESCE, &req, sizeof(req),
			&result, ES2_ARPC_CPORT_TIMEOUT + timeout);
	if (ret) {
		dev_err(dev, "failed to quiesce cport %u: %d (%d)\n",
			cport_id, ret, result);
		return ret;
	}

	return 0;
}

static int es2_cport_clear(struct gb_host_device *hd, u16 cport_id)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct device *dev = &es2->usb_dev->dev;
	struct arpc_cport_clear_req req;
	int ret;

	req.cport_id = cpu_to_le16(cport_id);
	ret = arpc_sync(es2, ARPC_TYPE_CPORT_CLEAR, &req, sizeof(req),
			NULL, ES2_ARPC_CPORT_TIMEOUT);
	if (ret) {
		dev_err(dev, "failed to clear cport %u: %d\n", cport_id, ret);
		return ret;
	}

	return 0;
}

static int latency_tag_enable(struct gb_host_device *hd, u16 cport_id)
{
	int retval;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 GB_APB_REQUEST_LATENCY_TAG_EN,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, cport_id, 0, NULL,
				 0, ES2_USB_CTRL_TIMEOUT);

	if (retval < 0)
		dev_err(&udev->dev, "Cannot enable latency tag for cport %d\n",
			cport_id);
	return retval;
}

static int latency_tag_disable(struct gb_host_device *hd, u16 cport_id)
{
	int retval;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 GB_APB_REQUEST_LATENCY_TAG_DIS,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, cport_id, 0, NULL,
				 0, ES2_USB_CTRL_TIMEOUT);

	if (retval < 0)
		dev_err(&udev->dev, "Cannot disable latency tag for cport %d\n",
			cport_id);
	return retval;
}

static struct gb_hd_driver es2_driver = {
	.hd_priv_size			= sizeof(struct es2_ap_dev),
	.message_send			= message_send,
	.message_cancel			= message_cancel,
	.cport_allocate			= es2_cport_allocate,
	.cport_release			= es2_cport_release,
	.cport_enable			= cport_enable,
	.cport_connected		= es2_cport_connected,
	.cport_flush			= es2_cport_flush,
	.cport_shutdown			= es2_cport_shutdown,
	.cport_quiesce			= es2_cport_quiesce,
	.cport_clear			= es2_cport_clear,
	.latency_tag_enable		= latency_tag_enable,
	.latency_tag_disable		= latency_tag_disable,
	.output				= output,
};

/* Common function to report consistent warnings based on URB status */
static int check_urb_status(struct urb *urb)
{
	struct device *dev = &urb->dev->dev;
	int status = urb->status;

	switch (status) {
	case 0:
		return 0;

	case -EOVERFLOW:
		dev_err(dev, "%s: overflow actual length is %d\n",
			__func__, urb->actual_length);
		fallthrough;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -EILSEQ:
	case -EPROTO:
		/* device is gone, stop sending */
		return status;
	}
	dev_err(dev, "%s: unknown status %d\n", __func__, status);

	return -EAGAIN;
}

static void es2_destroy(struct es2_ap_dev *es2)
{
	struct usb_device *udev;
	struct urb *urb;
	int i;

	debugfs_remove(es2->apb_log_enable_dentry);
	usb_log_disable(es2);

	/* Tear down everything! */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		urb = es2->cport_out_urb[i];
		usb_kill_urb(urb);
		usb_free_urb(urb);
		es2->cport_out_urb[i] = NULL;
		es2->cport_out_urb_busy[i] = false;	/* just to be anal */
	}

	for (i = 0; i < NUM_ARPC_IN_URB; ++i) {
		usb_free_urb(es2->arpc_urb[i]);
		kfree(es2->arpc_buffer[i]);
		es2->arpc_buffer[i] = NULL;
	}

	for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
		usb_free_urb(es2->cport_in.urb[i]);
		kfree(es2->cport_in.buffer[i]);
		es2->cport_in.buffer[i] = NULL;
	}

	/* release reserved CDSI0 and CDSI1 cports */
	gb_hd_cport_release_reserved(es2->hd, ES2_CPORT_CDSI1);
	gb_hd_cport_release_reserved(es2->hd, ES2_CPORT_CDSI0);

	udev = es2->usb_dev;
	gb_hd_put(es2->hd);

	usb_put_dev(udev);
}

static void cport_in_callback(struct urb *urb)
{
	struct gb_host_device *hd = urb->context;
	struct device *dev = &urb->dev->dev;
	struct gb_operation_msg_hdr *header;
	int status = check_urb_status(urb);
	int retval;
	u16 cport_id;

	if (status) {
		if ((status == -EAGAIN) || (status == -EPROTO))
			goto exit;

		/* The urb is being unlinked */
		if (status == -ENOENT || status == -ESHUTDOWN)
			return;

		dev_err(dev, "urb cport in error %d (dropped)\n", status);
		return;
	}

	if (urb->actual_length < sizeof(*header)) {
		dev_err(dev, "short message received\n");
		goto exit;
	}

	/* Extract the CPort id, which is packed in the message header */
	header = urb->transfer_buffer;
	cport_id = gb_message_cport_unpack(header);

	if (cport_id_valid(hd, cport_id)) {
		greybus_data_rcvd(hd, cport_id, urb->transfer_buffer,
				  urb->actual_length);
	} else {
		dev_err(dev, "invalid cport id %u received\n", cport_id);
	}
exit:
	/* put our urb back in the request pool */
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(dev, "failed to resubmit in-urb: %d\n", retval);
}

static void cport_out_callback(struct urb *urb)
{
	struct gb_message *message = urb->context;
	struct gb_host_device *hd = message->operation->connection->hd;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	int status = check_urb_status(urb);
	unsigned long flags;

	gb_message_cport_clear(message->header);

	spin_lock_irqsave(&es2->cport_out_urb_lock, flags);
	message->hcpriv = NULL;
	spin_unlock_irqrestore(&es2->cport_out_urb_lock, flags);

	/*
	 * Tell the submitter that the message send (attempt) is
	 * complete, and report the status.
	 */
	greybus_message_sent(hd, message, status);

	free_urb(es2, urb);
}

static struct arpc *arpc_alloc(void *payload, u16 size, u8 type)
{
	struct arpc *rpc;

	if (size + sizeof(*rpc->req) > ARPC_OUT_SIZE_MAX)
		return NULL;

	rpc = kzalloc(sizeof(*rpc), GFP_KERNEL);
	if (!rpc)
		return NULL;

	INIT_LIST_HEAD(&rpc->list);
	rpc->req = kzalloc(sizeof(*rpc->req) + size, GFP_KERNEL);
	if (!rpc->req)
		goto err_free_rpc;

	rpc->resp = kzalloc(sizeof(*rpc->resp), GFP_KERNEL);
	if (!rpc->resp)
		goto err_free_req;

	rpc->req->type = type;
	rpc->req->size = cpu_to_le16(sizeof(*rpc->req) + size);
	memcpy(rpc->req->data, payload, size);

	init_completion(&rpc->response_received);

	return rpc;

err_free_req:
	kfree(rpc->req);
err_free_rpc:
	kfree(rpc);

	return NULL;
}

static void arpc_free(struct arpc *rpc)
{
	kfree(rpc->req);
	kfree(rpc->resp);
	kfree(rpc);
}

static struct arpc *arpc_find(struct es2_ap_dev *es2, __le16 id)
{
	struct arpc *rpc;

	list_for_each_entry(rpc, &es2->arpcs, list) {
		if (rpc->req->id == id)
			return rpc;
	}

	return NULL;
}

static void arpc_add(struct es2_ap_dev *es2, struct arpc *rpc)
{
	rpc->active = true;
	rpc->req->id = cpu_to_le16(es2->arpc_id_cycle++);
	list_add_tail(&rpc->list, &es2->arpcs);
}

static void arpc_del(struct es2_ap_dev *es2, struct arpc *rpc)
{
	if (rpc->active) {
		rpc->active = false;
		list_del(&rpc->list);
	}
}

static int arpc_send(struct es2_ap_dev *es2, struct arpc *rpc, int timeout)
{
	struct usb_device *udev = es2->usb_dev;
	int retval;

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 GB_APB_REQUEST_ARPC_RUN,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE,
				 0, 0,
				 rpc->req, le16_to_cpu(rpc->req->size),
				 ES2_USB_CTRL_TIMEOUT);
	if (retval < 0) {
		dev_err(&udev->dev,
			"failed to send ARPC request %d: %d\n",
			rpc->req->type, retval);
		return retval;
	}

	return 0;
}

static int arpc_sync(struct es2_ap_dev *es2, u8 type, void *payload,
		     size_t size, int *result, unsigned int timeout)
{
	struct arpc *rpc;
	unsigned long flags;
	int retval;

	if (result)
		*result = 0;

	rpc = arpc_alloc(payload, size, type);
	if (!rpc)
		return -ENOMEM;

	spin_lock_irqsave(&es2->arpc_lock, flags);
	arpc_add(es2, rpc);
	spin_unlock_irqrestore(&es2->arpc_lock, flags);

	retval = arpc_send(es2, rpc, timeout);
	if (retval)
		goto out_arpc_del;

	retval = wait_for_completion_interruptible_timeout(
						&rpc->response_received,
						msecs_to_jiffies(timeout));
	if (retval <= 0) {
		if (!retval)
			retval = -ETIMEDOUT;
		goto out_arpc_del;
	}

	if (rpc->resp->result) {
		retval = -EREMOTEIO;
		if (result)
			*result = rpc->resp->result;
	} else {
		retval = 0;
	}

out_arpc_del:
	spin_lock_irqsave(&es2->arpc_lock, flags);
	arpc_del(es2, rpc);
	spin_unlock_irqrestore(&es2->arpc_lock, flags);
	arpc_free(rpc);

	if (retval < 0 && retval != -EREMOTEIO) {
		dev_err(&es2->usb_dev->dev,
			"failed to execute ARPC: %d\n", retval);
	}

	return retval;
}

static void arpc_in_callback(struct urb *urb)
{
	struct es2_ap_dev *es2 = urb->context;
	struct device *dev = &urb->dev->dev;
	int status = check_urb_status(urb);
	struct arpc *rpc;
	struct arpc_response_message *resp;
	unsigned long flags;
	int retval;

	if (status) {
		if ((status == -EAGAIN) || (status == -EPROTO))
			goto exit;

		/* The urb is being unlinked */
		if (status == -ENOENT || status == -ESHUTDOWN)
			return;

		dev_err(dev, "arpc in-urb error %d (dropped)\n", status);
		return;
	}

	if (urb->actual_length < sizeof(*resp)) {
		dev_err(dev, "short aprc response received\n");
		goto exit;
	}

	resp = urb->transfer_buffer;
	spin_lock_irqsave(&es2->arpc_lock, flags);
	rpc = arpc_find(es2, resp->id);
	if (!rpc) {
		dev_err(dev, "invalid arpc response id received: %u\n",
			le16_to_cpu(resp->id));
		spin_unlock_irqrestore(&es2->arpc_lock, flags);
		goto exit;
	}

	arpc_del(es2, rpc);
	memcpy(rpc->resp, resp, sizeof(*resp));
	complete(&rpc->response_received);
	spin_unlock_irqrestore(&es2->arpc_lock, flags);

exit:
	/* put our urb back in the request pool */
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(dev, "failed to resubmit arpc in-urb: %d\n", retval);
}

#define APB1_LOG_MSG_SIZE	64
static void apb_log_get(struct es2_ap_dev *es2, char *buf)
{
	int retval;

	do {
		retval = usb_control_msg(es2->usb_dev,
					 usb_rcvctrlpipe(es2->usb_dev, 0),
					 GB_APB_REQUEST_LOG,
					 USB_DIR_IN | USB_TYPE_VENDOR |
					 USB_RECIP_INTERFACE,
					 0x00, 0x00,
					 buf,
					 APB1_LOG_MSG_SIZE,
					 ES2_USB_CTRL_TIMEOUT);
		if (retval > 0)
			kfifo_in(&es2->apb_log_fifo, buf, retval);
	} while (retval > 0);
}

static int apb_log_poll(void *data)
{
	struct es2_ap_dev *es2 = data;
	char *buf;

	buf = kmalloc(APB1_LOG_MSG_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	while (!kthread_should_stop()) {
		msleep(1000);
		apb_log_get(es2, buf);
	}

	kfree(buf);

	return 0;
}

static ssize_t apb_log_read(struct file *f, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct es2_ap_dev *es2 = file_inode(f)->i_private;
	ssize_t ret;
	size_t copied;
	char *tmp_buf;

	if (count > APB1_LOG_SIZE)
		count = APB1_LOG_SIZE;

	tmp_buf = kmalloc(count, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	copied = kfifo_out(&es2->apb_log_fifo, tmp_buf, count);
	ret = simple_read_from_buffer(buf, count, ppos, tmp_buf, copied);

	kfree(tmp_buf);

	return ret;
}

static const struct file_operations apb_log_fops = {
	.read	= apb_log_read,
};

static void usb_log_enable(struct es2_ap_dev *es2)
{
	if (!IS_ERR_OR_NULL(es2->apb_log_task))
		return;

	/* get log from APB1 */
	es2->apb_log_task = kthread_run(apb_log_poll, es2, "apb_log");
	if (IS_ERR(es2->apb_log_task))
		return;
	/* XXX We will need to rename this per APB */
	es2->apb_log_dentry = debugfs_create_file("apb_log", 0444,
						  gb_debugfs_get(), es2,
						  &apb_log_fops);
}

static void usb_log_disable(struct es2_ap_dev *es2)
{
	if (IS_ERR_OR_NULL(es2->apb_log_task))
		return;

	debugfs_remove(es2->apb_log_dentry);
	es2->apb_log_dentry = NULL;

	kthread_stop(es2->apb_log_task);
	es2->apb_log_task = NULL;
}

static ssize_t apb_log_enable_read(struct file *f, char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct es2_ap_dev *es2 = file_inode(f)->i_private;
	int enable = !IS_ERR_OR_NULL(es2->apb_log_task);
	char tmp_buf[3];

	sprintf(tmp_buf, "%d\n", enable);
	return simple_read_from_buffer(buf, count, ppos, tmp_buf, 2);
}

static ssize_t apb_log_enable_write(struct file *f, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	int enable;
	ssize_t retval;
	struct es2_ap_dev *es2 = file_inode(f)->i_private;

	retval = kstrtoint_from_user(buf, count, 10, &enable);
	if (retval)
		return retval;

	if (enable)
		usb_log_enable(es2);
	else
		usb_log_disable(es2);

	return count;
}

static const struct file_operations apb_log_enable_fops = {
	.read	= apb_log_enable_read,
	.write	= apb_log_enable_write,
};

static int apb_get_cport_count(struct usb_device *udev)
{
	int retval;
	__le16 *cport_count;

	cport_count = kzalloc(sizeof(*cport_count), GFP_KERNEL);
	if (!cport_count)
		return -ENOMEM;

	retval = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				 GB_APB_REQUEST_CPORT_COUNT,
				 USB_DIR_IN | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, 0, 0, cport_count,
				 sizeof(*cport_count), ES2_USB_CTRL_TIMEOUT);
	if (retval != sizeof(*cport_count)) {
		dev_err(&udev->dev, "Cannot retrieve CPort count: %d\n",
			retval);

		if (retval >= 0)
			retval = -EIO;

		goto out;
	}

	retval = le16_to_cpu(*cport_count);

	/* We need to fit a CPort ID in one byte of a message header */
	if (retval > U8_MAX) {
		retval = U8_MAX;
		dev_warn(&udev->dev, "Limiting number of CPorts to U8_MAX\n");
	}

out:
	kfree(cport_count);
	return retval;
}

/*
 * The ES2 USB Bridge device has 15 endpoints
 * 1 Control - usual USB stuff + AP -> APBridgeA messages
 * 7 Bulk IN - CPort data in
 * 7 Bulk OUT - CPort data out
 */
static int ap_probe(struct usb_interface *interface,
		    const struct usb_device_id *id)
{
	struct es2_ap_dev *es2;
	struct gb_host_device *hd;
	struct usb_device *udev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	__u8 ep_addr;
	int retval;
	int i;
	int num_cports;
	bool bulk_out_found = false;
	bool bulk_in_found = false;
	bool arpc_in_found = false;

	udev = usb_get_dev(interface_to_usbdev(interface));

	num_cports = apb_get_cport_count(udev);
	if (num_cports < 0) {
		usb_put_dev(udev);
		dev_err(&udev->dev, "Cannot retrieve CPort count: %d\n",
			num_cports);
		return num_cports;
	}

	hd = gb_hd_create(&es2_driver, &udev->dev, ES2_GBUF_MSG_SIZE_MAX,
			  num_cports);
	if (IS_ERR(hd)) {
		usb_put_dev(udev);
		return PTR_ERR(hd);
	}

	es2 = hd_to_es2(hd);
	es2->hd = hd;
	es2->usb_intf = interface;
	es2->usb_dev = udev;
	spin_lock_init(&es2->cport_out_urb_lock);
	INIT_KFIFO(es2->apb_log_fifo);
	usb_set_intfdata(interface, es2);

	/*
	 * Reserve the CDSI0 and CDSI1 CPorts so they won't be allocated
	 * dynamically.
	 */
	retval = gb_hd_cport_reserve(hd, ES2_CPORT_CDSI0);
	if (retval)
		goto error;
	retval = gb_hd_cport_reserve(hd, ES2_CPORT_CDSI1);
	if (retval)
		goto error;

	/* find all bulk endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		ep_addr = endpoint->bEndpointAddress;

		if (usb_endpoint_is_bulk_in(endpoint)) {
			if (!bulk_in_found) {
				es2->cport_in.endpoint = ep_addr;
				bulk_in_found = true;
			} else if (!arpc_in_found) {
				es2->arpc_endpoint_in = ep_addr;
				arpc_in_found = true;
			} else {
				dev_warn(&udev->dev,
					 "Unused bulk IN endpoint found: 0x%02x\n",
					 ep_addr);
			}
			continue;
		}
		if (usb_endpoint_is_bulk_out(endpoint)) {
			if (!bulk_out_found) {
				es2->cport_out_endpoint = ep_addr;
				bulk_out_found = true;
			} else {
				dev_warn(&udev->dev,
					 "Unused bulk OUT endpoint found: 0x%02x\n",
					 ep_addr);
			}
			continue;
		}
		dev_warn(&udev->dev,
			 "Unknown endpoint type found, address 0x%02x\n",
			 ep_addr);
	}
	if (!bulk_in_found || !arpc_in_found || !bulk_out_found) {
		dev_err(&udev->dev, "Not enough endpoints found in device, aborting!\n");
		retval = -ENODEV;
		goto error;
	}

	/* Allocate buffers for our cport in messages */
	for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
		struct urb *urb;
		u8 *buffer;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			retval = -ENOMEM;
			goto error;
		}
		es2->cport_in.urb[i] = urb;

		buffer = kmalloc(ES2_GBUF_MSG_SIZE_MAX, GFP_KERNEL);
		if (!buffer) {
			retval = -ENOMEM;
			goto error;
		}

		usb_fill_bulk_urb(urb, udev,
				  usb_rcvbulkpipe(udev, es2->cport_in.endpoint),
				  buffer, ES2_GBUF_MSG_SIZE_MAX,
				  cport_in_callback, hd);

		es2->cport_in.buffer[i] = buffer;
	}

	/* Allocate buffers for ARPC in messages */
	for (i = 0; i < NUM_ARPC_IN_URB; ++i) {
		struct urb *urb;
		u8 *buffer;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			retval = -ENOMEM;
			goto error;
		}
		es2->arpc_urb[i] = urb;

		buffer = kmalloc(ARPC_IN_SIZE_MAX, GFP_KERNEL);
		if (!buffer) {
			retval = -ENOMEM;
			goto error;
		}

		usb_fill_bulk_urb(urb, udev,
				  usb_rcvbulkpipe(udev,
						  es2->arpc_endpoint_in),
				  buffer, ARPC_IN_SIZE_MAX,
				  arpc_in_callback, es2);

		es2->arpc_buffer[i] = buffer;
	}

	/* Allocate urbs for our CPort OUT messages */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		struct urb *urb;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			retval = -ENOMEM;
			goto error;
		}

		es2->cport_out_urb[i] = urb;
		es2->cport_out_urb_busy[i] = false;	/* just to be anal */
	}

	/* XXX We will need to rename this per APB */
	es2->apb_log_enable_dentry = debugfs_create_file("apb_log_enable",
							 0644,
							 gb_debugfs_get(), es2,
							 &apb_log_enable_fops);

	INIT_LIST_HEAD(&es2->arpcs);
	spin_lock_init(&es2->arpc_lock);

	retval = es2_arpc_in_enable(es2);
	if (retval)
		goto error;

	retval = gb_hd_add(hd);
	if (retval)
		goto err_disable_arpc_in;

	retval = es2_cport_in_enable(es2, &es2->cport_in);
	if (retval)
		goto err_hd_del;

	return 0;

err_hd_del:
	gb_hd_del(hd);
err_disable_arpc_in:
	es2_arpc_in_disable(es2);
error:
	es2_destroy(es2);

	return retval;
}

static void ap_disconnect(struct usb_interface *interface)
{
	struct es2_ap_dev *es2 = usb_get_intfdata(interface);

	gb_hd_del(es2->hd);

	es2_cport_in_disable(es2, &es2->cport_in);
	es2_arpc_in_disable(es2);

	es2_destroy(es2);
}

static struct usb_driver es2_ap_driver = {
	.name =		"es2_ap_driver",
	.probe =	ap_probe,
	.disconnect =	ap_disconnect,
	.id_table =	id_table,
	.soft_unbind =	1,
};

module_usb_driver(es2_ap_driver);

MODULE_DESCRIPTION("Greybus AP USB driver for ES2 controller chips");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
