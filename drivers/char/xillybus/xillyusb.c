// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Xillybus Ltd, http://xillybus.com
 *
 * Driver for the XillyUSB FPGA/host framework.
 *
 * This driver interfaces with a special IP core in an FPGA, setting up
 * a pipe between a hardware FIFO in the programmable logic and a device
 * file in the host. The number of such pipes and their attributes are
 * set up on the logic. This driver detects these automatically and
 * creates the device files accordingly.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/module.h>
#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/crc32.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/usb.h>

#include "xillybus_class.h"

MODULE_DESCRIPTION("Driver for XillyUSB FPGA IP Core");
MODULE_AUTHOR("Eli Billauer, Xillybus Ltd.");
MODULE_ALIAS("xillyusb");
MODULE_LICENSE("GPL v2");

#define XILLY_RX_TIMEOUT		(10 * HZ / 1000)
#define XILLY_RESPONSE_TIMEOUT		(500 * HZ / 1000)

#define BUF_SIZE_ORDER			4
#define BUFNUM				8
#define LOG2_IDT_FIFO_SIZE		16
#define LOG2_INITIAL_FIFO_BUF_SIZE	16

#define MSG_EP_NUM			1
#define IN_EP_NUM			1

static const char xillyname[] = "xillyusb";

static unsigned int fifo_buf_order;

#define USB_VENDOR_ID_XILINX		0x03fd
#define USB_VENDOR_ID_ALTERA		0x09fb

#define USB_PRODUCT_ID_XILLYUSB		0xebbe

static const struct usb_device_id xillyusb_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_XILINX, USB_PRODUCT_ID_XILLYUSB) },
	{ USB_DEVICE(USB_VENDOR_ID_ALTERA, USB_PRODUCT_ID_XILLYUSB) },
	{ }
};

MODULE_DEVICE_TABLE(usb, xillyusb_table);

struct xillyusb_dev;

struct xillyfifo {
	unsigned int bufsize; /* In bytes, always a power of 2 */
	unsigned int bufnum;
	unsigned int size; /* Lazy: Equals bufsize * bufnum */
	unsigned int buf_order;

	int fill; /* Number of bytes in the FIFO */
	spinlock_t lock;
	wait_queue_head_t waitq;

	unsigned int readpos;
	unsigned int readbuf;
	unsigned int writepos;
	unsigned int writebuf;
	char **mem;
};

struct xillyusb_channel;

struct xillyusb_endpoint {
	struct xillyusb_dev *xdev;

	struct mutex ep_mutex; /* serialize operations on endpoint */

	struct list_head buffers;
	struct list_head filled_buffers;
	spinlock_t buffers_lock; /* protect these two lists */

	unsigned int order;
	unsigned int buffer_size;

	unsigned int fill_mask;

	int outstanding_urbs;

	struct usb_anchor anchor;

	struct xillyfifo fifo;

	struct work_struct workitem;

	bool shutting_down;
	bool drained;
	bool wake_on_drain;

	u8 ep_num;
};

struct xillyusb_channel {
	struct xillyusb_dev *xdev;

	struct xillyfifo *in_fifo;
	struct xillyusb_endpoint *out_ep;
	struct mutex lock; /* protect @out_ep, @in_fifo, bit fields below */

	struct mutex in_mutex; /* serialize fops on FPGA to host stream */
	struct mutex out_mutex; /* serialize fops on host to FPGA stream */
	wait_queue_head_t flushq;

	int chan_idx;

	u32 in_consumed_bytes;
	u32 in_current_checkpoint;
	u32 out_bytes;

	unsigned int in_log2_element_size;
	unsigned int out_log2_element_size;
	unsigned int in_log2_fifo_size;
	unsigned int out_log2_fifo_size;

	unsigned int read_data_ok; /* EOF not arrived (yet) */
	unsigned int poll_used;
	unsigned int flushing;
	unsigned int flushed;
	unsigned int canceled;

	/* Bit fields protected by @lock except for initialization */
	unsigned readable:1;
	unsigned writable:1;
	unsigned open_for_read:1;
	unsigned open_for_write:1;
	unsigned in_synchronous:1;
	unsigned out_synchronous:1;
	unsigned in_seekable:1;
	unsigned out_seekable:1;
};

struct xillybuffer {
	struct list_head entry;
	struct xillyusb_endpoint *ep;
	void *buf;
	unsigned int len;
};

struct xillyusb_dev {
	struct xillyusb_channel *channels;

	struct usb_device	*udev;
	struct device		*dev; /* For dev_err() and such */
	struct kref		kref;
	struct workqueue_struct	*workq;

	int error;
	spinlock_t error_lock; /* protect @error */
	struct work_struct wakeup_workitem;

	int num_channels;

	struct xillyusb_endpoint *msg_ep;
	struct xillyusb_endpoint *in_ep;

	struct mutex msg_mutex; /* serialize opcode transmission */
	int in_bytes_left;
	int leftover_chan_num;
	unsigned int in_counter;
	struct mutex process_in_mutex; /* synchronize wakeup_all() */
};

/* FPGA to host opcodes */
enum {
	OPCODE_DATA = 0,
	OPCODE_QUIESCE_ACK = 1,
	OPCODE_EOF = 2,
	OPCODE_REACHED_CHECKPOINT = 3,
	OPCODE_CANCELED_CHECKPOINT = 4,
};

/* Host to FPGA opcodes */
enum {
	OPCODE_QUIESCE = 0,
	OPCODE_REQ_IDT = 1,
	OPCODE_SET_CHECKPOINT = 2,
	OPCODE_CLOSE = 3,
	OPCODE_SET_PUSH = 4,
	OPCODE_UPDATE_PUSH = 5,
	OPCODE_CANCEL_CHECKPOINT = 6,
	OPCODE_SET_ADDR = 7,
};

/*
 * fifo_write() and fifo_read() are NOT reentrant (i.e. concurrent multiple
 * calls to each on the same FIFO is not allowed) however it's OK to have
 * threads calling each of the two functions once on the same FIFO, and
 * at the same time.
 */

static int fifo_write(struct xillyfifo *fifo,
		      const void *data, unsigned int len,
		      int (*copier)(void *, const void *, int))
{
	unsigned int done = 0;
	unsigned int todo = len;
	unsigned int nmax;
	unsigned int writepos = fifo->writepos;
	unsigned int writebuf = fifo->writebuf;
	unsigned long flags;
	int rc;

	nmax = fifo->size - READ_ONCE(fifo->fill);

	while (1) {
		unsigned int nrail = fifo->bufsize - writepos;
		unsigned int n = min(todo, nmax);

		if (n == 0) {
			spin_lock_irqsave(&fifo->lock, flags);
			fifo->fill += done;
			spin_unlock_irqrestore(&fifo->lock, flags);

			fifo->writepos = writepos;
			fifo->writebuf = writebuf;

			return done;
		}

		if (n > nrail)
			n = nrail;

		rc = (*copier)(fifo->mem[writebuf] + writepos, data + done, n);

		if (rc)
			return rc;

		done += n;
		todo -= n;

		writepos += n;
		nmax -= n;

		if (writepos == fifo->bufsize) {
			writepos = 0;
			writebuf++;

			if (writebuf == fifo->bufnum)
				writebuf = 0;
		}
	}
}

static int fifo_read(struct xillyfifo *fifo,
		     void *data, unsigned int len,
		     int (*copier)(void *, const void *, int))
{
	unsigned int done = 0;
	unsigned int todo = len;
	unsigned int fill;
	unsigned int readpos = fifo->readpos;
	unsigned int readbuf = fifo->readbuf;
	unsigned long flags;
	int rc;

	/*
	 * The spinlock here is necessary, because otherwise fifo->fill
	 * could have been increased by fifo_write() after writing data
	 * to the buffer, but this data would potentially not have been
	 * visible on this thread at the time the updated fifo->fill was.
	 * That could lead to reading invalid data.
	 */

	spin_lock_irqsave(&fifo->lock, flags);
	fill = fifo->fill;
	spin_unlock_irqrestore(&fifo->lock, flags);

	while (1) {
		unsigned int nrail = fifo->bufsize - readpos;
		unsigned int n = min(todo, fill);

		if (n == 0) {
			spin_lock_irqsave(&fifo->lock, flags);
			fifo->fill -= done;
			spin_unlock_irqrestore(&fifo->lock, flags);

			fifo->readpos = readpos;
			fifo->readbuf = readbuf;

			return done;
		}

		if (n > nrail)
			n = nrail;

		rc = (*copier)(data + done, fifo->mem[readbuf] + readpos, n);

		if (rc)
			return rc;

		done += n;
		todo -= n;

		readpos += n;
		fill -= n;

		if (readpos == fifo->bufsize) {
			readpos = 0;
			readbuf++;

			if (readbuf == fifo->bufnum)
				readbuf = 0;
		}
	}
}

/*
 * These three wrapper functions are used as the @copier argument to
 * fifo_write() and fifo_read(), so that they can work directly with
 * user memory as well.
 */

static int xilly_copy_from_user(void *dst, const void *src, int n)
{
	if (copy_from_user(dst, (const void __user *)src, n))
		return -EFAULT;

	return 0;
}

static int xilly_copy_to_user(void *dst, const void *src, int n)
{
	if (copy_to_user((void __user *)dst, src, n))
		return -EFAULT;

	return 0;
}

static int xilly_memcpy(void *dst, const void *src, int n)
{
	memcpy(dst, src, n);

	return 0;
}

static int fifo_init(struct xillyfifo *fifo,
		     unsigned int log2_size)
{
	unsigned int log2_bufnum;
	unsigned int buf_order;
	int i;

	unsigned int log2_fifo_buf_size;

retry:
	log2_fifo_buf_size = fifo_buf_order + PAGE_SHIFT;

	if (log2_size > log2_fifo_buf_size) {
		log2_bufnum = log2_size - log2_fifo_buf_size;
		buf_order = fifo_buf_order;
		fifo->bufsize = 1 << log2_fifo_buf_size;
	} else {
		log2_bufnum = 0;
		buf_order = (log2_size > PAGE_SHIFT) ?
			log2_size - PAGE_SHIFT : 0;
		fifo->bufsize = 1 << log2_size;
	}

	fifo->bufnum = 1 << log2_bufnum;
	fifo->size = fifo->bufnum * fifo->bufsize;
	fifo->buf_order = buf_order;

	fifo->mem = kmalloc_array(fifo->bufnum, sizeof(void *), GFP_KERNEL);

	if (!fifo->mem)
		return -ENOMEM;

	for (i = 0; i < fifo->bufnum; i++) {
		fifo->mem[i] = (void *)
			__get_free_pages(GFP_KERNEL, buf_order);

		if (!fifo->mem[i])
			goto memfail;
	}

	fifo->fill = 0;
	fifo->readpos = 0;
	fifo->readbuf = 0;
	fifo->writepos = 0;
	fifo->writebuf = 0;
	spin_lock_init(&fifo->lock);
	init_waitqueue_head(&fifo->waitq);
	return 0;

memfail:
	for (i--; i >= 0; i--)
		free_pages((unsigned long)fifo->mem[i], buf_order);

	kfree(fifo->mem);
	fifo->mem = NULL;

	if (fifo_buf_order) {
		fifo_buf_order--;
		goto retry;
	} else {
		return -ENOMEM;
	}
}

static void fifo_mem_release(struct xillyfifo *fifo)
{
	int i;

	if (!fifo->mem)
		return;

	for (i = 0; i < fifo->bufnum; i++)
		free_pages((unsigned long)fifo->mem[i], fifo->buf_order);

	kfree(fifo->mem);
}

/*
 * When endpoint_quiesce() returns, the endpoint has no URBs submitted,
 * won't accept any new URB submissions, and its related work item doesn't
 * and won't run anymore.
 */

static void endpoint_quiesce(struct xillyusb_endpoint *ep)
{
	mutex_lock(&ep->ep_mutex);
	ep->shutting_down = true;
	mutex_unlock(&ep->ep_mutex);

	usb_kill_anchored_urbs(&ep->anchor);
	cancel_work_sync(&ep->workitem);
}

/*
 * Note that endpoint_dealloc() also frees fifo memory (if allocated), even
 * though endpoint_alloc doesn't allocate that memory.
 */

static void endpoint_dealloc(struct xillyusb_endpoint *ep)
{
	struct list_head *this, *next;

	fifo_mem_release(&ep->fifo);

	/* Join @filled_buffers with @buffers to free these entries too */
	list_splice(&ep->filled_buffers, &ep->buffers);

	list_for_each_safe(this, next, &ep->buffers) {
		struct xillybuffer *xb =
			list_entry(this, struct xillybuffer, entry);

		free_pages((unsigned long)xb->buf, ep->order);
		kfree(xb);
	}

	kfree(ep);
}

static struct xillyusb_endpoint
*endpoint_alloc(struct xillyusb_dev *xdev,
		u8 ep_num,
		void (*work)(struct work_struct *),
		unsigned int order,
		int bufnum)
{
	int i;

	struct xillyusb_endpoint *ep;

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);

	if (!ep)
		return NULL;

	INIT_LIST_HEAD(&ep->buffers);
	INIT_LIST_HEAD(&ep->filled_buffers);

	spin_lock_init(&ep->buffers_lock);
	mutex_init(&ep->ep_mutex);

	init_usb_anchor(&ep->anchor);
	INIT_WORK(&ep->workitem, work);

	ep->order = order;
	ep->buffer_size =  1 << (PAGE_SHIFT + order);
	ep->outstanding_urbs = 0;
	ep->drained = true;
	ep->wake_on_drain = false;
	ep->xdev = xdev;
	ep->ep_num = ep_num;
	ep->shutting_down = false;

	for (i = 0; i < bufnum; i++) {
		struct xillybuffer *xb;
		unsigned long addr;

		xb = kzalloc(sizeof(*xb), GFP_KERNEL);

		if (!xb) {
			endpoint_dealloc(ep);
			return NULL;
		}

		addr = __get_free_pages(GFP_KERNEL, order);

		if (!addr) {
			kfree(xb);
			endpoint_dealloc(ep);
			return NULL;
		}

		xb->buf = (void *)addr;
		xb->ep = ep;
		list_add_tail(&xb->entry, &ep->buffers);
	}
	return ep;
}

static void cleanup_dev(struct kref *kref)
{
	struct xillyusb_dev *xdev =
		container_of(kref, struct xillyusb_dev, kref);

	if (xdev->in_ep)
		endpoint_dealloc(xdev->in_ep);

	if (xdev->msg_ep)
		endpoint_dealloc(xdev->msg_ep);

	if (xdev->workq)
		destroy_workqueue(xdev->workq);

	kfree(xdev->channels); /* Argument may be NULL, and that's fine */
	kfree(xdev);
}

/*
 * @process_in_mutex is taken to ensure that bulk_in_work() won't call
 * process_bulk_in() after wakeup_all()'s execution: The latter zeroes all
 * @read_data_ok entries, which will make process_bulk_in() report false
 * errors if executed. The mechanism relies on that xdev->error is assigned
 * a non-zero value by report_io_error() prior to queueing wakeup_all(),
 * which prevents bulk_in_work() from calling process_bulk_in().
 *
 * The fact that wakeup_all() and bulk_in_work() are queued on the same
 * workqueue makes their concurrent execution very unlikely, however the
 * kernel's API doesn't seem to ensure this strictly.
 */

static void wakeup_all(struct work_struct *work)
{
	int i;
	struct xillyusb_dev *xdev = container_of(work, struct xillyusb_dev,
						 wakeup_workitem);

	mutex_lock(&xdev->process_in_mutex);

	for (i = 0; i < xdev->num_channels; i++) {
		struct xillyusb_channel *chan = &xdev->channels[i];

		mutex_lock(&chan->lock);

		if (chan->in_fifo) {
			/*
			 * Fake an EOF: Even if such arrives, it won't be
			 * processed.
			 */
			chan->read_data_ok = 0;
			wake_up_interruptible(&chan->in_fifo->waitq);
		}

		if (chan->out_ep)
			wake_up_interruptible(&chan->out_ep->fifo.waitq);

		mutex_unlock(&chan->lock);

		wake_up_interruptible(&chan->flushq);
	}

	mutex_unlock(&xdev->process_in_mutex);

	wake_up_interruptible(&xdev->msg_ep->fifo.waitq);

	kref_put(&xdev->kref, cleanup_dev);
}

static void report_io_error(struct xillyusb_dev *xdev,
			    int errcode)
{
	unsigned long flags;
	bool do_once = false;

	spin_lock_irqsave(&xdev->error_lock, flags);
	if (!xdev->error) {
		xdev->error = errcode;
		do_once = true;
	}
	spin_unlock_irqrestore(&xdev->error_lock, flags);

	if (do_once) {
		kref_get(&xdev->kref); /* xdev is used by work item */
		queue_work(xdev->workq, &xdev->wakeup_workitem);
	}
}

/*
 * safely_assign_in_fifo() changes the value of chan->in_fifo and ensures
 * the previous pointer is never used after its return.
 */

static void safely_assign_in_fifo(struct xillyusb_channel *chan,
				  struct xillyfifo *fifo)
{
	mutex_lock(&chan->lock);
	chan->in_fifo = fifo;
	mutex_unlock(&chan->lock);

	flush_work(&chan->xdev->in_ep->workitem);
}

static void bulk_in_completer(struct urb *urb)
{
	struct xillybuffer *xb = urb->context;
	struct xillyusb_endpoint *ep = xb->ep;
	unsigned long flags;

	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		      urb->status == -ECONNRESET ||
		      urb->status == -ESHUTDOWN))
			report_io_error(ep->xdev, -EIO);

		spin_lock_irqsave(&ep->buffers_lock, flags);
		list_add_tail(&xb->entry, &ep->buffers);
		ep->outstanding_urbs--;
		spin_unlock_irqrestore(&ep->buffers_lock, flags);

		return;
	}

	xb->len = urb->actual_length;

	spin_lock_irqsave(&ep->buffers_lock, flags);
	list_add_tail(&xb->entry, &ep->filled_buffers);
	spin_unlock_irqrestore(&ep->buffers_lock, flags);

	if (!ep->shutting_down)
		queue_work(ep->xdev->workq, &ep->workitem);
}

static void bulk_out_completer(struct urb *urb)
{
	struct xillybuffer *xb = urb->context;
	struct xillyusb_endpoint *ep = xb->ep;
	unsigned long flags;

	if (urb->status &&
	    (!(urb->status == -ENOENT ||
	       urb->status == -ECONNRESET ||
	       urb->status == -ESHUTDOWN)))
		report_io_error(ep->xdev, -EIO);

	spin_lock_irqsave(&ep->buffers_lock, flags);
	list_add_tail(&xb->entry, &ep->buffers);
	ep->outstanding_urbs--;
	spin_unlock_irqrestore(&ep->buffers_lock, flags);

	if (!ep->shutting_down)
		queue_work(ep->xdev->workq, &ep->workitem);
}

static void try_queue_bulk_in(struct xillyusb_endpoint *ep)
{
	struct xillyusb_dev *xdev = ep->xdev;
	struct xillybuffer *xb;
	struct urb *urb;

	int rc;
	unsigned long flags;
	unsigned int bufsize = ep->buffer_size;

	mutex_lock(&ep->ep_mutex);

	if (ep->shutting_down || xdev->error)
		goto done;

	while (1) {
		spin_lock_irqsave(&ep->buffers_lock, flags);

		if (list_empty(&ep->buffers)) {
			spin_unlock_irqrestore(&ep->buffers_lock, flags);
			goto done;
		}

		xb = list_first_entry(&ep->buffers, struct xillybuffer, entry);
		list_del(&xb->entry);
		ep->outstanding_urbs++;

		spin_unlock_irqrestore(&ep->buffers_lock, flags);

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			report_io_error(xdev, -ENOMEM);
			goto relist;
		}

		usb_fill_bulk_urb(urb, xdev->udev,
				  usb_rcvbulkpipe(xdev->udev, ep->ep_num),
				  xb->buf, bufsize, bulk_in_completer, xb);

		usb_anchor_urb(urb, &ep->anchor);

		rc = usb_submit_urb(urb, GFP_KERNEL);

		if (rc) {
			report_io_error(xdev, (rc == -ENOMEM) ? -ENOMEM :
					-EIO);
			goto unanchor;
		}

		usb_free_urb(urb); /* This just decrements reference count */
	}

unanchor:
	usb_unanchor_urb(urb);
	usb_free_urb(urb);

relist:
	spin_lock_irqsave(&ep->buffers_lock, flags);
	list_add_tail(&xb->entry, &ep->buffers);
	ep->outstanding_urbs--;
	spin_unlock_irqrestore(&ep->buffers_lock, flags);

done:
	mutex_unlock(&ep->ep_mutex);
}

static void try_queue_bulk_out(struct xillyusb_endpoint *ep)
{
	struct xillyfifo *fifo = &ep->fifo;
	struct xillyusb_dev *xdev = ep->xdev;
	struct xillybuffer *xb;
	struct urb *urb;

	int rc;
	unsigned int fill;
	unsigned long flags;
	bool do_wake = false;

	mutex_lock(&ep->ep_mutex);

	if (ep->shutting_down || xdev->error)
		goto done;

	fill = READ_ONCE(fifo->fill) & ep->fill_mask;

	while (1) {
		int count;
		unsigned int max_read;

		spin_lock_irqsave(&ep->buffers_lock, flags);

		/*
		 * Race conditions might have the FIFO filled while the
		 * endpoint is marked as drained here. That doesn't matter,
		 * because the sole purpose of @drained is to ensure that
		 * certain data has been sent on the USB channel before
		 * shutting it down. Hence knowing that the FIFO appears
		 * to be empty with no outstanding URBs at some moment
		 * is good enough.
		 */

		if (!fill) {
			ep->drained = !ep->outstanding_urbs;
			if (ep->drained && ep->wake_on_drain)
				do_wake = true;

			spin_unlock_irqrestore(&ep->buffers_lock, flags);
			goto done;
		}

		ep->drained = false;

		if ((fill < ep->buffer_size && ep->outstanding_urbs) ||
		    list_empty(&ep->buffers)) {
			spin_unlock_irqrestore(&ep->buffers_lock, flags);
			goto done;
		}

		xb = list_first_entry(&ep->buffers, struct xillybuffer, entry);
		list_del(&xb->entry);
		ep->outstanding_urbs++;

		spin_unlock_irqrestore(&ep->buffers_lock, flags);

		max_read = min(fill, ep->buffer_size);

		count = fifo_read(&ep->fifo, xb->buf, max_read, xilly_memcpy);

		/*
		 * xilly_memcpy always returns 0 => fifo_read can't fail =>
		 * count > 0
		 */

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			report_io_error(xdev, -ENOMEM);
			goto relist;
		}

		usb_fill_bulk_urb(urb, xdev->udev,
				  usb_sndbulkpipe(xdev->udev, ep->ep_num),
				  xb->buf, count, bulk_out_completer, xb);

		usb_anchor_urb(urb, &ep->anchor);

		rc = usb_submit_urb(urb, GFP_KERNEL);

		if (rc) {
			report_io_error(xdev, (rc == -ENOMEM) ? -ENOMEM :
					-EIO);
			goto unanchor;
		}

		usb_free_urb(urb); /* This just decrements reference count */

		fill -= count;
		do_wake = true;
	}

unanchor:
	usb_unanchor_urb(urb);
	usb_free_urb(urb);

relist:
	spin_lock_irqsave(&ep->buffers_lock, flags);
	list_add_tail(&xb->entry, &ep->buffers);
	ep->outstanding_urbs--;
	spin_unlock_irqrestore(&ep->buffers_lock, flags);

done:
	mutex_unlock(&ep->ep_mutex);

	if (do_wake)
		wake_up_interruptible(&fifo->waitq);
}

static void bulk_out_work(struct work_struct *work)
{
	struct xillyusb_endpoint *ep = container_of(work,
						    struct xillyusb_endpoint,
						    workitem);
	try_queue_bulk_out(ep);
}

static int process_in_opcode(struct xillyusb_dev *xdev,
			     int opcode,
			     int chan_num)
{
	struct xillyusb_channel *chan;
	struct device *dev = xdev->dev;
	int chan_idx = chan_num >> 1;

	if (chan_idx >= xdev->num_channels) {
		dev_err(dev, "Received illegal channel ID %d from FPGA\n",
			chan_num);
		return -EIO;
	}

	chan = &xdev->channels[chan_idx];

	switch (opcode) {
	case OPCODE_EOF:
		if (!chan->read_data_ok) {
			dev_err(dev, "Received unexpected EOF for channel %d\n",
				chan_num);
			return -EIO;
		}

		/*
		 * A write memory barrier ensures that the FIFO's fill level
		 * is visible before read_data_ok turns zero, so the data in
		 * the FIFO isn't missed by the consumer.
		 */
		smp_wmb();
		WRITE_ONCE(chan->read_data_ok, 0);
		wake_up_interruptible(&chan->in_fifo->waitq);
		break;

	case OPCODE_REACHED_CHECKPOINT:
		chan->flushing = 0;
		wake_up_interruptible(&chan->flushq);
		break;

	case OPCODE_CANCELED_CHECKPOINT:
		chan->canceled = 1;
		wake_up_interruptible(&chan->flushq);
		break;

	default:
		dev_err(dev, "Received illegal opcode %d from FPGA\n",
			opcode);
		return -EIO;
	}

	return 0;
}

static int process_bulk_in(struct xillybuffer *xb)
{
	struct xillyusb_endpoint *ep = xb->ep;
	struct xillyusb_dev *xdev = ep->xdev;
	struct device *dev = xdev->dev;
	int dws = xb->len >> 2;
	__le32 *p = xb->buf;
	u32 ctrlword;
	struct xillyusb_channel *chan;
	struct xillyfifo *fifo;
	int chan_num = 0, opcode;
	int chan_idx;
	int bytes, count, dwconsume;
	int in_bytes_left = 0;
	int rc;

	if ((dws << 2) != xb->len) {
		dev_err(dev, "Received BULK IN transfer with %d bytes, not a multiple of 4\n",
			xb->len);
		return -EIO;
	}

	if (xdev->in_bytes_left) {
		bytes = min(xdev->in_bytes_left, dws << 2);
		in_bytes_left = xdev->in_bytes_left - bytes;
		chan_num = xdev->leftover_chan_num;
		goto resume_leftovers;
	}

	while (dws) {
		ctrlword = le32_to_cpu(*p++);
		dws--;

		chan_num = ctrlword & 0xfff;
		count = (ctrlword >> 12) & 0x3ff;
		opcode = (ctrlword >> 24) & 0xf;

		if (opcode != OPCODE_DATA) {
			unsigned int in_counter = xdev->in_counter++ & 0x3ff;

			if (count != in_counter) {
				dev_err(dev, "Expected opcode counter %d, got %d\n",
					in_counter, count);
				return -EIO;
			}

			rc = process_in_opcode(xdev, opcode, chan_num);

			if (rc)
				return rc;

			continue;
		}

		bytes = min(count + 1, dws << 2);
		in_bytes_left = count + 1 - bytes;

resume_leftovers:
		chan_idx = chan_num >> 1;

		if (!(chan_num & 1) || chan_idx >= xdev->num_channels ||
		    !xdev->channels[chan_idx].read_data_ok) {
			dev_err(dev, "Received illegal channel ID %d from FPGA\n",
				chan_num);
			return -EIO;
		}
		chan = &xdev->channels[chan_idx];

		fifo = chan->in_fifo;

		if (unlikely(!fifo))
			return -EIO; /* We got really unexpected data */

		if (bytes != fifo_write(fifo, p, bytes, xilly_memcpy)) {
			dev_err(dev, "Misbehaving FPGA overflowed an upstream FIFO!\n");
			return -EIO;
		}

		wake_up_interruptible(&fifo->waitq);

		dwconsume = (bytes + 3) >> 2;
		dws -= dwconsume;
		p += dwconsume;
	}

	xdev->in_bytes_left = in_bytes_left;
	xdev->leftover_chan_num = chan_num;
	return 0;
}

static void bulk_in_work(struct work_struct *work)
{
	struct xillyusb_endpoint *ep =
		container_of(work, struct xillyusb_endpoint, workitem);
	struct xillyusb_dev *xdev = ep->xdev;
	unsigned long flags;
	struct xillybuffer *xb;
	bool consumed = false;
	int rc = 0;

	mutex_lock(&xdev->process_in_mutex);

	spin_lock_irqsave(&ep->buffers_lock, flags);

	while (1) {
		if (rc || list_empty(&ep->filled_buffers)) {
			spin_unlock_irqrestore(&ep->buffers_lock, flags);
			mutex_unlock(&xdev->process_in_mutex);

			if (rc)
				report_io_error(xdev, rc);
			else if (consumed)
				try_queue_bulk_in(ep);

			return;
		}

		xb = list_first_entry(&ep->filled_buffers, struct xillybuffer,
				      entry);
		list_del(&xb->entry);

		spin_unlock_irqrestore(&ep->buffers_lock, flags);

		consumed = true;

		if (!xdev->error)
			rc = process_bulk_in(xb);

		spin_lock_irqsave(&ep->buffers_lock, flags);
		list_add_tail(&xb->entry, &ep->buffers);
		ep->outstanding_urbs--;
	}
}

static int xillyusb_send_opcode(struct xillyusb_dev *xdev,
				int chan_num, char opcode, u32 data)
{
	struct xillyusb_endpoint *ep = xdev->msg_ep;
	struct xillyfifo *fifo = &ep->fifo;
	__le32 msg[2];

	int rc = 0;

	msg[0] = cpu_to_le32((chan_num & 0xfff) |
			     ((opcode & 0xf) << 24));
	msg[1] = cpu_to_le32(data);

	mutex_lock(&xdev->msg_mutex);

	/*
	 * The wait queue is woken with the interruptible variant, so the
	 * wait function matches, however returning because of an interrupt
	 * will mess things up considerably, in particular when the caller is
	 * the release method. And the xdev->error part prevents being stuck
	 * forever in the event of a bizarre hardware bug: Pull the USB plug.
	 */

	while (wait_event_interruptible(fifo->waitq,
					fifo->fill <= (fifo->size - 8) ||
					xdev->error))
		; /* Empty loop */

	if (xdev->error) {
		rc = xdev->error;
		goto unlock_done;
	}

	fifo_write(fifo, (void *)msg, 8, xilly_memcpy);

	try_queue_bulk_out(ep);

unlock_done:
	mutex_unlock(&xdev->msg_mutex);

	return rc;
}

/*
 * Note that flush_downstream() merely waits for the data to arrive to
 * the application logic at the FPGA -- unlike PCIe Xillybus' counterpart,
 * it does nothing to make it happen (and neither is it necessary).
 *
 * This function is not reentrant for the same @chan, but this is covered
 * by the fact that for any given @chan, it's called either by the open,
 * write, llseek and flush fops methods, which can't run in parallel (and the
 * write + flush and llseek method handlers are protected with out_mutex).
 *
 * chan->flushed is there to avoid multiple flushes at the same position,
 * in particular as a result of programs that close the file descriptor
 * e.g. after a dup2() for redirection.
 */

static int flush_downstream(struct xillyusb_channel *chan,
			    long timeout,
			    bool interruptible)
{
	struct xillyusb_dev *xdev = chan->xdev;
	int chan_num = chan->chan_idx << 1;
	long deadline, left_to_sleep;
	int rc;

	if (chan->flushed)
		return 0;

	deadline = jiffies + 1 + timeout;

	if (chan->flushing) {
		long cancel_deadline = jiffies + 1 + XILLY_RESPONSE_TIMEOUT;

		chan->canceled = 0;
		rc = xillyusb_send_opcode(xdev, chan_num,
					  OPCODE_CANCEL_CHECKPOINT, 0);

		if (rc)
			return rc; /* Only real error, never -EINTR */

		/* Ignoring interrupts. Cancellation must be handled */
		while (!chan->canceled) {
			left_to_sleep = cancel_deadline - ((long)jiffies);

			if (left_to_sleep <= 0) {
				report_io_error(xdev, -EIO);
				return -EIO;
			}

			rc = wait_event_interruptible_timeout(chan->flushq,
							      chan->canceled ||
							      xdev->error,
							      left_to_sleep);

			if (xdev->error)
				return xdev->error;
		}
	}

	chan->flushing = 1;

	/*
	 * The checkpoint is given in terms of data elements, not bytes. As
	 * a result, if less than an element's worth of data is stored in the
	 * FIFO, it's not flushed, including the flush before closing, which
	 * means that such data is lost. This is consistent with PCIe Xillybus.
	 */

	rc = xillyusb_send_opcode(xdev, chan_num,
				  OPCODE_SET_CHECKPOINT,
				  chan->out_bytes >>
				  chan->out_log2_element_size);

	if (rc)
		return rc; /* Only real error, never -EINTR */

	if (!timeout) {
		while (chan->flushing) {
			rc = wait_event_interruptible(chan->flushq,
						      !chan->flushing ||
						      xdev->error);
			if (xdev->error)
				return xdev->error;

			if (interruptible && rc)
				return -EINTR;
		}

		goto done;
	}

	while (chan->flushing) {
		left_to_sleep = deadline - ((long)jiffies);

		if (left_to_sleep <= 0)
			return -ETIMEDOUT;

		rc = wait_event_interruptible_timeout(chan->flushq,
						      !chan->flushing ||
						      xdev->error,
						      left_to_sleep);

		if (xdev->error)
			return xdev->error;

		if (interruptible && rc < 0)
			return -EINTR;
	}

done:
	chan->flushed = 1;
	return 0;
}

/* request_read_anything(): Ask the FPGA for any little amount of data */
static int request_read_anything(struct xillyusb_channel *chan,
				 char opcode)
{
	struct xillyusb_dev *xdev = chan->xdev;
	unsigned int sh = chan->in_log2_element_size;
	int chan_num = (chan->chan_idx << 1) | 1;
	u32 mercy = chan->in_consumed_bytes + (2 << sh) - 1;

	return xillyusb_send_opcode(xdev, chan_num, opcode, mercy >> sh);
}

static int xillyusb_open(struct inode *inode, struct file *filp)
{
	struct xillyusb_dev *xdev;
	struct xillyusb_channel *chan;
	struct xillyfifo *in_fifo = NULL;
	struct xillyusb_endpoint *out_ep = NULL;
	int rc;
	int index;

	rc = xillybus_find_inode(inode, (void **)&xdev, &index);
	if (rc)
		return rc;

	chan = &xdev->channels[index];
	filp->private_data = chan;

	mutex_lock(&chan->lock);

	rc = -ENODEV;

	if (xdev->error)
		goto unmutex_fail;

	if (((filp->f_mode & FMODE_READ) && !chan->readable) ||
	    ((filp->f_mode & FMODE_WRITE) && !chan->writable))
		goto unmutex_fail;

	if ((filp->f_flags & O_NONBLOCK) && (filp->f_mode & FMODE_READ) &&
	    chan->in_synchronous) {
		dev_err(xdev->dev,
			"open() failed: O_NONBLOCK not allowed for read on this device\n");
		goto unmutex_fail;
	}

	if ((filp->f_flags & O_NONBLOCK) && (filp->f_mode & FMODE_WRITE) &&
	    chan->out_synchronous) {
		dev_err(xdev->dev,
			"open() failed: O_NONBLOCK not allowed for write on this device\n");
		goto unmutex_fail;
	}

	rc = -EBUSY;

	if (((filp->f_mode & FMODE_READ) && chan->open_for_read) ||
	    ((filp->f_mode & FMODE_WRITE) && chan->open_for_write))
		goto unmutex_fail;

	kref_get(&xdev->kref);

	if (filp->f_mode & FMODE_READ)
		chan->open_for_read = 1;

	if (filp->f_mode & FMODE_WRITE)
		chan->open_for_write = 1;

	mutex_unlock(&chan->lock);

	if (filp->f_mode & FMODE_WRITE) {
		out_ep = endpoint_alloc(xdev,
					(chan->chan_idx + 2) | USB_DIR_OUT,
					bulk_out_work, BUF_SIZE_ORDER, BUFNUM);

		if (!out_ep) {
			rc = -ENOMEM;
			goto unopen;
		}

		rc = fifo_init(&out_ep->fifo, chan->out_log2_fifo_size);

		if (rc)
			goto late_unopen;

		out_ep->fill_mask = -(1 << chan->out_log2_element_size);
		chan->out_bytes = 0;
		chan->flushed = 0;

		/*
		 * Sending a flush request to a previously closed stream
		 * effectively opens it, and also waits until the command is
		 * confirmed by the FPGA. The latter is necessary because the
		 * data is sent through a separate BULK OUT endpoint, and the
		 * xHCI controller is free to reorder transmissions.
		 *
		 * This can't go wrong unless there's a serious hardware error
		 * (or the computer is stuck for 500 ms?)
		 */
		rc = flush_downstream(chan, XILLY_RESPONSE_TIMEOUT, false);

		if (rc == -ETIMEDOUT) {
			rc = -EIO;
			report_io_error(xdev, rc);
		}

		if (rc)
			goto late_unopen;
	}

	if (filp->f_mode & FMODE_READ) {
		in_fifo = kzalloc(sizeof(*in_fifo), GFP_KERNEL);

		if (!in_fifo) {
			rc = -ENOMEM;
			goto late_unopen;
		}

		rc = fifo_init(in_fifo, chan->in_log2_fifo_size);

		if (rc) {
			kfree(in_fifo);
			goto late_unopen;
		}
	}

	mutex_lock(&chan->lock);
	if (in_fifo) {
		chan->in_fifo = in_fifo;
		chan->read_data_ok = 1;
	}
	if (out_ep)
		chan->out_ep = out_ep;
	mutex_unlock(&chan->lock);

	if (in_fifo) {
		u32 in_checkpoint = 0;

		if (!chan->in_synchronous)
			in_checkpoint = in_fifo->size >>
				chan->in_log2_element_size;

		chan->in_consumed_bytes = 0;
		chan->poll_used = 0;
		chan->in_current_checkpoint = in_checkpoint;
		rc = xillyusb_send_opcode(xdev, (chan->chan_idx << 1) | 1,
					  OPCODE_SET_CHECKPOINT,
					  in_checkpoint);

		if (rc) /* Failure guarantees that opcode wasn't sent */
			goto unfifo;

		/*
		 * In non-blocking mode, request the FPGA to send any data it
		 * has right away. Otherwise, the first read() will always
		 * return -EAGAIN, which is OK strictly speaking, but ugly.
		 * Checking and unrolling if this fails isn't worth the
		 * effort -- the error is propagated to the first read()
		 * anyhow.
		 */
		if (filp->f_flags & O_NONBLOCK)
			request_read_anything(chan, OPCODE_SET_PUSH);
	}

	return 0;

unfifo:
	chan->read_data_ok = 0;
	safely_assign_in_fifo(chan, NULL);
	fifo_mem_release(in_fifo);
	kfree(in_fifo);

	if (out_ep) {
		mutex_lock(&chan->lock);
		chan->out_ep = NULL;
		mutex_unlock(&chan->lock);
	}

late_unopen:
	if (out_ep)
		endpoint_dealloc(out_ep);

unopen:
	mutex_lock(&chan->lock);

	if (filp->f_mode & FMODE_READ)
		chan->open_for_read = 0;

	if (filp->f_mode & FMODE_WRITE)
		chan->open_for_write = 0;

	mutex_unlock(&chan->lock);

	kref_put(&xdev->kref, cleanup_dev);

	return rc;

unmutex_fail:
	mutex_unlock(&chan->lock);
	return rc;
}

static ssize_t xillyusb_read(struct file *filp, char __user *userbuf,
			     size_t count, loff_t *f_pos)
{
	struct xillyusb_channel *chan = filp->private_data;
	struct xillyusb_dev *xdev = chan->xdev;
	struct xillyfifo *fifo = chan->in_fifo;
	int chan_num = (chan->chan_idx << 1) | 1;

	long deadline, left_to_sleep;
	int bytes_done = 0;
	bool sent_set_push = false;
	int rc;

	deadline = jiffies + 1 + XILLY_RX_TIMEOUT;

	rc = mutex_lock_interruptible(&chan->in_mutex);

	if (rc)
		return rc;

	while (1) {
		u32 fifo_checkpoint_bytes, complete_checkpoint_bytes;
		u32 complete_checkpoint, fifo_checkpoint;
		u32 checkpoint;
		s32 diff, leap;
		unsigned int sh = chan->in_log2_element_size;
		bool checkpoint_for_complete;

		rc = fifo_read(fifo, (__force void *)userbuf + bytes_done,
			       count - bytes_done, xilly_copy_to_user);

		if (rc < 0)
			break;

		bytes_done += rc;
		chan->in_consumed_bytes += rc;

		left_to_sleep = deadline - ((long)jiffies);

		/*
		 * Some 32-bit arithmetic that may wrap. Note that
		 * complete_checkpoint is rounded up to the closest element
		 * boundary, because the read() can't be completed otherwise.
		 * fifo_checkpoint_bytes is rounded down, because it protects
		 * in_fifo from overflowing.
		 */

		fifo_checkpoint_bytes = chan->in_consumed_bytes + fifo->size;
		complete_checkpoint_bytes =
			chan->in_consumed_bytes + count - bytes_done;

		fifo_checkpoint = fifo_checkpoint_bytes >> sh;
		complete_checkpoint =
			(complete_checkpoint_bytes + (1 << sh) - 1) >> sh;

		diff = (fifo_checkpoint - complete_checkpoint) << sh;

		if (chan->in_synchronous && diff >= 0) {
			checkpoint = complete_checkpoint;
			checkpoint_for_complete = true;
		} else {
			checkpoint = fifo_checkpoint;
			checkpoint_for_complete = false;
		}

		leap = (checkpoint - chan->in_current_checkpoint) << sh;

		/*
		 * To prevent flooding of OPCODE_SET_CHECKPOINT commands as
		 * data is consumed, it's issued only if it moves the
		 * checkpoint by at least an 8th of the FIFO's size, or if
		 * it's necessary to complete the number of bytes requested by
		 * the read() call.
		 *
		 * chan->read_data_ok is checked to spare an unnecessary
		 * submission after receiving EOF, however it's harmless if
		 * such slips away.
		 */

		if (chan->read_data_ok &&
		    (leap > (fifo->size >> 3) ||
		     (checkpoint_for_complete && leap > 0))) {
			chan->in_current_checkpoint = checkpoint;
			rc = xillyusb_send_opcode(xdev, chan_num,
						  OPCODE_SET_CHECKPOINT,
						  checkpoint);

			if (rc)
				break;
		}

		if (bytes_done == count ||
		    (left_to_sleep <= 0 && bytes_done))
			break;

		/*
		 * Reaching here means that the FIFO was empty when
		 * fifo_read() returned, but not necessarily right now. Error
		 * and EOF are checked and reported only now, so that no data
		 * that managed its way to the FIFO is lost.
		 */

		if (!READ_ONCE(chan->read_data_ok)) { /* FPGA has sent EOF */
			/* Has data slipped into the FIFO since fifo_read()? */
			smp_rmb();
			if (READ_ONCE(fifo->fill))
				continue;

			rc = 0;
			break;
		}

		if (xdev->error) {
			rc = xdev->error;
			break;
		}

		if (filp->f_flags & O_NONBLOCK) {
			rc = -EAGAIN;
			break;
		}

		if (!sent_set_push) {
			rc = xillyusb_send_opcode(xdev, chan_num,
						  OPCODE_SET_PUSH,
						  complete_checkpoint);

			if (rc)
				break;

			sent_set_push = true;
		}

		if (left_to_sleep > 0) {
			/*
			 * Note that when xdev->error is set (e.g. when the
			 * device is unplugged), read_data_ok turns zero and
			 * fifo->waitq is awaken.
			 * Therefore no special attention to xdev->error.
			 */

			rc = wait_event_interruptible_timeout
				(fifo->waitq,
				 fifo->fill || !chan->read_data_ok,
				 left_to_sleep);
		} else { /* bytes_done == 0 */
			/* Tell FPGA to send anything it has */
			rc = request_read_anything(chan, OPCODE_UPDATE_PUSH);

			if (rc)
				break;

			rc = wait_event_interruptible
				(fifo->waitq,
				 fifo->fill || !chan->read_data_ok);
		}

		if (rc < 0) {
			rc = -EINTR;
			break;
		}
	}

	if (((filp->f_flags & O_NONBLOCK) || chan->poll_used) &&
	    !READ_ONCE(fifo->fill))
		request_read_anything(chan, OPCODE_SET_PUSH);

	mutex_unlock(&chan->in_mutex);

	if (bytes_done)
		return bytes_done;

	return rc;
}

static int xillyusb_flush(struct file *filp, fl_owner_t id)
{
	struct xillyusb_channel *chan = filp->private_data;
	int rc;

	if (!(filp->f_mode & FMODE_WRITE))
		return 0;

	rc = mutex_lock_interruptible(&chan->out_mutex);

	if (rc)
		return rc;

	/*
	 * One second's timeout on flushing. Interrupts are ignored, because if
	 * the user pressed CTRL-C, that interrupt will still be in flight by
	 * the time we reach here, and the opportunity to flush is lost.
	 */
	rc = flush_downstream(chan, HZ, false);

	mutex_unlock(&chan->out_mutex);

	if (rc == -ETIMEDOUT) {
		/* The things you do to use dev_warn() and not pr_warn() */
		struct xillyusb_dev *xdev = chan->xdev;

		mutex_lock(&chan->lock);
		if (!xdev->error)
			dev_warn(xdev->dev,
				 "Timed out while flushing. Output data may be lost.\n");
		mutex_unlock(&chan->lock);
	}

	return rc;
}

static ssize_t xillyusb_write(struct file *filp, const char __user *userbuf,
			      size_t count, loff_t *f_pos)
{
	struct xillyusb_channel *chan = filp->private_data;
	struct xillyusb_dev *xdev = chan->xdev;
	struct xillyfifo *fifo = &chan->out_ep->fifo;
	int rc;

	rc = mutex_lock_interruptible(&chan->out_mutex);

	if (rc)
		return rc;

	while (1) {
		if (xdev->error) {
			rc = xdev->error;
			break;
		}

		if (count == 0)
			break;

		rc = fifo_write(fifo, (__force void *)userbuf, count,
				xilly_copy_from_user);

		if (rc != 0)
			break;

		if (filp->f_flags & O_NONBLOCK) {
			rc = -EAGAIN;
			break;
		}

		if (wait_event_interruptible
		    (fifo->waitq,
		     fifo->fill != fifo->size || xdev->error)) {
			rc = -EINTR;
			break;
		}
	}

	if (rc < 0)
		goto done;

	chan->out_bytes += rc;

	if (rc) {
		try_queue_bulk_out(chan->out_ep);
		chan->flushed = 0;
	}

	if (chan->out_synchronous) {
		int flush_rc = flush_downstream(chan, 0, true);

		if (flush_rc && !rc)
			rc = flush_rc;
	}

done:
	mutex_unlock(&chan->out_mutex);

	return rc;
}

static int xillyusb_release(struct inode *inode, struct file *filp)
{
	struct xillyusb_channel *chan = filp->private_data;
	struct xillyusb_dev *xdev = chan->xdev;
	int rc_read = 0, rc_write = 0;

	if (filp->f_mode & FMODE_READ) {
		struct xillyfifo *in_fifo = chan->in_fifo;

		rc_read = xillyusb_send_opcode(xdev, (chan->chan_idx << 1) | 1,
					       OPCODE_CLOSE, 0);
		/*
		 * If rc_read is nonzero, xdev->error indicates a global
		 * device error. The error is reported later, so that
		 * resources are freed.
		 *
		 * Looping on wait_event_interruptible() kinda breaks the idea
		 * of being interruptible, and this should have been
		 * wait_event(). Only it's being waken with
		 * wake_up_interruptible() for the sake of other uses. If
		 * there's a global device error, chan->read_data_ok is
		 * deasserted and the wait queue is awaken, so this is covered.
		 */

		while (wait_event_interruptible(in_fifo->waitq,
						!chan->read_data_ok))
			; /* Empty loop */

		safely_assign_in_fifo(chan, NULL);
		fifo_mem_release(in_fifo);
		kfree(in_fifo);

		mutex_lock(&chan->lock);
		chan->open_for_read = 0;
		mutex_unlock(&chan->lock);
	}

	if (filp->f_mode & FMODE_WRITE) {
		struct xillyusb_endpoint *ep = chan->out_ep;
		/*
		 * chan->flushing isn't zeroed. If the pre-release flush timed
		 * out, a cancel request will be sent before the next
		 * OPCODE_SET_CHECKPOINT (i.e. when the file is opened again).
		 * This is despite that the FPGA forgets about the checkpoint
		 * request as the file closes. Still, in an exceptional race
		 * condition, the FPGA could send an OPCODE_REACHED_CHECKPOINT
		 * just before closing that would reach the host after the
		 * file has re-opened.
		 */

		mutex_lock(&chan->lock);
		chan->out_ep = NULL;
		mutex_unlock(&chan->lock);

		endpoint_quiesce(ep);
		endpoint_dealloc(ep);

		/* See comments on rc_read above */
		rc_write = xillyusb_send_opcode(xdev, chan->chan_idx << 1,
						OPCODE_CLOSE, 0);

		mutex_lock(&chan->lock);
		chan->open_for_write = 0;
		mutex_unlock(&chan->lock);
	}

	kref_put(&xdev->kref, cleanup_dev);

	return rc_read ? rc_read : rc_write;
}

/*
 * Xillybus' API allows device nodes to be seekable, giving the user
 * application access to a RAM array on the FPGA (or logic emulating it).
 */

static loff_t xillyusb_llseek(struct file *filp, loff_t offset, int whence)
{
	struct xillyusb_channel *chan = filp->private_data;
	struct xillyusb_dev *xdev = chan->xdev;
	loff_t pos = filp->f_pos;
	int rc = 0;
	unsigned int log2_element_size = chan->readable ?
		chan->in_log2_element_size : chan->out_log2_element_size;

	/*
	 * Take both mutexes not allowing interrupts, since it seems like
	 * common applications don't expect an -EINTR here. Besides, multiple
	 * access to a single file descriptor on seekable devices is a mess
	 * anyhow.
	 */

	mutex_lock(&chan->out_mutex);
	mutex_lock(&chan->in_mutex);

	switch (whence) {
	case SEEK_SET:
		pos = offset;
		break;
	case SEEK_CUR:
		pos += offset;
		break;
	case SEEK_END:
		pos = offset; /* Going to the end => to the beginning */
		break;
	default:
		rc = -EINVAL;
		goto end;
	}

	/* In any case, we must finish on an element boundary */
	if (pos & ((1 << log2_element_size) - 1)) {
		rc = -EINVAL;
		goto end;
	}

	rc = xillyusb_send_opcode(xdev, chan->chan_idx << 1,
				  OPCODE_SET_ADDR,
				  pos >> log2_element_size);

	if (rc)
		goto end;

	if (chan->writable) {
		chan->flushed = 0;
		rc = flush_downstream(chan, HZ, false);
	}

end:
	mutex_unlock(&chan->out_mutex);
	mutex_unlock(&chan->in_mutex);

	if (rc) /* Return error after releasing mutexes */
		return rc;

	filp->f_pos = pos;

	return pos;
}

static __poll_t xillyusb_poll(struct file *filp, poll_table *wait)
{
	struct xillyusb_channel *chan = filp->private_data;
	__poll_t mask = 0;

	if (chan->in_fifo)
		poll_wait(filp, &chan->in_fifo->waitq, wait);

	if (chan->out_ep)
		poll_wait(filp, &chan->out_ep->fifo.waitq, wait);

	/*
	 * If this is the first time poll() is called, and the file is
	 * readable, set the relevant flag. Also tell the FPGA to send all it
	 * has, to kickstart the mechanism that ensures there's always some
	 * data in in_fifo unless the stream is dry end-to-end. Note that the
	 * first poll() may not return a EPOLLIN, even if there's data on the
	 * FPGA. Rather, the data will arrive soon, and trigger the relevant
	 * wait queue.
	 */

	if (!chan->poll_used && chan->in_fifo) {
		chan->poll_used = 1;
		request_read_anything(chan, OPCODE_SET_PUSH);
	}

	/*
	 * poll() won't play ball regarding read() channels which
	 * are synchronous. Allowing that will create situations where data has
	 * been delivered at the FPGA, and users expecting select() to wake up,
	 * which it may not. So make it never work.
	 */

	if (chan->in_fifo && !chan->in_synchronous &&
	    (READ_ONCE(chan->in_fifo->fill) || !chan->read_data_ok))
		mask |= EPOLLIN | EPOLLRDNORM;

	if (chan->out_ep &&
	    (READ_ONCE(chan->out_ep->fifo.fill) != chan->out_ep->fifo.size))
		mask |= EPOLLOUT | EPOLLWRNORM;

	if (chan->xdev->error)
		mask |= EPOLLERR;

	return mask;
}

static const struct file_operations xillyusb_fops = {
	.owner      = THIS_MODULE,
	.read       = xillyusb_read,
	.write      = xillyusb_write,
	.open       = xillyusb_open,
	.flush      = xillyusb_flush,
	.release    = xillyusb_release,
	.llseek     = xillyusb_llseek,
	.poll       = xillyusb_poll,
};

static int xillyusb_setup_base_eps(struct xillyusb_dev *xdev)
{
	xdev->msg_ep = endpoint_alloc(xdev, MSG_EP_NUM | USB_DIR_OUT,
				      bulk_out_work, 1, 2);
	if (!xdev->msg_ep)
		return -ENOMEM;

	if (fifo_init(&xdev->msg_ep->fifo, 13)) /* 8 kiB */
		goto dealloc;

	xdev->msg_ep->fill_mask = -8; /* 8 bytes granularity */

	xdev->in_ep = endpoint_alloc(xdev, IN_EP_NUM | USB_DIR_IN,
				     bulk_in_work, BUF_SIZE_ORDER, BUFNUM);
	if (!xdev->in_ep)
		goto dealloc;

	try_queue_bulk_in(xdev->in_ep);

	return 0;

dealloc:
	endpoint_dealloc(xdev->msg_ep); /* Also frees FIFO mem if allocated */
	return -ENOMEM;
}

static int setup_channels(struct xillyusb_dev *xdev,
			  __le16 *chandesc,
			  int num_channels)
{
	struct xillyusb_channel *chan;
	int i;

	chan = kcalloc(num_channels, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	xdev->channels = chan;

	for (i = 0; i < num_channels; i++, chan++) {
		unsigned int in_desc = le16_to_cpu(*chandesc++);
		unsigned int out_desc = le16_to_cpu(*chandesc++);

		chan->xdev = xdev;
		mutex_init(&chan->in_mutex);
		mutex_init(&chan->out_mutex);
		mutex_init(&chan->lock);
		init_waitqueue_head(&chan->flushq);

		chan->chan_idx = i;

		if (in_desc & 0x80) { /* Entry is valid */
			chan->readable = 1;
			chan->in_synchronous = !!(in_desc & 0x40);
			chan->in_seekable = !!(in_desc & 0x20);
			chan->in_log2_element_size = in_desc & 0x0f;
			chan->in_log2_fifo_size = ((in_desc >> 8) & 0x1f) + 16;
		}

		/*
		 * A downstream channel should never exist above index 13,
		 * as it would request a nonexistent BULK endpoint > 15.
		 * In the peculiar case that it does, it's ignored silently.
		 */

		if ((out_desc & 0x80) && i < 14) { /* Entry is valid */
			chan->writable = 1;
			chan->out_synchronous = !!(out_desc & 0x40);
			chan->out_seekable = !!(out_desc & 0x20);
			chan->out_log2_element_size = out_desc & 0x0f;
			chan->out_log2_fifo_size =
				((out_desc >> 8) & 0x1f) + 16;
		}
	}

	return 0;
}

static int xillyusb_discovery(struct usb_interface *interface)
{
	int rc;
	struct xillyusb_dev *xdev = usb_get_intfdata(interface);
	__le16 bogus_chandesc[2];
	struct xillyfifo idt_fifo;
	struct xillyusb_channel *chan;
	unsigned int idt_len, names_offset;
	unsigned char *idt;
	int num_channels;

	rc = xillyusb_send_opcode(xdev, ~0, OPCODE_QUIESCE, 0);

	if (rc) {
		dev_err(&interface->dev, "Failed to send quiesce request. Aborting.\n");
		return rc;
	}

	/* Phase I: Set up one fake upstream channel and obtain IDT */

	/* Set up a fake IDT with one async IN stream */
	bogus_chandesc[0] = cpu_to_le16(0x80);
	bogus_chandesc[1] = cpu_to_le16(0);

	rc = setup_channels(xdev, bogus_chandesc, 1);

	if (rc)
		return rc;

	rc = fifo_init(&idt_fifo, LOG2_IDT_FIFO_SIZE);

	if (rc)
		return rc;

	chan = xdev->channels;

	chan->in_fifo = &idt_fifo;
	chan->read_data_ok = 1;

	xdev->num_channels = 1;

	rc = xillyusb_send_opcode(xdev, ~0, OPCODE_REQ_IDT, 0);

	if (rc) {
		dev_err(&interface->dev, "Failed to send IDT request. Aborting.\n");
		goto unfifo;
	}

	rc = wait_event_interruptible_timeout(idt_fifo.waitq,
					      !chan->read_data_ok,
					      XILLY_RESPONSE_TIMEOUT);

	if (xdev->error) {
		rc = xdev->error;
		goto unfifo;
	}

	if (rc < 0) {
		rc = -EINTR; /* Interrupt on probe method? Interesting. */
		goto unfifo;
	}

	if (chan->read_data_ok) {
		rc = -ETIMEDOUT;
		dev_err(&interface->dev, "No response from FPGA. Aborting.\n");
		goto unfifo;
	}

	idt_len = READ_ONCE(idt_fifo.fill);
	idt = kmalloc(idt_len, GFP_KERNEL);

	if (!idt) {
		rc = -ENOMEM;
		goto unfifo;
	}

	fifo_read(&idt_fifo, idt, idt_len, xilly_memcpy);

	if (crc32_le(~0, idt, idt_len) != 0) {
		dev_err(&interface->dev, "IDT failed CRC check. Aborting.\n");
		rc = -ENODEV;
		goto unidt;
	}

	if (*idt > 0x90) {
		dev_err(&interface->dev, "No support for IDT version 0x%02x. Maybe the xillyusb driver needs an upgrade. Aborting.\n",
			(int)*idt);
		rc = -ENODEV;
		goto unidt;
	}

	/* Phase II: Set up the streams as defined in IDT */

	num_channels = le16_to_cpu(*((__le16 *)(idt + 1)));
	names_offset = 3 + num_channels * 4;
	idt_len -= 4; /* Exclude CRC */

	if (idt_len < names_offset) {
		dev_err(&interface->dev, "IDT too short. This is exceptionally weird, because its CRC is OK\n");
		rc = -ENODEV;
		goto unidt;
	}

	rc = setup_channels(xdev, (void *)idt + 3, num_channels);

	if (rc)
		goto unidt;

	/*
	 * Except for wildly misbehaving hardware, or if it was disconnected
	 * just after responding with the IDT, there is no reason for any
	 * work item to be running now. To be sure that xdev->channels
	 * is updated on anything that might run in parallel, flush the
	 * workqueue, which rarely does anything.
	 */
	flush_workqueue(xdev->workq);

	xdev->num_channels = num_channels;

	fifo_mem_release(&idt_fifo);
	kfree(chan);

	rc = xillybus_init_chrdev(&interface->dev, &xillyusb_fops,
				  THIS_MODULE, xdev,
				  idt + names_offset,
				  idt_len - names_offset,
				  num_channels,
				  xillyname, true);

	kfree(idt);

	return rc;

unidt:
	kfree(idt);

unfifo:
	safely_assign_in_fifo(chan, NULL);
	fifo_mem_release(&idt_fifo);

	return rc;
}

static int xillyusb_probe(struct usb_interface *interface,
			  const struct usb_device_id *id)
{
	struct xillyusb_dev *xdev;
	int rc;

	xdev = kzalloc(sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	kref_init(&xdev->kref);
	mutex_init(&xdev->process_in_mutex);
	mutex_init(&xdev->msg_mutex);

	xdev->udev = usb_get_dev(interface_to_usbdev(interface));
	xdev->dev = &interface->dev;
	xdev->error = 0;
	spin_lock_init(&xdev->error_lock);
	xdev->in_counter = 0;
	xdev->in_bytes_left = 0;
	xdev->workq = alloc_workqueue(xillyname, WQ_HIGHPRI, 0);

	if (!xdev->workq) {
		dev_err(&interface->dev, "Failed to allocate work queue\n");
		rc = -ENOMEM;
		goto fail;
	}

	INIT_WORK(&xdev->wakeup_workitem, wakeup_all);

	usb_set_intfdata(interface, xdev);

	rc = xillyusb_setup_base_eps(xdev);
	if (rc)
		goto fail;

	rc = xillyusb_discovery(interface);
	if (rc)
		goto latefail;

	return 0;

latefail:
	endpoint_quiesce(xdev->in_ep);
	endpoint_quiesce(xdev->msg_ep);

fail:
	usb_set_intfdata(interface, NULL);
	kref_put(&xdev->kref, cleanup_dev);
	return rc;
}

static void xillyusb_disconnect(struct usb_interface *interface)
{
	struct xillyusb_dev *xdev = usb_get_intfdata(interface);
	struct xillyusb_endpoint *msg_ep = xdev->msg_ep;
	struct xillyfifo *fifo = &msg_ep->fifo;
	int rc;
	int i;

	xillybus_cleanup_chrdev(xdev, &interface->dev);

	/*
	 * Try to send OPCODE_QUIESCE, which will fail silently if the device
	 * was disconnected, but makes sense on module unload.
	 */

	msg_ep->wake_on_drain = true;
	xillyusb_send_opcode(xdev, ~0, OPCODE_QUIESCE, 0);

	/*
	 * If the device has been disconnected, sending the opcode causes
	 * a global device error with xdev->error, if such error didn't
	 * occur earlier. Hence timing out means that the USB link is fine,
	 * but somehow the message wasn't sent. Should never happen.
	 */

	rc = wait_event_interruptible_timeout(fifo->waitq,
					      msg_ep->drained || xdev->error,
					      XILLY_RESPONSE_TIMEOUT);

	if (!rc)
		dev_err(&interface->dev,
			"Weird timeout condition on sending quiesce request.\n");

	report_io_error(xdev, -ENODEV); /* Discourage further activity */

	/*
	 * This device driver is declared with soft_unbind set, or else
	 * sending OPCODE_QUIESCE above would always fail. The price is
	 * that the USB framework didn't kill outstanding URBs, so it has
	 * to be done explicitly before returning from this call.
	 */

	for (i = 0; i < xdev->num_channels; i++) {
		struct xillyusb_channel *chan = &xdev->channels[i];

		/*
		 * Lock taken to prevent chan->out_ep from changing. It also
		 * ensures xillyusb_open() and xillyusb_flush() don't access
		 * xdev->dev after being nullified below.
		 */
		mutex_lock(&chan->lock);
		if (chan->out_ep)
			endpoint_quiesce(chan->out_ep);
		mutex_unlock(&chan->lock);
	}

	endpoint_quiesce(xdev->in_ep);
	endpoint_quiesce(xdev->msg_ep);

	usb_set_intfdata(interface, NULL);

	xdev->dev = NULL;

	kref_put(&xdev->kref, cleanup_dev);
}

static struct usb_driver xillyusb_driver = {
	.name = xillyname,
	.id_table = xillyusb_table,
	.probe = xillyusb_probe,
	.disconnect = xillyusb_disconnect,
	.soft_unbind = 1,
};

static int __init xillyusb_init(void)
{
	int rc = 0;

	if (LOG2_INITIAL_FIFO_BUF_SIZE > PAGE_SHIFT)
		fifo_buf_order = LOG2_INITIAL_FIFO_BUF_SIZE - PAGE_SHIFT;
	else
		fifo_buf_order = 0;

	rc = usb_register(&xillyusb_driver);

	return rc;
}

static void __exit xillyusb_exit(void)
{
	usb_deregister(&xillyusb_driver);
}

module_init(xillyusb_init);
module_exit(xillyusb_exit);
