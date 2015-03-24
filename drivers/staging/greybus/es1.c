/*
 * Greybus "AP" USB driver
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/sizes.h>
#include <linux/usb.h>
#include <linux/kfifo.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include "greybus.h"
#include "svc_msg.h"
#include "kernel_ver.h"

/*
 * Macros for making pointers explicitly opaque, such that the result
 * isn't valid but also can't be mistaken for an ERR_PTR() value.
 */
#define conceal_urb(urb)	((void *)((uintptr_t)(urb) ^ 0xbad))
#define reveal_urb(cookie)	((void *)((uintptr_t)(cookie) ^ 0xbad))

/* Memory sizes for the buffers sent to/from the ES1 controller */
#define ES1_SVC_MSG_SIZE	(sizeof(struct svc_msg) + SZ_64K)
#define ES1_GBUF_MSG_SIZE_MAX	PAGE_SIZE

static const struct usb_device_id id_table[] = {
	/* Made up numbers for the SVC USB Bridge in ES1 */
	{ USB_DEVICE(0xffff, 0x0001) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

#define APB1_LOG_SIZE		SZ_16K
static struct dentry *apb1_log_dentry;
static struct dentry *apb1_log_enable_dentry;
static struct task_struct *apb1_log_task;
static DEFINE_KFIFO(apb1_log_fifo, char, APB1_LOG_SIZE);

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

/**
 * es1_ap_dev - ES1 USB Bridge to AP structure
 * @usb_dev: pointer to the USB device we are.
 * @usb_intf: pointer to the USB interface we are bound to.
 * @hd: pointer to our greybus_host_device structure
 * @control_endpoint: endpoint to send data to SVC
 * @svc_endpoint: endpoint for SVC data in
 * @cport_in_endpoint: bulk in endpoint for CPort data
 * @cport-out_endpoint: bulk out endpoint for CPort data
 * @svc_buffer: buffer for SVC messages coming in on @svc_endpoint
 * @svc_urb: urb for SVC messages coming in on @svc_endpoint
 * @cport_in_urb: array of urbs for the CPort in messages
 * @cport_in_buffer: array of buffers for the @cport_in_urb urbs
 * @cport_out_urb: array of urbs for the CPort out messages
 * @cport_out_urb_busy: array of flags to see if the @cport_out_urb is busy or
 *			not.
 * @cport_out_urb_lock: locks the @cport_out_urb_busy "list"
 */
struct es1_ap_dev {
	struct usb_device *usb_dev;
	struct usb_interface *usb_intf;
	struct greybus_host_device *hd;

	__u8 control_endpoint;
	__u8 svc_endpoint;
	__u8 cport_in_endpoint;
	__u8 cport_out_endpoint;

	u8 *svc_buffer;
	struct urb *svc_urb;

	struct urb *cport_in_urb[NUM_CPORT_IN_URB];
	u8 *cport_in_buffer[NUM_CPORT_IN_URB];
	struct urb *cport_out_urb[NUM_CPORT_OUT_URB];
	bool cport_out_urb_busy[NUM_CPORT_OUT_URB];
	spinlock_t cport_out_urb_lock;
};

static inline struct es1_ap_dev *hd_to_es1(struct greybus_host_device *hd)
{
	return (struct es1_ap_dev *)&hd->hd_priv;
}

static void cport_out_callback(struct urb *urb);
static void usb_log_enable(struct es1_ap_dev *es1, int enable);

/*
 * Buffer constraints for the host driver.
 *
 * A "buffer" is used to hold data to be transferred for Greybus by
 * the host driver.  A buffer is represented by a "buffer pointer",
 * which defines a region of memory used by the host driver for
 * transferring the data.  When Greybus allocates a buffer, it must
 * do so subject to the constraints associated with the host driver.
 * These constraints are specified by two parameters: the
 * headroom; and the maximum buffer size.
 *
 *			+------------------+
 *			|    Host driver   | \
 *			|   reserved area  |  }- headroom
 *			|      . . .       | /
 *  buffer pointer ---> +------------------+
 *			| Buffer space for | \
 *			| transferred data |  }- buffer size
 *			|      . . .       | /   (limited to size_max)
 *			+------------------+
 *
 *  headroom:	Every buffer must have at least this much space
 *		*before* the buffer pointer, reserved for use by the
 *		host driver.  I.e., ((char *)buffer - headroom) must
 *		point to valid memory, usable only by the host driver.
 *  size_max:	The maximum size of a buffer (not including the
 *		headroom) must not exceed this.
 */
static void hd_buffer_constraints(struct greybus_host_device *hd)
{
	/*
	 * Only one byte is required, but this produces a result
	 * that's better aligned for the user.
	 */
	hd->buffer_headroom = sizeof(u32);	/* For cport id */
	hd->buffer_size_max = ES1_GBUF_MSG_SIZE_MAX;
	BUILD_BUG_ON(hd->buffer_headroom > GB_BUFFER_HEADROOM_MAX);
}

#define ES1_TIMEOUT	500	/* 500 ms for the SVC to do something */
static int submit_svc(struct svc_msg *svc_msg, struct greybus_host_device *hd)
{
	struct es1_ap_dev *es1 = hd_to_es1(hd);
	int retval;

	/* SVC messages go down our control pipe */
	retval = usb_control_msg(es1->usb_dev,
				 usb_sndctrlpipe(es1->usb_dev,
						 es1->control_endpoint),
				 0x01,	/* vendor request AP message */
				 USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
				 0x00, 0x00,
				 (char *)svc_msg,
				 sizeof(*svc_msg),
				 ES1_TIMEOUT);
	if (retval != sizeof(*svc_msg))
		return retval;

	return 0;
}

static struct urb *next_free_urb(struct es1_ap_dev *es1, gfp_t gfp_mask)
{
	struct urb *urb = NULL;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&es1->cport_out_urb_lock, flags);

	/* Look in our pool of allocated urbs first, as that's the "fastest" */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		if (es1->cport_out_urb_busy[i] == false) {
			es1->cport_out_urb_busy[i] = true;
			urb = es1->cport_out_urb[i];
			break;
		}
	}
	spin_unlock_irqrestore(&es1->cport_out_urb_lock, flags);
	if (urb)
		return urb;

	/*
	 * Crap, pool is empty, complain to the syslog and go allocate one
	 * dynamically as we have to succeed.
	 */
	dev_err(&es1->usb_dev->dev,
		"No free CPort OUT urbs, having to dynamically allocate one!\n");
	return usb_alloc_urb(0, gfp_mask);
}

static void free_urb(struct es1_ap_dev *es1, struct urb *urb)
{
	unsigned long flags;
	int i;
	/*
	 * See if this was an urb in our pool, if so mark it "free", otherwise
	 * we need to free it ourselves.
	 */
	spin_lock_irqsave(&es1->cport_out_urb_lock, flags);
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		if (urb == es1->cport_out_urb[i]) {
			es1->cport_out_urb_busy[i] = false;
			urb = NULL;
			break;
		}
	}
	spin_unlock_irqrestore(&es1->cport_out_urb_lock, flags);

	/* If urb is not NULL, then we need to free this urb */
	usb_free_urb(urb);
}

/*
 * Returns an opaque cookie value if successful, or a pointer coded
 * error otherwise.  If the caller wishes to cancel the in-flight
 * buffer, it must supply the returned cookie to the cancel routine.
 */
static void *buffer_send(struct greybus_host_device *hd, u16 cport_id,
			void *buffer, size_t buffer_size, gfp_t gfp_mask)
{
	struct es1_ap_dev *es1 = hd_to_es1(hd);
	struct usb_device *udev = es1->usb_dev;
	u8 *transfer_buffer = buffer;
	int transfer_buffer_size;
	int retval;
	struct urb *urb;

	if (!buffer) {
		pr_err("null buffer supplied to send\n");
		return ERR_PTR(-EINVAL);
	}
	if (buffer_size > (size_t)INT_MAX) {
		pr_err("bad buffer size (%zu) supplied to send\n", buffer_size);
		return ERR_PTR(-EINVAL);
	}
	transfer_buffer--;
	transfer_buffer_size = buffer_size + 1;

	/*
	 * The data actually transferred will include an indication
	 * of where the data should be sent.  Do one last check of
	 * the target CPort id before filling it in.
	 */
	if (cport_id == CPORT_ID_BAD) {
		pr_err("request to send inbound data buffer\n");
		return ERR_PTR(-EINVAL);
	}
	if (cport_id > (u16)U8_MAX) {
		pr_err("cport_id (%hd) is out of range for ES1\n", cport_id);
		return ERR_PTR(-EINVAL);
	}
	/* OK, the destination is fine; record it in the transfer buffer */
	*transfer_buffer = cport_id;

	/* Find a free urb */
	urb = next_free_urb(es1, gfp_mask);
	if (!urb)
		return ERR_PTR(-ENOMEM);

	usb_fill_bulk_urb(urb, udev,
			  usb_sndbulkpipe(udev, es1->cport_out_endpoint),
			  transfer_buffer, transfer_buffer_size,
			  cport_out_callback, hd);
	retval = usb_submit_urb(urb, gfp_mask);
	if (retval) {
		pr_err("error %d submitting URB\n", retval);
		free_urb(es1, urb);
		return ERR_PTR(retval);
	}

	return conceal_urb(urb);
}

/*
 * The cookie value supplied is the value that buffer_send()
 * returned to its caller.  It identifies the buffer that should be
 * canceled.  This function must also handle (which is to say,
 * ignore) a null cookie value.
 */
static void buffer_cancel(void *cookie)
{

	/*
	 * We really should be defensive and track all outstanding
	 * (sent) buffers rather than trusting the cookie provided
	 * is valid.  For the time being, this will do.
	 */
	if (cookie)
		usb_kill_urb(reveal_urb(cookie));
}

static struct greybus_host_driver es1_driver = {
	.hd_priv_size		= sizeof(struct es1_ap_dev),
	.buffer_send		= buffer_send,
	.buffer_cancel		= buffer_cancel,
	.submit_svc		= submit_svc,
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

static void ap_disconnect(struct usb_interface *interface)
{
	struct es1_ap_dev *es1;
	struct usb_device *udev;
	int i;

	es1 = usb_get_intfdata(interface);
	if (!es1)
		return;

	usb_log_enable(es1, 0);

	/* Tear down everything! */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		struct urb *urb = es1->cport_out_urb[i];

		if (!urb)
			break;
		usb_kill_urb(urb);
		usb_free_urb(urb);
		es1->cport_out_urb[i] = NULL;
		es1->cport_out_urb_busy[i] = false;	/* just to be anal */
	}

	for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
		struct urb *urb = es1->cport_in_urb[i];

		if (!urb)
			break;
		usb_kill_urb(urb);
		usb_free_urb(urb);
		kfree(es1->cport_in_buffer[i]);
		es1->cport_in_buffer[i] = NULL;
	}

	usb_kill_urb(es1->svc_urb);
	usb_free_urb(es1->svc_urb);
	es1->svc_urb = NULL;
	kfree(es1->svc_buffer);
	es1->svc_buffer = NULL;

	usb_set_intfdata(interface, NULL);
	udev = es1->usb_dev;
	greybus_remove_hd(es1->hd);

	usb_put_dev(udev);
}

/* Callback for when we get a SVC message */
static void svc_in_callback(struct urb *urb)
{
	struct greybus_host_device *hd = urb->context;
	struct device *dev = &urb->dev->dev;
	int status = check_urb_status(urb);
	int retval;

	if (status) {
		if ((status == -EAGAIN) || (status == -EPROTO))
			goto exit;
		dev_err(dev, "urb svc in error %d (dropped)\n", status);
		return;
	}

	/* We have a message, create a new message structure, add it to the
	 * list, and wake up our thread that will process the messages.
	 */
	greybus_svc_in(hd, urb->transfer_buffer, urb->actual_length);

exit:
	/* resubmit the urb to get more messages */
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(dev, "Can not submit urb for AP data: %d\n", retval);
}

static void cport_in_callback(struct urb *urb)
{
	struct greybus_host_device *hd = urb->context;
	struct device *dev = &urb->dev->dev;
	int status = check_urb_status(urb);
	int retval;
	u16 cport_id;
	u8 *data;

	if (status) {
		if ((status == -EAGAIN) || (status == -EPROTO))
			goto exit;
		dev_err(dev, "urb cport in error %d (dropped)\n", status);
		return;
	}

	/* The size has to be at least one, for the cport id */
	if (!urb->actual_length) {
		dev_err(dev, "%s: no cport id in input buffer?\n", __func__);
		goto exit;
	}

	/*
	 * Our CPort number is the first byte of the data stream,
	 * the rest of the stream is "real" data
	 */
	data = urb->transfer_buffer;
	cport_id = (u16)data[0];
	data = &data[1];

	/* Pass this data to the greybus core */
	greybus_data_rcvd(hd, cport_id, data, urb->actual_length - 1);

exit:
	/* put our urb back in the request pool */
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(dev, "%s: error %d in submitting urb.\n",
			__func__, retval);
}

static void cport_out_callback(struct urb *urb)
{
	struct greybus_host_device *hd = urb->context;
	struct es1_ap_dev *es1 = hd_to_es1(hd);
	int status = check_urb_status(urb);
	u8 *data = urb->transfer_buffer + 1;

	/*
	 * Tell the submitter that the buffer send (attempt) is
	 * complete, and report the status.  The submitter's buffer
	 * starts after the one-byte CPort id we inserted.
	 */
	data = urb->transfer_buffer + 1;
	greybus_data_sent(hd, data, status);

	free_urb(es1, urb);
	/*
	 * Rest assured Greg, this craziness is getting fixed.
	 *
	 * Yes, you are right, we aren't telling anyone that the urb finished.
	 * "That's crazy!  How does this all even work?" you might be saying.
	 * The "magic" is the idea that greybus works on the "operation" level,
	 * not the "send a buffer" level.  All operations are "round-trip" with
	 * a response from the device that the operation finished, or it will
	 * time out.  Because of that, we don't care that this urb finished, or
	 * failed, or did anything else, as higher levels of the protocol stack
	 * will handle completions and timeouts and the rest.
	 *
	 * This protocol is "needed" due to some hardware restrictions on the
	 * current generation of Unipro controllers.  Think about it for a
	 * minute, this is a USB driver, talking to a Unipro bridge, impedance
	 * mismatch is huge, yet the Unipro controller are even more
	 * underpowered than this little USB controller.  We rely on the round
	 * trip to keep stalls in the Unipro controllers from happening so that
	 * we can keep data flowing properly, no matter how slow it might be.
	 *
	 * Once again, a wonderful bus protocol cut down in its prime by a naive
	 * controller chip.  We dream of the day we have a "real" HCD for
	 * Unipro.  Until then, we suck it up and make the hardware work, as
	 * that's the job of the firmware and kernel.
	 * </rant>
	 */
}

static void apb1_log_get(struct es1_ap_dev *es1)
{
	char buf[65];
	int retval;

	/* SVC messages go down our control pipe */
	do {
		memset(buf, 0, 65);
		retval = usb_control_msg(es1->usb_dev,
					usb_rcvctrlpipe(es1->usb_dev,
							es1->control_endpoint),
					0x02,	/* vendor request APB1 log */
					USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
					0x00, 0x00,
					buf,
					64,
					ES1_TIMEOUT);
		if (retval > 0)
			kfifo_in(&apb1_log_fifo, buf, retval);
	} while (retval > 0);
}

static int apb1_log_poll(void *data)
{
	while (!kthread_should_stop()) {
		msleep(1000);
		apb1_log_get((struct es1_ap_dev *)data);
	}
	return 0;
}

static ssize_t apb1_log_read(struct file *f, char __user *buf,
				size_t count, loff_t *ppos)
{
	ssize_t ret;
	size_t copied;
	char *tmp_buf;

	if (count > APB1_LOG_SIZE)
		count = APB1_LOG_SIZE;

	tmp_buf = kmalloc(count, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	copied = kfifo_out(&apb1_log_fifo, tmp_buf, count);
	ret = simple_read_from_buffer(buf, count, ppos, tmp_buf, copied);

	kfree(tmp_buf);

	return ret;
}

static const struct file_operations apb1_log_fops = {
	.read	= apb1_log_read,
};

static void usb_log_enable(struct es1_ap_dev *es1, int enable)
{
	if (enable && apb1_log_task != NULL)
		return;

	if (enable) {
		/* get log from APB1 */
		apb1_log_task = kthread_run(apb1_log_poll, es1, "apb1_log");
		if (apb1_log_task == ERR_PTR(-ENOMEM))
			return;
		apb1_log_dentry = debugfs_create_file("apb1_log", S_IRUGO,
							gb_debugfs_get(), NULL,
							&apb1_log_fops);
	} else {
		debugfs_remove(apb1_log_dentry);
		apb1_log_dentry = NULL;

		if (apb1_log_task) {
			kthread_stop(apb1_log_task);
			apb1_log_task = NULL;
		}
	}
}

static ssize_t apb1_log_enable_read(struct file *f, char __user *buf,
				size_t count, loff_t *ppos)
{
	char tmp_buf[3];
	int enable = apb1_log_task != NULL;
	sprintf(tmp_buf, "%d\n", enable);
	return simple_read_from_buffer(buf, count, ppos, tmp_buf, 3);
}

static ssize_t apb1_log_enable_write(struct file *f, const char __user *buf,
				size_t count, loff_t *ppos)
{
	int enable;
	ssize_t retval;
	struct es1_ap_dev *es1 = (struct es1_ap_dev *)f->f_inode->i_private;

	retval = kstrtoint_from_user(buf, count, 10, &enable);
	if (retval)
		return retval;

	if (enable) {
		usb_log_enable(es1, enable);
		retval = count;
	} else {
		retval = -EINVAL;
	}

	return retval;
}

static const struct file_operations apb1_log_enable_fops = {
	.read	= apb1_log_enable_read,
	.write	= apb1_log_enable_write,
};

/*
 * The ES1 USB Bridge device contains 4 endpoints
 * 1 Control - usual USB stuff + AP -> SVC messages
 * 1 Interrupt IN - SVC -> AP messages
 * 1 Bulk IN - CPort data in
 * 1 Bulk OUT - CPort data out
 */
static int ap_probe(struct usb_interface *interface,
		    const struct usb_device_id *id)
{
	struct es1_ap_dev *es1;
	struct greybus_host_device *hd;
	struct usb_device *udev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	bool int_in_found = false;
	bool bulk_in_found = false;
	bool bulk_out_found = false;
	int retval = -ENOMEM;
	int i;
	u8 svc_interval = 0;

	udev = usb_get_dev(interface_to_usbdev(interface));

	hd = greybus_create_hd(&es1_driver, &udev->dev);
	if (!hd) {
		usb_put_dev(udev);
		return -ENOMEM;
	}

	/* Fill in the buffer allocation constraints */
	hd_buffer_constraints(hd);

	es1 = hd_to_es1(hd);
	es1->hd = hd;
	es1->usb_intf = interface;
	es1->usb_dev = udev;
	spin_lock_init(&es1->cport_out_urb_lock);
	usb_set_intfdata(interface, es1);

	/* Control endpoint is the pipe to talk to this AP, so save it off */
	endpoint = &udev->ep0.desc;
	es1->control_endpoint = endpoint->bEndpointAddress;

	/* find all 3 of our endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_int_in(endpoint)) {
			es1->svc_endpoint = endpoint->bEndpointAddress;
			svc_interval = endpoint->bInterval;
			int_in_found = true;
		} else if (usb_endpoint_is_bulk_in(endpoint)) {
			es1->cport_in_endpoint = endpoint->bEndpointAddress;
			bulk_in_found = true;
		} else if (usb_endpoint_is_bulk_out(endpoint)) {
			es1->cport_out_endpoint = endpoint->bEndpointAddress;
			bulk_out_found = true;
		} else {
			dev_err(&udev->dev,
				"Unknown endpoint type found, address %x\n",
				endpoint->bEndpointAddress);
		}
	}
	if ((int_in_found == false) ||
	    (bulk_in_found == false) ||
	    (bulk_out_found == false)) {
		dev_err(&udev->dev, "Not enough endpoints found in device, aborting!\n");
		goto error;
	}

	/* Create our buffer and URB to get SVC messages, and start it up */
	es1->svc_buffer = kmalloc(ES1_SVC_MSG_SIZE, GFP_KERNEL);
	if (!es1->svc_buffer)
		goto error;

	es1->svc_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!es1->svc_urb)
		goto error;

	usb_fill_int_urb(es1->svc_urb, udev,
			 usb_rcvintpipe(udev, es1->svc_endpoint),
			 es1->svc_buffer, ES1_SVC_MSG_SIZE, svc_in_callback,
			 hd, svc_interval);

	/* Allocate buffers for our cport in messages and start them up */
	for (i = 0; i < NUM_CPORT_IN_URB; ++i) {
		struct urb *urb;
		u8 *buffer;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			goto error;
		buffer = kmalloc(ES1_GBUF_MSG_SIZE_MAX, GFP_KERNEL);
		if (!buffer)
			goto error;

		usb_fill_bulk_urb(urb, udev,
				  usb_rcvbulkpipe(udev, es1->cport_in_endpoint),
				  buffer, ES1_GBUF_MSG_SIZE_MAX,
				  cport_in_callback, hd);
		es1->cport_in_urb[i] = urb;
		es1->cport_in_buffer[i] = buffer;
		retval = usb_submit_urb(urb, GFP_KERNEL);
		if (retval)
			goto error;
	}

	/* Allocate urbs for our CPort OUT messages */
	for (i = 0; i < NUM_CPORT_OUT_URB; ++i) {
		struct urb *urb;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			goto error;

		es1->cport_out_urb[i] = urb;
		es1->cport_out_urb_busy[i] = false;	/* just to be anal */
	}

	/* Start up our svc urb, which allows events to start flowing */
	retval = usb_submit_urb(es1->svc_urb, GFP_KERNEL);
	if (retval)
		goto error;

	apb1_log_enable_dentry = debugfs_create_file("apb1_log_enable",
							(S_IWUSR | S_IRUGO),
							gb_debugfs_get(), es1,
							&apb1_log_enable_fops);

	return 0;
error:
	ap_disconnect(interface);

	return retval;
}

static struct usb_driver es1_ap_driver = {
	.name =		"es1_ap_driver",
	.probe =	ap_probe,
	.disconnect =	ap_disconnect,
	.id_table =	id_table,
};

module_usb_driver(es1_ap_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
