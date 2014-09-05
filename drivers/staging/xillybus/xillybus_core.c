/*
 * linux/drivers/misc/xillybus_core.c
 *
 * Copyright 2011 Xillybus Ltd, http://xillybus.com
 *
 * Driver for the Xillybus FPGA/host framework.
 *
 * This driver interfaces with a special IP core in an FPGA, setting up
 * a pipe between a hardware FIFO in the programmable logic and a device
 * file in the host. The number of such pipes and their attributes are
 * set up on the logic. This driver detects these automatically and
 * creates the device files accordingly.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/list.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/crc32.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include "xillybus.h"

MODULE_DESCRIPTION("Xillybus core functions");
MODULE_AUTHOR("Eli Billauer, Xillybus Ltd.");
MODULE_VERSION("1.07");
MODULE_ALIAS("xillybus_core");
MODULE_LICENSE("GPL v2");

/* General timeout is 100 ms, rx timeout is 10 ms */
#define XILLY_RX_TIMEOUT (10*HZ/1000)
#define XILLY_TIMEOUT (100*HZ/1000)

#define fpga_msg_ctrl_reg              0x0008
#define fpga_dma_control_reg           0x0020
#define fpga_dma_bufno_reg             0x0024
#define fpga_dma_bufaddr_lowaddr_reg   0x0028
#define fpga_dma_bufaddr_highaddr_reg  0x002c
#define fpga_buf_ctrl_reg              0x0030
#define fpga_buf_offset_reg            0x0034
#define fpga_endian_reg                0x0040

#define XILLYMSG_OPCODE_RELEASEBUF 1
#define XILLYMSG_OPCODE_QUIESCEACK 2
#define XILLYMSG_OPCODE_FIFOEOF 3
#define XILLYMSG_OPCODE_FATAL_ERROR 4
#define XILLYMSG_OPCODE_NONEMPTY 5

static const char xillyname[] = "xillybus";

static struct class *xillybus_class;

/*
 * ep_list_lock is the last lock to be taken; No other lock requests are
 * allowed while holding it. It merely protects list_of_endpoints, and not
 * the endpoints listed in it.
 */

static LIST_HEAD(list_of_endpoints);
static struct mutex ep_list_lock;
static struct workqueue_struct *xillybus_wq;

/*
 * Locking scheme: Mutexes protect invocations of character device methods.
 * If both locks are taken, wr_mutex is taken first, rd_mutex second.
 *
 * wr_spinlock protects wr_*_buf_idx, wr_empty, wr_sleepy, wr_ready and the
 * buffers' end_offset fields against changes made by IRQ handler (and in
 * theory, other file request handlers, but the mutex handles that). Nothing
 * else.
 * They are held for short direct memory manipulations. Needless to say,
 * no mutex locking is allowed when a spinlock is held.
 *
 * rd_spinlock does the same with rd_*_buf_idx, rd_empty and end_offset.
 *
 * register_mutex is endpoint-specific, and is held when non-atomic
 * register operations are performed. wr_mutex and rd_mutex may be
 * held when register_mutex is taken, but none of the spinlocks. Note that
 * register_mutex doesn't protect against sporadic buf_ctrl_reg writes
 * which are unrelated to buf_offset_reg, since they are harmless.
 *
 * Blocking on the wait queues is allowed with mutexes held, but not with
 * spinlocks.
 *
 * Only interruptible blocking is allowed on mutexes and wait queues.
 *
 * All in all, the locking order goes (with skips allowed, of course):
 * wr_mutex -> rd_mutex -> register_mutex -> wr_spinlock -> rd_spinlock
 */

static void malformed_message(struct xilly_endpoint *endpoint, u32 *buf)
{
	int opcode;
	int msg_channel, msg_bufno, msg_data, msg_dir;

	opcode = (buf[0] >> 24) & 0xff;
	msg_dir = buf[0] & 1;
	msg_channel = (buf[0] >> 1) & 0x7ff;
	msg_bufno = (buf[0] >> 12) & 0x3ff;
	msg_data = buf[1] & 0xfffffff;

	dev_warn(endpoint->dev,
		 "Malformed message (skipping): opcode=%d, channel=%03x, dir=%d, bufno=%03x, data=%07x\n",
		 opcode, msg_channel, msg_dir, msg_bufno, msg_data);
}

/*
 * xillybus_isr assumes the interrupt is allocated exclusively to it,
 * which is the natural case MSI and several other hardware-oriented
 * interrupts. Sharing is not allowed.
 */

irqreturn_t xillybus_isr(int irq, void *data)
{
	struct xilly_endpoint *ep = data;
	u32 *buf;
	unsigned int buf_size;
	int i;
	int opcode;
	unsigned int msg_channel, msg_bufno, msg_data, msg_dir;
	struct xilly_channel *channel;

	buf = ep->msgbuf_addr;
	buf_size = ep->msg_buf_size/sizeof(u32);

	ep->ephw->hw_sync_sgl_for_cpu(ep,
				      ep->msgbuf_dma_addr,
				      ep->msg_buf_size,
				      DMA_FROM_DEVICE);

	for (i = 0; i < buf_size; i += 2) {
		if (((buf[i+1] >> 28) & 0xf) != ep->msg_counter) {
			malformed_message(ep, &buf[i]);
			dev_warn(ep->dev,
				 "Sending a NACK on counter %x (instead of %x) on entry %d\n",
				 ((buf[i+1] >> 28) & 0xf),
				 ep->msg_counter,
				 i/2);

			if (++ep->failed_messages > 10) {
				dev_err(ep->dev,
					"Lost sync with interrupt messages. Stopping.\n");
			} else {
				ep->ephw->hw_sync_sgl_for_device(
					ep,
					ep->msgbuf_dma_addr,
					ep->msg_buf_size,
					DMA_FROM_DEVICE);

				iowrite32(0x01,  /* Message NACK */
					  ep->registers + fpga_msg_ctrl_reg);
			}
			return IRQ_HANDLED;
		} else if (buf[i] & (1 << 22)) /* Last message */
			break;
	}

	if (i >= buf_size) {
		dev_err(ep->dev, "Bad interrupt message. Stopping.\n");
		return IRQ_HANDLED;
	}

	buf_size = i + 2;

	for (i = 0; i < buf_size; i += 2) { /* Scan through messages */
		opcode = (buf[i] >> 24) & 0xff;

		msg_dir = buf[i] & 1;
		msg_channel = (buf[i] >> 1) & 0x7ff;
		msg_bufno = (buf[i] >> 12) & 0x3ff;
		msg_data = buf[i+1] & 0xfffffff;

		switch (opcode) {
		case XILLYMSG_OPCODE_RELEASEBUF:
			if ((msg_channel > ep->num_channels) ||
			    (msg_channel == 0)) {
				malformed_message(ep, &buf[i]);
				break;
			}

			channel = ep->channels[msg_channel];

			if (msg_dir) { /* Write channel */
				if (msg_bufno >= channel->num_wr_buffers) {
					malformed_message(ep, &buf[i]);
					break;
				}
				spin_lock(&channel->wr_spinlock);
				channel->wr_buffers[msg_bufno]->end_offset =
					msg_data;
				channel->wr_fpga_buf_idx = msg_bufno;
				channel->wr_empty = 0;
				channel->wr_sleepy = 0;
				spin_unlock(&channel->wr_spinlock);

				wake_up_interruptible(&channel->wr_wait);

			} else {
				/* Read channel */

				if (msg_bufno >= channel->num_rd_buffers) {
					malformed_message(ep, &buf[i]);
					break;
				}

				spin_lock(&channel->rd_spinlock);
				channel->rd_fpga_buf_idx = msg_bufno;
				channel->rd_full = 0;
				spin_unlock(&channel->rd_spinlock);

				wake_up_interruptible(&channel->rd_wait);
				if (!channel->rd_synchronous)
					queue_delayed_work(
						xillybus_wq,
						&channel->rd_workitem,
						XILLY_RX_TIMEOUT);
			}

			break;
		case XILLYMSG_OPCODE_NONEMPTY:
			if ((msg_channel > ep->num_channels) ||
			    (msg_channel == 0) || (!msg_dir) ||
			    !ep->channels[msg_channel]->wr_supports_nonempty) {
				malformed_message(ep, &buf[i]);
				break;
			}

			channel = ep->channels[msg_channel];

			if (msg_bufno >= channel->num_wr_buffers) {
				malformed_message(ep, &buf[i]);
				break;
			}
			spin_lock(&channel->wr_spinlock);
			if (msg_bufno == channel->wr_host_buf_idx)
				channel->wr_ready = 1;
			spin_unlock(&channel->wr_spinlock);

			wake_up_interruptible(&channel->wr_ready_wait);

			break;
		case XILLYMSG_OPCODE_QUIESCEACK:
			ep->idtlen = msg_data;
			wake_up_interruptible(&ep->ep_wait);

			break;
		case XILLYMSG_OPCODE_FIFOEOF:
			if ((msg_channel > ep->num_channels) ||
			    (msg_channel == 0) || (!msg_dir) ||
			    !ep->channels[msg_channel]->num_wr_buffers) {
				malformed_message(ep, &buf[i]);
				break;
			}
			channel = ep->channels[msg_channel];
			spin_lock(&channel->wr_spinlock);
			channel->wr_eof = msg_bufno;
			channel->wr_sleepy = 0;

			channel->wr_hangup = channel->wr_empty &&
				(channel->wr_host_buf_idx == msg_bufno);

			spin_unlock(&channel->wr_spinlock);

			wake_up_interruptible(&channel->wr_wait);

			break;
		case XILLYMSG_OPCODE_FATAL_ERROR:
			ep->fatal_error = 1;
			wake_up_interruptible(&ep->ep_wait); /* For select() */
			dev_err(ep->dev,
				"FPGA reported a fatal error. This means that the low-level communication with the device has failed. This hardware problem is most likely unrelated to Xillybus (neither kernel module nor FPGA core), but reports are still welcome. All I/O is aborted.\n");
			break;
		default:
			malformed_message(ep, &buf[i]);
			break;
		}
	}

	ep->ephw->hw_sync_sgl_for_device(ep,
					 ep->msgbuf_dma_addr,
					 ep->msg_buf_size,
					 DMA_FROM_DEVICE);

	ep->msg_counter = (ep->msg_counter + 1) & 0xf;
	ep->failed_messages = 0;
	iowrite32(0x03, ep->registers + fpga_msg_ctrl_reg); /* Message ACK */

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(xillybus_isr);

/*
 * A few trivial memory management functions.
 * NOTE: These functions are used only on probe and remove, and therefore
 * no locks are applied!
 */

static void xillybus_autoflush(struct work_struct *work);

struct xilly_alloc_state {
	void *salami;
	int left_of_salami;
	int nbuffer;
	enum dma_data_direction direction;
	u32 regdirection;
};

static int xilly_get_dma_buffers(struct xilly_endpoint *ep,
				 struct xilly_alloc_state *s,
				 struct xilly_buffer **buffers,
				 int bufnum, int bytebufsize)
{
	int i, rc;
	dma_addr_t dma_addr;
	struct device *dev = ep->dev;
	struct xilly_buffer *this_buffer = NULL; /* Init to silence warning */

	if (buffers) { /* Not the message buffer */
		this_buffer = devm_kcalloc(dev, bufnum,
					   sizeof(struct xilly_buffer),
					   GFP_KERNEL);
		if (!this_buffer)
			return -ENOMEM;
	}

	for (i = 0; i < bufnum; i++) {
		/*
		 * Buffers are expected in descending size order, so there
		 * is either enough space for this buffer or none at all.
		 */

		if ((s->left_of_salami < bytebufsize) &&
		    (s->left_of_salami > 0)) {
			dev_err(ep->dev,
				"Corrupt buffer allocation in IDT. Aborting.\n");
			return -ENODEV;
		}

		if (s->left_of_salami == 0) {
			int allocorder, allocsize;

			allocsize = PAGE_SIZE;
			allocorder = 0;
			while (bytebufsize > allocsize) {
				allocsize *= 2;
				allocorder++;
			}

			s->salami = (void *) devm_get_free_pages(
				dev,
				GFP_KERNEL | __GFP_DMA32 | __GFP_ZERO,
				allocorder);
			if (!s->salami)
				return -ENOMEM;

			s->left_of_salami = allocsize;
		}

		rc = ep->ephw->map_single(ep, s->salami,
					  bytebufsize, s->direction,
					  &dma_addr);
		if (rc)
			return rc;

		iowrite32((u32) (dma_addr & 0xffffffff),
			  ep->registers + fpga_dma_bufaddr_lowaddr_reg);
		iowrite32(((u32) ((((u64) dma_addr) >> 32) & 0xffffffff)),
			  ep->registers + fpga_dma_bufaddr_highaddr_reg);

		if (buffers) { /* Not the message buffer */
			this_buffer->addr = s->salami;
			this_buffer->dma_addr = dma_addr;
			buffers[i] = this_buffer++;

			iowrite32(s->regdirection | s->nbuffer++,
				  ep->registers + fpga_dma_bufno_reg);
		} else {
			ep->msgbuf_addr = s->salami;
			ep->msgbuf_dma_addr = dma_addr;
			ep->msg_buf_size = bytebufsize;

			iowrite32(s->regdirection,
				  ep->registers + fpga_dma_bufno_reg);
		}

		s->left_of_salami -= bytebufsize;
		s->salami += bytebufsize;
	}
	return 0;
}

static int xilly_setupchannels(struct xilly_endpoint *ep,
			       unsigned char *chandesc,
			       int entries)
{
	struct device *dev = ep->dev;
	int i, entry, rc;
	struct xilly_channel *channel;
	int channelnum, bufnum, bufsize, format, is_writebuf;
	int bytebufsize;
	int synchronous, allowpartial, exclusive_open, seekable;
	int supports_nonempty;
	int msg_buf_done = 0;

	struct xilly_alloc_state rd_alloc = {
		.salami = NULL,
		.left_of_salami = 0,
		.nbuffer = 1,
		.direction = DMA_TO_DEVICE,
		.regdirection = 0,
	};

	struct xilly_alloc_state wr_alloc = {
		.salami = NULL,
		.left_of_salami = 0,
		.nbuffer = 1,
		.direction = DMA_FROM_DEVICE,
		.regdirection = 0x80000000,
	};

	channel = devm_kcalloc(dev, ep->num_channels,
			       sizeof(struct xilly_channel), GFP_KERNEL);
	if (!channel)
		return -ENOMEM;

	ep->channels = devm_kcalloc(dev, ep->num_channels + 1,
				    sizeof(struct xilly_channel *),
				    GFP_KERNEL);
	if (!ep->channels)
		return -ENOMEM;

	ep->channels[0] = NULL; /* Channel 0 is message buf. */

	/* Initialize all channels with defaults */

	for (i = 1; i <= ep->num_channels; i++) {
		channel->wr_buffers = NULL;
		channel->rd_buffers = NULL;
		channel->num_wr_buffers = 0;
		channel->num_rd_buffers = 0;
		channel->wr_fpga_buf_idx = -1;
		channel->wr_host_buf_idx = 0;
		channel->wr_host_buf_pos = 0;
		channel->wr_empty = 1;
		channel->wr_ready = 0;
		channel->wr_sleepy = 1;
		channel->rd_fpga_buf_idx = 0;
		channel->rd_host_buf_idx = 0;
		channel->rd_host_buf_pos = 0;
		channel->rd_full = 0;
		channel->wr_ref_count = 0;
		channel->rd_ref_count = 0;

		spin_lock_init(&channel->wr_spinlock);
		spin_lock_init(&channel->rd_spinlock);
		mutex_init(&channel->wr_mutex);
		mutex_init(&channel->rd_mutex);
		init_waitqueue_head(&channel->rd_wait);
		init_waitqueue_head(&channel->wr_wait);
		init_waitqueue_head(&channel->wr_ready_wait);

		INIT_DELAYED_WORK(&channel->rd_workitem, xillybus_autoflush);

		channel->endpoint = ep;
		channel->chan_num = i;

		channel->log2_element_size = 0;

		ep->channels[i] = channel++;
	}

	for (entry = 0; entry < entries; entry++, chandesc += 4) {
		struct xilly_buffer **buffers = NULL;

		is_writebuf = chandesc[0] & 0x01;
		channelnum = (chandesc[0] >> 1) | ((chandesc[1] & 0x0f) << 7);
		format = (chandesc[1] >> 4) & 0x03;
		allowpartial = (chandesc[1] >> 6) & 0x01;
		synchronous = (chandesc[1] >> 7) & 0x01;
		bufsize = 1 << (chandesc[2] & 0x1f);
		bufnum = 1 << (chandesc[3] & 0x0f);
		exclusive_open = (chandesc[2] >> 7) & 0x01;
		seekable = (chandesc[2] >> 6) & 0x01;
		supports_nonempty = (chandesc[2] >> 5) & 0x01;

		if ((channelnum > ep->num_channels) ||
		    ((channelnum == 0) && !is_writebuf)) {
			dev_err(ep->dev,
				"IDT requests channel out of range. Aborting.\n");
			return -ENODEV;
		}

		channel = ep->channels[channelnum]; /* NULL for msg channel */

		if (!is_writebuf || channelnum > 0) {
			channel->log2_element_size = ((format > 2) ?
						      2 : format);

			bytebufsize = channel->rd_buf_size = bufsize *
				(1 << channel->log2_element_size);

			buffers = devm_kcalloc(dev, bufnum,
					       sizeof(struct xilly_buffer *),
					       GFP_KERNEL);
			if (!buffers)
				return -ENOMEM;
		} else {
			bytebufsize = bufsize << 2;
		}

		if (!is_writebuf) {
			channel->num_rd_buffers = bufnum;
			channel->rd_allow_partial = allowpartial;
			channel->rd_synchronous = synchronous;
			channel->rd_exclusive_open = exclusive_open;
			channel->seekable = seekable;

			channel->rd_buffers = buffers;
			rc = xilly_get_dma_buffers(ep, &rd_alloc, buffers,
						   bufnum, bytebufsize);
		} else if (channelnum > 0) {
			channel->num_wr_buffers = bufnum;

			channel->seekable = seekable;
			channel->wr_supports_nonempty = supports_nonempty;

			channel->wr_allow_partial = allowpartial;
			channel->wr_synchronous = synchronous;
			channel->wr_exclusive_open = exclusive_open;

			channel->wr_buffers = buffers;
			rc = xilly_get_dma_buffers(ep, &wr_alloc, buffers,
						   bufnum, bytebufsize);
		} else {
			rc = xilly_get_dma_buffers(ep, &wr_alloc, NULL,
						   bufnum, bytebufsize);
			msg_buf_done++;
		}

		if (rc)
			return -ENOMEM;
	}

	if (!msg_buf_done) {
		dev_err(ep->dev,
			"Corrupt IDT: No message buffer. Aborting.\n");
		return -ENODEV;
	}
	return 0;
}

static int xilly_scan_idt(struct xilly_endpoint *endpoint,
			  struct xilly_idt_handle *idt_handle)
{
	int count = 0;
	unsigned char *idt = endpoint->channels[1]->wr_buffers[0]->addr;
	unsigned char *end_of_idt = idt + endpoint->idtlen - 4;
	unsigned char *scan;
	int len;

	scan = idt;
	idt_handle->idt = idt;

	scan++; /* Skip version number */

	while ((scan <= end_of_idt) && *scan) {
		while ((scan <= end_of_idt) && *scan++)
			/* Do nothing, just scan thru string */;
		count++;
	}

	scan++;

	if (scan > end_of_idt) {
		dev_err(endpoint->dev,
			"IDT device name list overflow. Aborting.\n");
		return -ENODEV;
	}
	idt_handle->chandesc = scan;

	len = endpoint->idtlen - (3 + ((int) (scan - idt)));

	if (len & 0x03) {
		dev_err(endpoint->dev,
			"Corrupt IDT device name list. Aborting.\n");
		return -ENODEV;
	}

	idt_handle->entries = len >> 2;
	endpoint->num_channels = count;

	return 0;
}

static int xilly_obtain_idt(struct xilly_endpoint *endpoint)
{
	struct xilly_channel *channel;
	unsigned char *version;
	long t;

	channel = endpoint->channels[1]; /* This should be generated ad-hoc */

	channel->wr_sleepy = 1;

	iowrite32(1 |
		  (3 << 24), /* Opcode 3 for channel 0 = Send IDT */
		  endpoint->registers + fpga_buf_ctrl_reg);

	t = wait_event_interruptible_timeout(channel->wr_wait,
					     (!channel->wr_sleepy),
					     XILLY_TIMEOUT);

	if (t <= 0) {
		dev_err(endpoint->dev, "Failed to obtain IDT. Aborting.\n");

		if (endpoint->fatal_error)
			return -EIO;

		return -ENODEV;
	}

	endpoint->ephw->hw_sync_sgl_for_cpu(
		channel->endpoint,
		channel->wr_buffers[0]->dma_addr,
		channel->wr_buf_size,
		DMA_FROM_DEVICE);

	if (channel->wr_buffers[0]->end_offset != endpoint->idtlen) {
		dev_err(endpoint->dev,
			"IDT length mismatch (%d != %d). Aborting.\n",
			channel->wr_buffers[0]->end_offset, endpoint->idtlen);
		return -ENODEV;
	}

	if (crc32_le(~0, channel->wr_buffers[0]->addr,
		     endpoint->idtlen+1) != 0) {
		dev_err(endpoint->dev, "IDT failed CRC check. Aborting.\n");
		return -ENODEV;
	}

	version = channel->wr_buffers[0]->addr;

	/* Check version number. Accept anything below 0x82 for now. */
	if (*version > 0x82) {
		dev_err(endpoint->dev,
			"No support for IDT version 0x%02x. Maybe the xillybus driver needs an upgarde. Aborting.\n",
			*version);
		return -ENODEV;
	}

	return 0;
}

static ssize_t xillybus_read(struct file *filp, char __user *userbuf,
			     size_t count, loff_t *f_pos)
{
	ssize_t rc;
	unsigned long flags;
	int bytes_done = 0;
	int no_time_left = 0;
	long deadline, left_to_sleep;
	struct xilly_channel *channel = filp->private_data;

	int empty, reached_eof, exhausted, ready;
	/* Initializations are there only to silence warnings */

	int howmany = 0, bufpos = 0, bufidx = 0, bufferdone = 0;
	int waiting_bufidx;

	if (channel->endpoint->fatal_error)
		return -EIO;

	deadline = jiffies + 1 + XILLY_RX_TIMEOUT;

	rc = mutex_lock_interruptible(&channel->wr_mutex);
	if (rc)
		return rc;

	while (1) { /* Note that we may drop mutex within this loop */
		int bytes_to_do = count - bytes_done;

		spin_lock_irqsave(&channel->wr_spinlock, flags);

		empty = channel->wr_empty;
		ready = !empty || channel->wr_ready;

		if (!empty) {
			bufidx = channel->wr_host_buf_idx;
			bufpos = channel->wr_host_buf_pos;
			howmany = ((channel->wr_buffers[bufidx]->end_offset
				    + 1) << channel->log2_element_size)
				- bufpos;

			/* Update wr_host_* to its post-operation state */
			if (howmany > bytes_to_do) {
				bufferdone = 0;

				howmany = bytes_to_do;
				channel->wr_host_buf_pos += howmany;
			} else {
				bufferdone = 1;

				channel->wr_host_buf_pos = 0;

				if (bufidx == channel->wr_fpga_buf_idx) {
					channel->wr_empty = 1;
					channel->wr_sleepy = 1;
					channel->wr_ready = 0;
				}

				if (bufidx >= (channel->num_wr_buffers - 1))
					channel->wr_host_buf_idx = 0;
				else
					channel->wr_host_buf_idx++;
			}
		}

		/*
		 * Marking our situation after the possible changes above,
		 * for use after releasing the spinlock.
		 *
		 * empty = empty before change
		 * exhasted = empty after possible change
		 */

		reached_eof = channel->wr_empty &&
			(channel->wr_host_buf_idx == channel->wr_eof);
		channel->wr_hangup = reached_eof;
		exhausted = channel->wr_empty;
		waiting_bufidx = channel->wr_host_buf_idx;

		spin_unlock_irqrestore(&channel->wr_spinlock, flags);

		if (!empty) { /* Go on, now without the spinlock */

			if (bufpos == 0) /* Position zero means it's virgin */
				channel->endpoint->ephw->hw_sync_sgl_for_cpu(
					channel->endpoint,
					channel->wr_buffers[bufidx]->dma_addr,
					channel->wr_buf_size,
					DMA_FROM_DEVICE);

			if (copy_to_user(
				    userbuf,
				    channel->wr_buffers[bufidx]->addr
				    + bufpos, howmany))
				rc = -EFAULT;

			userbuf += howmany;
			bytes_done += howmany;

			if (bufferdone) {
				channel->endpoint->ephw->hw_sync_sgl_for_device(
					channel->endpoint,
					channel->wr_buffers[bufidx]->dma_addr,
					channel->wr_buf_size,
					DMA_FROM_DEVICE);

				/*
				 * Tell FPGA the buffer is done with. It's an
				 * atomic operation to the FPGA, so what
				 * happens with other channels doesn't matter,
				 * and the certain channel is protected with
				 * the channel-specific mutex.
				 */

				iowrite32(1 | (channel->chan_num << 1) |
					  (bufidx << 12),
					  channel->endpoint->registers +
					  fpga_buf_ctrl_reg);
			}

			if (rc) {
				mutex_unlock(&channel->wr_mutex);
				return rc;
			}
		}

		/* This includes a zero-count return = EOF */
		if ((bytes_done >= count) || reached_eof)
			break;

		if (!exhausted)
			continue; /* More in RAM buffer(s)? Just go on. */

		if ((bytes_done > 0) &&
		    (no_time_left ||
		     (channel->wr_synchronous && channel->wr_allow_partial)))
			break;

		/*
		 * Nonblocking read: The "ready" flag tells us that the FPGA
		 * has data to send. In non-blocking mode, if it isn't on,
		 * just return. But if there is, we jump directly to the point
		 * where we ask for the FPGA to send all it has, and wait
		 * until that data arrives. So in a sense, we *do* block in
		 * nonblocking mode, but only for a very short time.
		 */

		if (!no_time_left && (filp->f_flags & O_NONBLOCK)) {
			if (bytes_done > 0)
				break;

			if (ready)
				goto desperate;

			rc = -EAGAIN;
			break;
		}

		if (!no_time_left || (bytes_done > 0)) {
			/*
			 * Note that in case of an element-misaligned read
			 * request, offsetlimit will include the last element,
			 * which will be partially read from.
			 */
			int offsetlimit = ((count - bytes_done) - 1) >>
				channel->log2_element_size;
			int buf_elements = channel->wr_buf_size >>
				channel->log2_element_size;

			/*
			 * In synchronous mode, always send an offset limit.
			 * Just don't send a value too big.
			 */

			if (channel->wr_synchronous) {
				/* Don't request more than one buffer */
				if (channel->wr_allow_partial &&
				    (offsetlimit >= buf_elements))
					offsetlimit = buf_elements - 1;

				/* Don't request more than all buffers */
				if (!channel->wr_allow_partial &&
				    (offsetlimit >=
				     (buf_elements * channel->num_wr_buffers)))
					offsetlimit = buf_elements *
						channel->num_wr_buffers - 1;
			}

			/*
			 * In asynchronous mode, force early flush of a buffer
			 * only if that will allow returning a full count. The
			 * "offsetlimit < ( ... )" rather than "<=" excludes
			 * requesting a full buffer, which would obviously
			 * cause a buffer transmission anyhow
			 */

			if (channel->wr_synchronous ||
			    (offsetlimit < (buf_elements - 1))) {
				mutex_lock(&channel->endpoint->register_mutex);

				iowrite32(offsetlimit,
					  channel->endpoint->registers +
					  fpga_buf_offset_reg);

				iowrite32(1 | (channel->chan_num << 1) |
					  (2 << 24) |  /* 2 = offset limit */
					  (waiting_bufidx << 12),
					  channel->endpoint->registers +
					  fpga_buf_ctrl_reg);

				mutex_unlock(&channel->endpoint->
					     register_mutex);
			}
		}

		/*
		 * If partial completion is disallowed, there is no point in
		 * timeout sleeping. Neither if no_time_left is set and
		 * there's no data.
		 */

		if (!channel->wr_allow_partial ||
		    (no_time_left && (bytes_done == 0))) {
			/*
			 * This do-loop will run more than once if another
			 * thread reasserted wr_sleepy before we got the mutex
			 * back, so we try again.
			 */

			do {
				mutex_unlock(&channel->wr_mutex);

				if (wait_event_interruptible(
					    channel->wr_wait,
					    (!channel->wr_sleepy)))
					goto interrupted;

				if (mutex_lock_interruptible(
					    &channel->wr_mutex))
					goto interrupted;
			} while (channel->wr_sleepy);

			continue;

interrupted: /* Mutex is not held if got here */
			if (channel->endpoint->fatal_error)
				return -EIO;
			if (bytes_done)
				return bytes_done;
			if (filp->f_flags & O_NONBLOCK)
				return -EAGAIN; /* Don't admit snoozing */
			return -EINTR;
		}

		left_to_sleep = deadline - ((long) jiffies);

		/*
		 * If our time is out, skip the waiting. We may miss wr_sleepy
		 * being deasserted but hey, almost missing the train is like
		 * missing it.
		 */

		if (left_to_sleep > 0) {
			left_to_sleep =
				wait_event_interruptible_timeout(
					channel->wr_wait,
					(!channel->wr_sleepy),
					left_to_sleep);

			if (left_to_sleep > 0) /* wr_sleepy deasserted */
				continue;

			if (left_to_sleep < 0) { /* Interrupt */
				mutex_unlock(&channel->wr_mutex);
				if (channel->endpoint->fatal_error)
					return -EIO;
				if (bytes_done)
					return bytes_done;
				return -EINTR;
			}
		}

desperate:
		no_time_left = 1; /* We're out of sleeping time. Desperate! */

		if (bytes_done == 0) {
			/*
			 * Reaching here means that we allow partial return,
			 * that we've run out of time, and that we have
			 * nothing to return.
			 * So tell the FPGA to send anything it has or gets.
			 */

			iowrite32(1 | (channel->chan_num << 1) |
				  (3 << 24) |  /* Opcode 3, flush it all! */
				  (waiting_bufidx << 12),
				  channel->endpoint->registers +
				  fpga_buf_ctrl_reg);
		}

		/*
		 * Reaching here means that we *do* have data in the buffer,
		 * but the "partial" flag disallows returning less than
		 * required. And we don't have as much. So loop again,
		 * which is likely to end up blocking indefinitely until
		 * enough data has arrived.
		 */
	}

	mutex_unlock(&channel->wr_mutex);

	if (channel->endpoint->fatal_error)
		return -EIO;

	if (rc)
		return rc;

	return bytes_done;
}

/*
 * The timeout argument takes values as follows:
 *  >0 : Flush with timeout
 * ==0 : Flush, and wait idefinitely for the flush to complete
 *  <0 : Autoflush: Flush only if there's a single buffer occupied
 */

static int xillybus_myflush(struct xilly_channel *channel, long timeout)
{
	int rc;
	unsigned long flags;

	int end_offset_plus1;
	int bufidx, bufidx_minus1;
	int i;
	int empty;
	int new_rd_host_buf_pos;

	if (channel->endpoint->fatal_error)
		return -EIO;
	rc = mutex_lock_interruptible(&channel->rd_mutex);
	if (rc)
		return rc;

	/*
	 * Don't flush a closed channel. This can happen when the work queued
	 * autoflush thread fires off after the file has closed. This is not
	 * an error, just something to dismiss.
	 */

	if (!channel->rd_ref_count)
		goto done;

	bufidx = channel->rd_host_buf_idx;

	bufidx_minus1 = (bufidx == 0) ?
		channel->num_rd_buffers - 1 :
		bufidx - 1;

	end_offset_plus1 = channel->rd_host_buf_pos >>
		channel->log2_element_size;

	new_rd_host_buf_pos = channel->rd_host_buf_pos -
		(end_offset_plus1 << channel->log2_element_size);

	/* Submit the current buffer if it's nonempty */
	if (end_offset_plus1) {
		unsigned char *tail = channel->rd_buffers[bufidx]->addr +
			(end_offset_plus1 << channel->log2_element_size);

		/* Copy  unflushed data, so we can put it in next buffer */
		for (i = 0; i < new_rd_host_buf_pos; i++)
			channel->rd_leftovers[i] = *tail++;

		spin_lock_irqsave(&channel->rd_spinlock, flags);

		/* Autoflush only if a single buffer is occupied */

		if ((timeout < 0) &&
		    (channel->rd_full ||
		     (bufidx_minus1 != channel->rd_fpga_buf_idx))) {
			spin_unlock_irqrestore(&channel->rd_spinlock, flags);
			/*
			 * A new work item may be queued by the ISR exactly
			 * now, since the execution of a work item allows the
			 * queuing of a new one while it's running.
			 */
			goto done;
		}

		/* The 4th element is never needed for data, so it's a flag */
		channel->rd_leftovers[3] = (new_rd_host_buf_pos != 0);

		/* Set up rd_full to reflect a certain moment's state */

		if (bufidx == channel->rd_fpga_buf_idx)
			channel->rd_full = 1;
		spin_unlock_irqrestore(&channel->rd_spinlock, flags);

		if (bufidx >= (channel->num_rd_buffers - 1))
			channel->rd_host_buf_idx = 0;
		else
			channel->rd_host_buf_idx++;

		channel->endpoint->ephw->hw_sync_sgl_for_device(
			channel->endpoint,
			channel->rd_buffers[bufidx]->dma_addr,
			channel->rd_buf_size,
			DMA_TO_DEVICE);

		mutex_lock(&channel->endpoint->register_mutex);

		iowrite32(end_offset_plus1 - 1,
			  channel->endpoint->registers + fpga_buf_offset_reg);

		iowrite32((channel->chan_num << 1) | /* Channel ID */
			  (2 << 24) |  /* Opcode 2, submit buffer */
			  (bufidx << 12),
			  channel->endpoint->registers + fpga_buf_ctrl_reg);

		mutex_unlock(&channel->endpoint->register_mutex);
	} else if (bufidx == 0) {
		bufidx = channel->num_rd_buffers - 1;
	} else {
		bufidx--;
	}

	channel->rd_host_buf_pos = new_rd_host_buf_pos;

	if (timeout < 0)
		goto done; /* Autoflush */

	/*
	 * bufidx is now the last buffer written to (or equal to
	 * rd_fpga_buf_idx if buffer was never written to), and
	 * channel->rd_host_buf_idx the one after it.
	 *
	 * If bufidx == channel->rd_fpga_buf_idx we're either empty or full.
	 */

	while (1) { /* Loop waiting for draining of buffers */
		spin_lock_irqsave(&channel->rd_spinlock, flags);

		if (bufidx != channel->rd_fpga_buf_idx)
			channel->rd_full = 1; /*
					       * Not really full,
					       * but needs waiting.
					       */

		empty = !channel->rd_full;

		spin_unlock_irqrestore(&channel->rd_spinlock, flags);

		if (empty)
			break;

		/*
		 * Indefinite sleep with mutex taken. With data waiting for
		 * flushing user should not be surprised if open() for write
		 * sleeps.
		 */
		if (timeout == 0)
			wait_event_interruptible(channel->rd_wait,
						 (!channel->rd_full));

		else if (wait_event_interruptible_timeout(
				 channel->rd_wait,
				 (!channel->rd_full),
				 timeout) == 0) {
			dev_warn(channel->endpoint->dev,
				 "Timed out while flushing. Output data may be lost.\n");

			rc = -ETIMEDOUT;
			break;
		}

		if (channel->rd_full) {
			rc = -EINTR;
			break;
		}
	}

done:
	mutex_unlock(&channel->rd_mutex);

	if (channel->endpoint->fatal_error)
		return -EIO;

	return rc;
}

static int xillybus_flush(struct file *filp, fl_owner_t id)
{
	if (!(filp->f_mode & FMODE_WRITE))
		return 0;

	return xillybus_myflush(filp->private_data, HZ); /* 1 second timeout */
}

static void xillybus_autoflush(struct work_struct *work)
{
	struct delayed_work *workitem = container_of(
		work, struct delayed_work, work);
	struct xilly_channel *channel = container_of(
		workitem, struct xilly_channel, rd_workitem);
	int rc;

	rc = xillybus_myflush(channel, -1);
	if (rc == -EINTR)
		dev_warn(channel->endpoint->dev,
			 "Autoflush failed because work queue thread got a signal.\n");
	else if (rc)
		dev_err(channel->endpoint->dev,
			"Autoflush failed under weird circumstances.\n");
}

static ssize_t xillybus_write(struct file *filp, const char __user *userbuf,
			      size_t count, loff_t *f_pos)
{
	ssize_t rc;
	unsigned long flags;
	int bytes_done = 0;
	struct xilly_channel *channel = filp->private_data;

	int full, exhausted;
	/* Initializations are there only to silence warnings */

	int howmany = 0, bufpos = 0, bufidx = 0, bufferdone = 0;
	int end_offset_plus1 = 0;

	if (channel->endpoint->fatal_error)
		return -EIO;

	rc = mutex_lock_interruptible(&channel->rd_mutex);
	if (rc)
		return rc;

	while (1) {
		int bytes_to_do = count - bytes_done;

		spin_lock_irqsave(&channel->rd_spinlock, flags);

		full = channel->rd_full;

		if (!full) {
			bufidx = channel->rd_host_buf_idx;
			bufpos = channel->rd_host_buf_pos;
			howmany = channel->rd_buf_size - bufpos;

			/*
			 * Update rd_host_* to its state after this operation.
			 * count=0 means committing the buffer immediately,
			 * which is like flushing, but not necessarily block.
			 */

			if ((howmany > bytes_to_do) &&
			    (count ||
			     ((bufpos >> channel->log2_element_size) == 0))) {
				bufferdone = 0;

				howmany = bytes_to_do;
				channel->rd_host_buf_pos += howmany;
			} else {
				bufferdone = 1;

				if (count) {
					end_offset_plus1 =
						channel->rd_buf_size >>
						channel->log2_element_size;
					channel->rd_host_buf_pos = 0;
				} else {
					unsigned char *tail;
					int i;

					end_offset_plus1 = bufpos >>
						channel->log2_element_size;

					channel->rd_host_buf_pos -=
						end_offset_plus1 <<
						channel->log2_element_size;

					tail = channel->
						rd_buffers[bufidx]->addr +
						(end_offset_plus1 <<
						 channel->log2_element_size);

					for (i = 0;
					     i < channel->rd_host_buf_pos;
					     i++)
						channel->rd_leftovers[i] =
							*tail++;
				}

				if (bufidx == channel->rd_fpga_buf_idx)
					channel->rd_full = 1;

				if (bufidx >= (channel->num_rd_buffers - 1))
					channel->rd_host_buf_idx = 0;
				else
					channel->rd_host_buf_idx++;
			}
		}

		/*
		 * Marking our situation after the possible changes above,
		 * for use  after releasing the spinlock.
		 *
		 * full = full before change
		 * exhasted = full after possible change
		 */

		exhausted = channel->rd_full;

		spin_unlock_irqrestore(&channel->rd_spinlock, flags);

		if (!full) { /* Go on, now without the spinlock */
			unsigned char *head =
				channel->rd_buffers[bufidx]->addr;
			int i;

			if ((bufpos == 0) || /* Zero means it's virgin */
			    (channel->rd_leftovers[3] != 0)) {
				channel->endpoint->ephw->hw_sync_sgl_for_cpu(
					channel->endpoint,
					channel->rd_buffers[bufidx]->dma_addr,
					channel->rd_buf_size,
					DMA_TO_DEVICE);

				/* Virgin, but leftovers are due */
				for (i = 0; i < bufpos; i++)
					*head++ = channel->rd_leftovers[i];

				channel->rd_leftovers[3] = 0; /* Clear flag */
			}

			if (copy_from_user(
				    channel->rd_buffers[bufidx]->addr + bufpos,
				    userbuf, howmany))
				rc = -EFAULT;

			userbuf += howmany;
			bytes_done += howmany;

			if (bufferdone) {
				channel->endpoint->ephw->hw_sync_sgl_for_device(
					channel->endpoint,
					channel->rd_buffers[bufidx]->dma_addr,
					channel->rd_buf_size,
					DMA_TO_DEVICE);

				mutex_lock(&channel->endpoint->register_mutex);

				iowrite32(end_offset_plus1 - 1,
					  channel->endpoint->registers +
					  fpga_buf_offset_reg);

				iowrite32((channel->chan_num << 1) |
					  (2 << 24) |  /* 2 = submit buffer */
					  (bufidx << 12),
					  channel->endpoint->registers +
					  fpga_buf_ctrl_reg);

				mutex_unlock(&channel->endpoint->
					     register_mutex);

				channel->rd_leftovers[3] =
					(channel->rd_host_buf_pos != 0);
			}

			if (rc) {
				mutex_unlock(&channel->rd_mutex);

				if (channel->endpoint->fatal_error)
					return -EIO;

				if (!channel->rd_synchronous)
					queue_delayed_work(
						xillybus_wq,
						&channel->rd_workitem,
						XILLY_RX_TIMEOUT);

				return rc;
			}
		}

		if (bytes_done >= count)
			break;

		if (!exhausted)
			continue; /* If there's more space, just go on */

		if ((bytes_done > 0) && channel->rd_allow_partial)
			break;

		/*
		 * Indefinite sleep with mutex taken. With data waiting for
		 * flushing, user should not be surprised if open() for write
		 * sleeps.
		 */

		if (filp->f_flags & O_NONBLOCK) {
			rc = -EAGAIN;
			break;
		}

		if (wait_event_interruptible(channel->rd_wait,
					     (!channel->rd_full))) {
			mutex_unlock(&channel->rd_mutex);

			if (channel->endpoint->fatal_error)
				return -EIO;

			if (bytes_done)
				return bytes_done;
			return -EINTR;
		}
	}

	mutex_unlock(&channel->rd_mutex);

	if (!channel->rd_synchronous)
		queue_delayed_work(xillybus_wq,
				   &channel->rd_workitem,
				   XILLY_RX_TIMEOUT);

	if (channel->endpoint->fatal_error)
		return -EIO;

	if (rc)
		return rc;

	if ((channel->rd_synchronous) && (bytes_done > 0)) {
		rc = xillybus_myflush(filp->private_data, 0); /* No timeout */

		if (rc && (rc != -EINTR))
			return rc;
	}

	return bytes_done;
}

static int xillybus_open(struct inode *inode, struct file *filp)
{
	int rc = 0;
	unsigned long flags;
	int minor = iminor(inode);
	int major = imajor(inode);
	struct xilly_endpoint *ep_iter, *endpoint = NULL;
	struct xilly_channel *channel;

	mutex_lock(&ep_list_lock);

	list_for_each_entry(ep_iter, &list_of_endpoints, ep_list) {
		if ((ep_iter->major == major) &&
		    (minor >= ep_iter->lowest_minor) &&
		    (minor < (ep_iter->lowest_minor +
			      ep_iter->num_channels))) {
			endpoint = ep_iter;
			break;
		}
	}
	mutex_unlock(&ep_list_lock);

	if (!endpoint) {
		pr_err("xillybus: open() failed to find a device for major=%d and minor=%d\n",
		       major, minor);
		return -ENODEV;
	}

	if (endpoint->fatal_error)
		return -EIO;

	channel = endpoint->channels[1 + minor - endpoint->lowest_minor];
	filp->private_data = channel;

	/*
	 * It gets complicated because:
	 * 1. We don't want to take a mutex we don't have to
	 * 2. We don't want to open one direction if the other will fail.
	 */

	if ((filp->f_mode & FMODE_READ) && (!channel->num_wr_buffers))
		return -ENODEV;

	if ((filp->f_mode & FMODE_WRITE) && (!channel->num_rd_buffers))
		return -ENODEV;

	if ((filp->f_mode & FMODE_READ) && (filp->f_flags & O_NONBLOCK) &&
	    (channel->wr_synchronous || !channel->wr_allow_partial ||
	     !channel->wr_supports_nonempty)) {
		dev_err(endpoint->dev,
			"open() failed: O_NONBLOCK not allowed for read on this device\n");
		return -ENODEV;
	}

	if ((filp->f_mode & FMODE_WRITE) && (filp->f_flags & O_NONBLOCK) &&
	    (channel->rd_synchronous || !channel->rd_allow_partial)) {
		dev_err(endpoint->dev,
			"open() failed: O_NONBLOCK not allowed for write on this device\n");
		return -ENODEV;
	}

	/*
	 * Note: open() may block on getting mutexes despite O_NONBLOCK.
	 * This shouldn't occur normally, since multiple open of the same
	 * file descriptor is almost always prohibited anyhow
	 * (*_exclusive_open is normally set in real-life systems).
	 */

	if (filp->f_mode & FMODE_READ) {
		rc = mutex_lock_interruptible(&channel->wr_mutex);
		if (rc)
			return rc;
	}

	if (filp->f_mode & FMODE_WRITE) {
		rc = mutex_lock_interruptible(&channel->rd_mutex);
		if (rc)
			goto unlock_wr;
	}

	if ((filp->f_mode & FMODE_READ) &&
	    (channel->wr_ref_count != 0) &&
	    (channel->wr_exclusive_open)) {
		rc = -EBUSY;
		goto unlock;
	}

	if ((filp->f_mode & FMODE_WRITE) &&
	    (channel->rd_ref_count != 0) &&
	    (channel->rd_exclusive_open)) {
		rc = -EBUSY;
		goto unlock;
	}

	if (filp->f_mode & FMODE_READ) {
		if (channel->wr_ref_count == 0) { /* First open of file */
			/* Move the host to first buffer */
			spin_lock_irqsave(&channel->wr_spinlock, flags);
			channel->wr_host_buf_idx = 0;
			channel->wr_host_buf_pos = 0;
			channel->wr_fpga_buf_idx = -1;
			channel->wr_empty = 1;
			channel->wr_ready = 0;
			channel->wr_sleepy = 1;
			channel->wr_eof = -1;
			channel->wr_hangup = 0;

			spin_unlock_irqrestore(&channel->wr_spinlock, flags);

			iowrite32(1 | (channel->chan_num << 1) |
				  (4 << 24) |  /* Opcode 4, open channel */
				  ((channel->wr_synchronous & 1) << 23),
				  channel->endpoint->registers +
				  fpga_buf_ctrl_reg);
		}

		channel->wr_ref_count++;
	}

	if (filp->f_mode & FMODE_WRITE) {
		if (channel->rd_ref_count == 0) { /* First open of file */
			/* Move the host to first buffer */
			spin_lock_irqsave(&channel->rd_spinlock, flags);
			channel->rd_host_buf_idx = 0;
			channel->rd_host_buf_pos = 0;
			channel->rd_leftovers[3] = 0; /* No leftovers. */
			channel->rd_fpga_buf_idx = channel->num_rd_buffers - 1;
			channel->rd_full = 0;

			spin_unlock_irqrestore(&channel->rd_spinlock, flags);

			iowrite32((channel->chan_num << 1) |
				  (4 << 24),   /* Opcode 4, open channel */
				  channel->endpoint->registers +
				  fpga_buf_ctrl_reg);
		}

		channel->rd_ref_count++;
	}

unlock:
	if (filp->f_mode & FMODE_WRITE)
		mutex_unlock(&channel->rd_mutex);
unlock_wr:
	if (filp->f_mode & FMODE_READ)
		mutex_unlock(&channel->wr_mutex);

	if (!rc && (!channel->seekable))
		return nonseekable_open(inode, filp);

	return rc;
}

static int xillybus_release(struct inode *inode, struct file *filp)
{
	unsigned long flags;
	struct xilly_channel *channel = filp->private_data;

	int buf_idx;
	int eof;

	if (channel->endpoint->fatal_error)
		return -EIO;

	if (filp->f_mode & FMODE_WRITE) {
		mutex_lock(&channel->rd_mutex);

		channel->rd_ref_count--;

		if (channel->rd_ref_count == 0) {
			/*
			 * We rely on the kernel calling flush()
			 * before we get here.
			 */

			iowrite32((channel->chan_num << 1) | /* Channel ID */
				  (5 << 24),  /* Opcode 5, close channel */
				  channel->endpoint->registers +
				  fpga_buf_ctrl_reg);
		}
		mutex_unlock(&channel->rd_mutex);
	}

	if (filp->f_mode & FMODE_READ) {
		mutex_lock(&channel->wr_mutex);

		channel->wr_ref_count--;

		if (channel->wr_ref_count == 0) {
			iowrite32(1 | (channel->chan_num << 1) |
				  (5 << 24),  /* Opcode 5, close channel */
				  channel->endpoint->registers +
				  fpga_buf_ctrl_reg);

			/*
			 * This is crazily cautious: We make sure that not
			 * only that we got an EOF (be it because we closed
			 * the channel or because of a user's EOF), but verify
			 * that it's one beyond the last buffer arrived, so
			 * we have no leftover buffers pending before wrapping
			 * up (which can only happen in asynchronous channels,
			 * BTW)
			 */

			while (1) {
				spin_lock_irqsave(&channel->wr_spinlock,
						  flags);
				buf_idx = channel->wr_fpga_buf_idx;
				eof = channel->wr_eof;
				channel->wr_sleepy = 1;
				spin_unlock_irqrestore(&channel->wr_spinlock,
						       flags);

				/*
				 * Check if eof points at the buffer after
				 * the last one the FPGA submitted. Note that
				 * no EOF is marked by negative eof.
				 */

				buf_idx++;
				if (buf_idx == channel->num_wr_buffers)
					buf_idx = 0;

				if (buf_idx == eof)
					break;

				/*
				 * Steal extra 100 ms if awaken by interrupt.
				 * This is a simple workaround for an
				 * interrupt pending when entering, which would
				 * otherwise result in declaring the hardware
				 * non-responsive.
				 */

				if (wait_event_interruptible(
					    channel->wr_wait,
					    (!channel->wr_sleepy)))
					msleep(100);

				if (channel->wr_sleepy) {
					mutex_unlock(&channel->wr_mutex);
					dev_warn(channel->endpoint->dev,
						 "Hardware failed to respond to close command, therefore left in messy state.\n");
					return -EINTR;
				}
			}
		}

		mutex_unlock(&channel->wr_mutex);
	}

	return 0;
}

static loff_t xillybus_llseek(struct file *filp, loff_t offset, int whence)
{
	struct xilly_channel *channel = filp->private_data;
	loff_t pos = filp->f_pos;
	int rc = 0;

	/*
	 * Take both mutexes not allowing interrupts, since it seems like
	 * common applications don't expect an -EINTR here. Besides, multiple
	 * access to a single file descriptor on seekable devices is a mess
	 * anyhow.
	 */

	if (channel->endpoint->fatal_error)
		return -EIO;

	mutex_lock(&channel->wr_mutex);
	mutex_lock(&channel->rd_mutex);

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
	if (pos & ((1 << channel->log2_element_size) - 1)) {
		rc = -EINVAL;
		goto end;
	}

	mutex_lock(&channel->endpoint->register_mutex);

	iowrite32(pos >> channel->log2_element_size,
		  channel->endpoint->registers + fpga_buf_offset_reg);

	iowrite32((channel->chan_num << 1) |
		  (6 << 24),  /* Opcode 6, set address */
		  channel->endpoint->registers + fpga_buf_ctrl_reg);

	mutex_unlock(&channel->endpoint->register_mutex);

end:
	mutex_unlock(&channel->rd_mutex);
	mutex_unlock(&channel->wr_mutex);

	if (rc) /* Return error after releasing mutexes */
		return rc;

	filp->f_pos = pos;

	/*
	 * Since seekable devices are allowed only when the channel is
	 * synchronous, we assume that there is no data pending in either
	 * direction (which holds true as long as no concurrent access on the
	 * file descriptor takes place).
	 * The only thing we may need to throw away is leftovers from partial
	 * write() flush.
	 */

	channel->rd_leftovers[3] = 0;

	return pos;
}

static unsigned int xillybus_poll(struct file *filp, poll_table *wait)
{
	struct xilly_channel *channel = filp->private_data;
	unsigned int mask = 0;
	unsigned long flags;

	poll_wait(filp, &channel->endpoint->ep_wait, wait);

	/*
	 * poll() won't play ball regarding read() channels which
	 * aren't asynchronous and support the nonempty message. Allowing
	 * that will create situations where data has been delivered at
	 * the FPGA, and users expecting select() to wake up, which it may
	 * not.
	 */

	if (!channel->wr_synchronous && channel->wr_supports_nonempty) {
		poll_wait(filp, &channel->wr_wait, wait);
		poll_wait(filp, &channel->wr_ready_wait, wait);

		spin_lock_irqsave(&channel->wr_spinlock, flags);
		if (!channel->wr_empty || channel->wr_ready)
			mask |= POLLIN | POLLRDNORM;

		if (channel->wr_hangup)
			/*
			 * Not POLLHUP, because its behavior is in the
			 * mist, and POLLIN does what we want: Wake up
			 * the read file descriptor so it sees EOF.
			 */
			mask |=  POLLIN | POLLRDNORM;
		spin_unlock_irqrestore(&channel->wr_spinlock, flags);
	}

	/*
	 * If partial data write is disallowed on a write() channel,
	 * it's pointless to ever signal OK to write, because is could
	 * block despite some space being available.
	 */

	if (channel->rd_allow_partial) {
		poll_wait(filp, &channel->rd_wait, wait);

		spin_lock_irqsave(&channel->rd_spinlock, flags);
		if (!channel->rd_full)
			mask |= POLLOUT | POLLWRNORM;
		spin_unlock_irqrestore(&channel->rd_spinlock, flags);
	}

	if (channel->endpoint->fatal_error)
		mask |= POLLERR;

	return mask;
}

static const struct file_operations xillybus_fops = {
	.owner      = THIS_MODULE,
	.read       = xillybus_read,
	.write      = xillybus_write,
	.open       = xillybus_open,
	.flush      = xillybus_flush,
	.release    = xillybus_release,
	.llseek     = xillybus_llseek,
	.poll       = xillybus_poll,
};

static int xillybus_init_chrdev(struct xilly_endpoint *endpoint,
				const unsigned char *idt)
{
	int rc;
	dev_t dev;
	int devnum, i, minor, major;
	char devname[48];
	struct device *device;

	rc = alloc_chrdev_region(&dev, 0, /* minor start */
				 endpoint->num_channels,
				 xillyname);
	if (rc) {
		dev_warn(endpoint->dev, "Failed to obtain major/minors");
		return rc;
	}

	endpoint->major = major = MAJOR(dev);
	endpoint->lowest_minor = minor = MINOR(dev);

	cdev_init(&endpoint->cdev, &xillybus_fops);
	endpoint->cdev.owner = endpoint->ephw->owner;
	rc = cdev_add(&endpoint->cdev, MKDEV(major, minor),
		      endpoint->num_channels);
	if (rc) {
		dev_warn(endpoint->dev, "Failed to add cdev. Aborting.\n");
		goto unregister_chrdev;
	}

	idt++;

	for (i = minor, devnum = 0;
	     devnum < endpoint->num_channels;
	     devnum++, i++) {
		snprintf(devname, sizeof(devname)-1, "xillybus_%s", idt);

		devname[sizeof(devname)-1] = 0; /* Should never matter */

		while (*idt++)
			/* Skip to next */;

		device = device_create(xillybus_class,
				       NULL,
				       MKDEV(major, i),
				       NULL,
				       "%s", devname);

		if (IS_ERR(device)) {
			dev_warn(endpoint->dev,
				 "Failed to create %s device. Aborting.\n",
				 devname);
			rc = -ENODEV;
			goto unroll_device_create;
		}
	}

	dev_info(endpoint->dev, "Created %d device files.\n",
		 endpoint->num_channels);
	return 0; /* succeed */

unroll_device_create:
	devnum--; i--;
	for (; devnum >= 0; devnum--, i--)
		device_destroy(xillybus_class, MKDEV(major, i));

	cdev_del(&endpoint->cdev);
unregister_chrdev:
	unregister_chrdev_region(MKDEV(major, minor), endpoint->num_channels);

	return rc;
}

static void xillybus_cleanup_chrdev(struct xilly_endpoint *endpoint)
{
	int minor;

	for (minor = endpoint->lowest_minor;
	     minor < (endpoint->lowest_minor + endpoint->num_channels);
	     minor++)
		device_destroy(xillybus_class, MKDEV(endpoint->major, minor));
	cdev_del(&endpoint->cdev);
	unregister_chrdev_region(MKDEV(endpoint->major,
				       endpoint->lowest_minor),
				 endpoint->num_channels);

	dev_info(endpoint->dev, "Removed %d device files.\n",
		 endpoint->num_channels);
}

struct xilly_endpoint *xillybus_init_endpoint(struct pci_dev *pdev,
					      struct device *dev,
					      struct xilly_endpoint_hardware
					      *ephw)
{
	struct xilly_endpoint *endpoint;

	endpoint = devm_kzalloc(dev, sizeof(*endpoint), GFP_KERNEL);
	if (!endpoint)
		return NULL;

	endpoint->pdev = pdev;
	endpoint->dev = dev;
	endpoint->ephw = ephw;
	endpoint->msg_counter = 0x0b;
	endpoint->failed_messages = 0;
	endpoint->fatal_error = 0;

	init_waitqueue_head(&endpoint->ep_wait);
	mutex_init(&endpoint->register_mutex);

	return endpoint;
}
EXPORT_SYMBOL(xillybus_init_endpoint);

static int xilly_quiesce(struct xilly_endpoint *endpoint)
{
	long t;

	endpoint->idtlen = -1;

	iowrite32((u32) (endpoint->dma_using_dac & 0x0001),
		  endpoint->registers + fpga_dma_control_reg);

	t = wait_event_interruptible_timeout(endpoint->ep_wait,
					     (endpoint->idtlen >= 0),
					     XILLY_TIMEOUT);
	if (t <= 0) {
		dev_err(endpoint->dev,
			"Failed to quiesce the device on exit.\n");
		return -ENODEV;
	}
	return 0;
}

int xillybus_endpoint_discovery(struct xilly_endpoint *endpoint)
{
	int rc;
	long t;

	void *bootstrap_resources;
	int idtbuffersize = (1 << PAGE_SHIFT);
	struct device *dev = endpoint->dev;

	/*
	 * The bogus IDT is used during bootstrap for allocating the initial
	 * message buffer, and then the message buffer and space for the IDT
	 * itself. The initial message buffer is of a single page's size, but
	 * it's soon replaced with a more modest one (and memory is freed).
	 */

	unsigned char bogus_idt[8] = { 1, 224, (PAGE_SHIFT)-2, 0,
				       3, 192, PAGE_SHIFT, 0 };
	struct xilly_idt_handle idt_handle;

	/*
	 * Writing the value 0x00000001 to Endianness register signals which
	 * endianness this processor is using, so the FPGA can swap words as
	 * necessary.
	 */

	iowrite32(1, endpoint->registers + fpga_endian_reg);

	/* Bootstrap phase I: Allocate temporary message buffer */

	bootstrap_resources = devres_open_group(dev, NULL, GFP_KERNEL);
	if (!bootstrap_resources)
		return -ENOMEM;

	endpoint->num_channels = 0;

	rc = xilly_setupchannels(endpoint, bogus_idt, 1);
	if (rc)
		return rc;

	/* Clear the message subsystem (and counter in particular) */
	iowrite32(0x04, endpoint->registers + fpga_msg_ctrl_reg);

	endpoint->idtlen = -1;

	/*
	 * Set DMA 32/64 bit mode, quiesce the device (?!) and get IDT
	 * buffer size.
	 */
	iowrite32((u32) (endpoint->dma_using_dac & 0x0001),
		  endpoint->registers + fpga_dma_control_reg);

	t = wait_event_interruptible_timeout(endpoint->ep_wait,
					     (endpoint->idtlen >= 0),
					     XILLY_TIMEOUT);
	if (t <= 0) {
		dev_err(endpoint->dev, "No response from FPGA. Aborting.\n");
		return -ENODEV;
	}

	/* Enable DMA */
	iowrite32((u32) (0x0002 | (endpoint->dma_using_dac & 0x0001)),
		  endpoint->registers + fpga_dma_control_reg);

	/* Bootstrap phase II: Allocate buffer for IDT and obtain it */
	while (endpoint->idtlen >= idtbuffersize) {
		idtbuffersize *= 2;
		bogus_idt[6]++;
	}

	endpoint->num_channels = 1;

	rc = xilly_setupchannels(endpoint, bogus_idt, 2);
	if (rc)
		goto failed_idt;

	rc = xilly_obtain_idt(endpoint);
	if (rc)
		goto failed_idt;

	rc = xilly_scan_idt(endpoint, &idt_handle);
	if (rc)
		goto failed_idt;

	devres_close_group(dev, bootstrap_resources);

	/* Bootstrap phase III: Allocate buffers according to IDT */

	rc = xilly_setupchannels(endpoint,
				 idt_handle.chandesc,
				 idt_handle.entries);
	if (rc)
		goto failed_idt;

	/*
	 * endpoint is now completely configured. We put it on the list
	 * available to open() before registering the char device(s)
	 */

	mutex_lock(&ep_list_lock);
	list_add_tail(&endpoint->ep_list, &list_of_endpoints);
	mutex_unlock(&ep_list_lock);

	rc = xillybus_init_chrdev(endpoint, idt_handle.idt);
	if (rc)
		goto failed_chrdevs;

	devres_release_group(dev, bootstrap_resources);

	return 0;

failed_chrdevs:
	mutex_lock(&ep_list_lock);
	list_del(&endpoint->ep_list);
	mutex_unlock(&ep_list_lock);

failed_idt:
	xilly_quiesce(endpoint);
	flush_workqueue(xillybus_wq);

	return rc;
}
EXPORT_SYMBOL(xillybus_endpoint_discovery);

void xillybus_endpoint_remove(struct xilly_endpoint *endpoint)
{
	xillybus_cleanup_chrdev(endpoint);

	mutex_lock(&ep_list_lock);
	list_del(&endpoint->ep_list);
	mutex_unlock(&ep_list_lock);

	xilly_quiesce(endpoint);

	/*
	 * Flushing is done upon endpoint release to prevent access to memory
	 * just about to be released. This makes the quiesce complete.
	 */
	flush_workqueue(xillybus_wq);
}
EXPORT_SYMBOL(xillybus_endpoint_remove);

static int __init xillybus_init(void)
{
	mutex_init(&ep_list_lock);

	xillybus_class = class_create(THIS_MODULE, xillyname);
	if (IS_ERR(xillybus_class))
		return PTR_ERR(xillybus_class);

	xillybus_wq = alloc_workqueue(xillyname, 0, 0);
	if (!xillybus_wq) {
		class_destroy(xillybus_class);
		return -ENOMEM;
	}

	return 0;
}

static void __exit xillybus_exit(void)
{
	/* flush_workqueue() was called for each endpoint released */
	destroy_workqueue(xillybus_wq);

	class_destroy(xillybus_class);
}

module_init(xillybus_init);
module_exit(xillybus_exit);
