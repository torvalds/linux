/*
 * Greybus "AP" USB driver for "ES2" controller chips
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/kthread.h>
#include <linux/sizes.h>
#include <linux/usb.h>
#include <linux/kfifo.h>
#include <linux/debugfs.h>
#include <asm/unaligned.h>

#include "greybus.h"
#include "kernel_ver.h"
#include "connection.h"
#include "greybus_trace.h"


/* Fixed CPort numbers */
#define ES2_CPORT_CDSI0		16
#define ES2_CPORT_CDSI1		17

/* Memory sizes for the buffers sent to/from the ES2 controller */
#define ES2_GBUF_MSG_SIZE_MAX	2048

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x18d1, 0x1eaf) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

#define APB1_LOG_SIZE		SZ_16K

/* Number of bulk in and bulk out couple */
#define NUM_BULKS		7

/*
 * Number of CPort IN urbs in flight at any point in time.
 * Adjust if we are having stalls in the USB buffer due to not enough urbs in
 * flight.
 */
#define NUM_CPORT_IN_URB	4

/* Number of CPort OUT urbs in flight at any point in time.
 * Adjust if we get messages saying we are out of urbs in the system log.
 */
#define NUM_CPORT_OUT_URB	(8 * NUM_BULKS)

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

/*
 * @endpoint: bulk out endpoint for CPort data
 */
struct es2_cport_out {
	__u8 endpoint;
};

/**
 * es2_ap_dev - ES2 USB Bridge to AP structure
 * @usb_dev: pointer to the USB device we are.
 * @usb_intf: pointer to the USB interface we are bound to.
 * @hd: pointer to our gb_host_device structure

 * @cport_in: endpoint, urbs and buffer for cport in messages
 * @cport_out: endpoint for for cport out messages
 * @cport_out_urb: array of urbs for the CPort out messages
 * @cport_out_urb_busy: array of flags to see if the @cport_out_urb is busy or
 *			not.
 * @cport_out_urb_cancelled: array of flags indicating whether the
 *			corresponding @cport_out_urb is being cancelled
 * @cport_out_urb_lock: locks the @cport_out_urb_busy "list"
 *
 * @apb_log_task: task pointer for logging thread
 * @apb_log_dentry: file system entry for the log file interface
 * @apb_log_enable_dentry: file system entry for enabling logging
 * @apb_log_fifo: kernel FIFO to carry logged data
 */
struct es2_ap_dev {
	struct usb_device *usb_dev;
	struct usb_interface *usb_intf;
	struct gb_host_device *hd;

	struct es2_cport_in cport_in[NUM_BULKS];
	struct es2_cport_out cport_out[NUM_BULKS];
	struct urb *cport_out_urb[NUM_CPORT_OUT_URB];
	bool cport_out_urb_busy[NUM_CPORT_OUT_URB];
	bool cport_out_urb_cancelled[NUM_CPORT_OUT_URB];
	spinlock_t cport_out_urb_lock;

	bool cdsi1_in_use;

	int *cport_to_ep;

	struct task_struct *apb_log_task;
	struct dentry *apb_log_dentry;
	struct dentry *apb_log_enable_dentry;
	DECLARE_KFIFO(apb_log_fifo, char, APB1_LOG_SIZE);
};

/**
 * cport_to_ep - information about cport to endpoints mapping
 * @cport_id: the id of cport to map to endpoints
 * @endpoint_in: the endpoint number to use for in transfer
 * @endpoint_out: he endpoint number to use for out transfer
 */
struct cport_to_ep {
	__le16 cport_id;
	__u8 endpoint_in;
	__u8 endpoint_out;
};

/**
 * timesync_enable_request - Enable timesync in an APBridge
 * @count: number of TimeSync Pulses to expect
 * @frame_time: the initial FrameTime at the first TimeSync Pulse
 * @strobe_delay: the expected delay in microseconds between each TimeSync Pulse
 * @refclk: The AP mandated reference clock to run FrameTime at
 */
struct timesync_enable_request {
	__u8	count;
	__le64	frame_time;
	__le32	strobe_delay;
	__le32	refclk;
} __packed;

/**
 * timesync_authoritative_request - Transmit authoritative FrameTime to APBridge
 * @frame_time: An array of authoritative FrameTimes provided by the SVC
 *              and relayed to the APBridge by the AP
 */
struct timesync_authoritative_request {
	__le64	frame_time[GB_TIMESYNC_MAX_STROBES];
} __packed;

static inline struct es2_ap_dev *hd_to_es2(struct gb_host_device *hd)
{
	return (struct es2_ap_dev *)&hd->hd_priv;
}

static void cport_out_callback(struct urb *urb);
static void usb_log_enable(struct es2_ap_dev *es2);
static void usb_log_disable(struct es2_ap_dev *es2);

/* Get the endpoints pair mapped to the cport */
static int cport_to_ep_pair(struct es2_ap_dev *es2, u16 cport_id)
{
	if (cport_id >= es2->hd->num_cports)
		return 0;
	return es2->cport_to_ep[cport_id];
}

#define ES2_TIMEOUT	500	/* 500 ms for the SVC to do something */

/* Disable for now until we work all of this out to keep a warning-free build */
#if 0
/* Test if the endpoints pair is already mapped to a cport */
static int ep_pair_in_use(struct es2_ap_dev *es2, int ep_pair)
{
	int i;

	for (i = 0; i < es2->hd->num_cports; i++) {
		if (es2->cport_to_ep[i] == ep_pair)
			return 1;
	}
	return 0;
}

/* Configure the endpoint mapping and send the request to APBridge */
static int map_cport_to_ep(struct es2_ap_dev *es2,
				u16 cport_id, int ep_pair)
{
	int retval;
	struct cport_to_ep *cport_to_ep;

	if (ep_pair < 0 || ep_pair >= NUM_BULKS)
		return -EINVAL;
	if (cport_id >= es2->hd->num_cports)
		return -EINVAL;
	if (ep_pair && ep_pair_in_use(es2, ep_pair))
		return -EINVAL;

	cport_to_ep = kmalloc(sizeof(*cport_to_ep), GFP_KERNEL);
	if (!cport_to_ep)
		return -ENOMEM;

	es2->cport_to_ep[cport_id] = ep_pair;
	cport_to_ep->cport_id = cpu_to_le16(cport_id);
	cport_to_ep->endpoint_in = es2->cport_in[ep_pair].endpoint;
	cport_to_ep->endpoint_out = es2->cport_out[ep_pair].endpoint;

	retval = usb_control_msg(es2->usb_dev,
				 usb_sndctrlpipe(es2->usb_dev, 0),
				 GB_APB_REQUEST_EP_MAPPING,
				 USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
				 0x00, 0x00,
				 (char *)cport_to_ep,
				 sizeof(*cport_to_ep),
				 ES2_TIMEOUT);
	if (retval == sizeof(*cport_to_ep))
		retval = 0;
	kfree(cport_to_ep);

	return retval;
}

/* Unmap a cport: use the muxed endpoints pair */
static int unmap_cport(struct es2_ap_dev *es2, u16 cport_id)
{
	return map_cport_to_ep(es2, cport_id, 0);
}
#endif

static int output_sync(struct es2_ap_dev *es2, void *req, u16 size, u8 cmd)
{
	struct usb_device *udev = es2->usb_dev;
	u8 *data;
	int retval;

	data = kmalloc(size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	memcpy(data, req, size);

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 cmd,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE,
				 0, 0, data, size, ES2_TIMEOUT);
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

static struct urb *next_free_urb(struct es2_ap_dev *es2, gfp_t gfp_mask)
{
	struct urb *urb = NULL;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&es2->cport_out_urb_lock, flags);

	/* Look in our pool of allocated urbs first, as that's the "fastest" */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		if (es2->cport_out_urb_busy[i] == false &&
				es2->cport_out_urb_cancelled[i] == false) {
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
	int ep_pair;
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

	ep_pair = cport_to_ep_pair(es2, cport_id);
	usb_fill_bulk_urb(urb, udev,
			  usb_sndbulkpipe(udev,
					  es2->cport_out[ep_pair].endpoint),
			  message->buffer, buffer_size,
			  cport_out_callback, message);
	urb->transfer_flags |= URB_ZERO_PACKET;
	trace_gb_host_device_send(hd, cport_id, buffer_size);
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

static int cport_reset(struct gb_host_device *hd, u16 cport_id)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;
	int retval;

	switch (cport_id) {
	case GB_SVC_CPORT_ID:
	case ES2_CPORT_CDSI0:
	case ES2_CPORT_CDSI1:
		return 0;
	}

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 GB_APB_REQUEST_RESET_CPORT,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, cport_id, 0,
				 NULL, 0, ES2_TIMEOUT);
	if (retval < 0) {
		dev_err(&udev->dev, "failed to reset cport %u: %d\n", cport_id,
			retval);
		return retval;
	}

	return 0;
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
		ida_end = hd->num_cports;
	} else if (cport_id < hd->num_cports) {
		ida_start = cport_id;
		ida_end = cport_id + 1;
	} else {
		dev_err(&hd->dev, "cport %d not available\n", cport_id);
		return -EINVAL;
	}

	return ida_simple_get(id_map, ida_start, ida_end, GFP_KERNEL);
}

static void es2_cport_release(struct gb_host_device *hd, u16 cport_id)
{
	struct es2_ap_dev *es2 = hd_to_es2(hd);

	switch (cport_id) {
	case ES2_CPORT_CDSI1:
		es2->cdsi1_in_use = false;
		return;
	}

	ida_simple_remove(&hd->cport_id_map, cport_id);
}

static int cport_enable(struct gb_host_device *hd, u16 cport_id)
{
	int retval;

	retval = cport_reset(hd, cport_id);
	if (retval)
		return retval;

	return 0;
}

static int latency_tag_enable(struct gb_host_device *hd, u16 cport_id)
{
	int retval;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;

	if (!cport_id_valid(hd, cport_id)) {
		dev_err(&udev->dev, "invalid cport %u\n", cport_id);
		return -EINVAL;
	}

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 GB_APB_REQUEST_LATENCY_TAG_EN,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, cport_id, 0, NULL,
				 0, ES2_TIMEOUT);

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

	if (!cport_id_valid(hd, cport_id)) {
		dev_err(&udev->dev, "invalid cport %u\n", cport_id);
		return -EINVAL;
	}

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 GB_APB_REQUEST_LATENCY_TAG_DIS,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, cport_id, 0, NULL,
				 0, ES2_TIMEOUT);

	if (retval < 0)
		dev_err(&udev->dev, "Cannot disable latency tag for cport %d\n",
			cport_id);
	return retval;
}

static int cport_features_enable(struct gb_host_device *hd, u16 cport_id)
{
	int retval;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 GB_APB_REQUEST_CPORT_FEAT_EN,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, cport_id, 0, NULL,
				 0, ES2_TIMEOUT);
	if (retval < 0)
		dev_err(&udev->dev, "Cannot enable CPort features for cport %u: %d\n",
			cport_id, retval);
	return retval;
}

static int cport_features_disable(struct gb_host_device *hd, u16 cport_id)
{
	int retval;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 GB_APB_REQUEST_CPORT_FEAT_DIS,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, cport_id, 0, NULL,
				 0, ES2_TIMEOUT);
	if (retval < 0)
		dev_err(&udev->dev,
			"Cannot disable CPort features for cport %u: %d\n",
			cport_id, retval);
	return retval;
}

static int timesync_enable(struct gb_host_device *hd, u8 count,
			   u64 frame_time, u32 strobe_delay, u32 refclk)
{
	int retval;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;
	struct gb_control_timesync_enable_request *request;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	request->count = count;
	request->frame_time = cpu_to_le64(frame_time);
	request->strobe_delay = cpu_to_le32(strobe_delay);
	request->refclk = cpu_to_le32(refclk);
	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 REQUEST_TIMESYNC_ENABLE,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, 0, 0, request,
				 sizeof(*request), ES2_TIMEOUT);
	if (retval < 0)
		dev_err(&udev->dev, "Cannot enable timesync %d\n", retval);

	kfree(request);
	return retval;
}

static int timesync_disable(struct gb_host_device *hd)
{
	int retval;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 REQUEST_TIMESYNC_DISABLE,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, 0, 0, NULL,
				 0, ES2_TIMEOUT);
	if (retval < 0)
		dev_err(&udev->dev, "Cannot disable timesync %d\n", retval);

	return retval;
}

static int timesync_authoritative(struct gb_host_device *hd, u64 *frame_time)
{
	int retval, i;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;
	struct timesync_authoritative_request *request;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	for (i = 0; i < GB_TIMESYNC_MAX_STROBES; i++)
		request->frame_time[i] = cpu_to_le64(frame_time[i]);

	retval = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 REQUEST_TIMESYNC_AUTHORITATIVE,
				 USB_DIR_OUT | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, 0, 0, request,
				 sizeof(*request), ES2_TIMEOUT);
	if (retval < 0)
		dev_err(&udev->dev, "Cannot timesync authoritative out %d\n", retval);

	kfree(request);
	return retval;
}

static int timesync_get_last_event(struct gb_host_device *hd, u64 *frame_time)
{
	int retval;
	struct es2_ap_dev *es2 = hd_to_es2(hd);
	struct usb_device *udev = es2->usb_dev;
	__le64 *response_frame_time;

	response_frame_time = kzalloc(sizeof(*response_frame_time), GFP_KERNEL);
	if (!response_frame_time)
		return -ENOMEM;

	retval = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				 REQUEST_TIMESYNC_GET_LAST_EVENT,
				 USB_DIR_IN | USB_TYPE_VENDOR |
				 USB_RECIP_INTERFACE, 0, 0, response_frame_time,
				 sizeof(*response_frame_time), ES2_TIMEOUT);

	if (retval != sizeof(*response_frame_time)) {
		dev_err(&udev->dev, "Cannot get last TimeSync event: %d\n",
			retval);

		if (retval >= 0)
			retval = -EIO;

		goto out;
	}
	*frame_time = le64_to_cpu(*response_frame_time);
	retval = 0;
out:
	kfree(response_frame_time);
	return retval;
}

static struct gb_hd_driver es2_driver = {
	.hd_priv_size			= sizeof(struct es2_ap_dev),
	.message_send			= message_send,
	.message_cancel			= message_cancel,
	.cport_allocate			= es2_cport_allocate,
	.cport_release			= es2_cport_release,
	.cport_enable			= cport_enable,
	.latency_tag_enable		= latency_tag_enable,
	.latency_tag_disable		= latency_tag_disable,
	.output				= output,
	.cport_features_enable		= cport_features_enable,
	.cport_features_disable		= cport_features_disable,
	.timesync_enable		= timesync_enable,
	.timesync_disable		= timesync_disable,
	.timesync_authoritative		= timesync_authoritative,
	.timesync_get_last_event	= timesync_get_last_event,
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
	int bulk_in;
	int i;

	debugfs_remove(es2->apb_log_enable_dentry);
	usb_log_disable(es2);

	/* Tear down everything! */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		struct urb *urb = es2->cport_out_urb[i];

		if (!urb)
			break;
		usb_kill_urb(urb);
		usb_free_urb(urb);
		es2->cport_out_urb[i] = NULL;
		es2->cport_out_urb_busy[i] = false;	/* just to be anal */
	}

	for (bulk_in = 0; bulk_in < NUM_BULKS; bulk_in++) {
		struct es2_cport_in *cport_in = &es2->cport_in[bulk_in];

		for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
			struct urb *urb = cport_in->urb[i];

			if (!urb)
				break;
			usb_free_urb(urb);
			kfree(cport_in->buffer[i]);
			cport_in->buffer[i] = NULL;
		}
	}

	kfree(es2->cport_to_ep);

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
		trace_gb_host_device_recv(hd, cport_id, urb->actual_length);
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

#define APB1_LOG_MSG_SIZE	64
static void apb_log_get(struct es2_ap_dev *es2, char *buf)
{
	int retval;

	/* SVC messages go down our control pipe */
	do {
		retval = usb_control_msg(es2->usb_dev,
					usb_rcvctrlpipe(es2->usb_dev, 0),
					GB_APB_REQUEST_LOG,
					USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
					0x00, 0x00,
					buf,
					APB1_LOG_MSG_SIZE,
					ES2_TIMEOUT);
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
	struct es2_ap_dev *es2 = f->f_inode->i_private;
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
	es2->apb_log_dentry = debugfs_create_file("apb_log", S_IRUGO,
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
	struct es2_ap_dev *es2 = f->f_inode->i_private;
	int enable = !IS_ERR_OR_NULL(es2->apb_log_task);
	char tmp_buf[3];

	sprintf(tmp_buf, "%d\n", enable);
	return simple_read_from_buffer(buf, count, ppos, tmp_buf, 3);
}

static ssize_t apb_log_enable_write(struct file *f, const char __user *buf,
				size_t count, loff_t *ppos)
{
	int enable;
	ssize_t retval;
	struct es2_ap_dev *es2 = f->f_inode->i_private;

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
				 sizeof(*cport_count), ES2_TIMEOUT);
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
	int bulk_in = 0;
	int bulk_out = 0;
	int retval;
	int i;
	int num_cports;

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

	es2->cport_to_ep = kcalloc(hd->num_cports, sizeof(*es2->cport_to_ep),
				   GFP_KERNEL);
	if (!es2->cport_to_ep) {
		retval = -ENOMEM;
		goto error;
	}

	/* find all bulk endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(endpoint)) {
			es2->cport_in[bulk_in++].endpoint =
				endpoint->bEndpointAddress;
		} else if (usb_endpoint_is_bulk_out(endpoint)) {
			es2->cport_out[bulk_out++].endpoint =
				endpoint->bEndpointAddress;
		} else {
			dev_err(&udev->dev,
				"Unknown endpoint type found, address 0x%02x\n",
				endpoint->bEndpointAddress);
		}
	}
	if (bulk_in != NUM_BULKS || bulk_out != NUM_BULKS) {
		dev_err(&udev->dev, "Not enough endpoints found in device, aborting!\n");
		retval = -ENODEV;
		goto error;
	}

	/* Allocate buffers for our cport in messages */
	for (bulk_in = 0; bulk_in < NUM_BULKS; bulk_in++) {
		struct es2_cport_in *cport_in = &es2->cport_in[bulk_in];

		for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
			struct urb *urb;
			u8 *buffer;

			urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!urb) {
				retval = -ENOMEM;
				goto error;
			}
			buffer = kmalloc(ES2_GBUF_MSG_SIZE_MAX, GFP_KERNEL);
			if (!buffer) {
				retval = -ENOMEM;
				goto error;
			}

			usb_fill_bulk_urb(urb, udev,
					  usb_rcvbulkpipe(udev,
							  cport_in->endpoint),
					  buffer, ES2_GBUF_MSG_SIZE_MAX,
					  cport_in_callback, hd);
			cport_in->urb[i] = urb;
			cport_in->buffer[i] = buffer;
		}
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
							(S_IWUSR | S_IRUGO),
							gb_debugfs_get(), es2,
							&apb_log_enable_fops);

	retval = gb_hd_add(hd);
	if (retval)
		goto error;

	for (i = 0; i < NUM_BULKS; ++i) {
		retval = es2_cport_in_enable(es2, &es2->cport_in[i]);
		if (retval)
			goto err_disable_cport_in;
	}

	return 0;

err_disable_cport_in:
	for (--i; i >= 0; --i)
		es2_cport_in_disable(es2, &es2->cport_in[i]);
	gb_hd_del(hd);
error:
	es2_destroy(es2);

	return retval;
}

static void ap_disconnect(struct usb_interface *interface)
{
	struct es2_ap_dev *es2 = usb_get_intfdata(interface);
	int i;

	gb_hd_del(es2->hd);

	for (i = 0; i < NUM_BULKS; ++i)
		es2_cport_in_disable(es2, &es2->cport_in[i]);

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

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
