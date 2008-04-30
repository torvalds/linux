/*
 *  drivers/s390/char/sclp_vt220.c
 *    SCLP VT220 terminal driver.
 *
 *  S390 version
 *    Copyright IBM Corp. 2003,2008
 *    Author(s): Peter Oberparleiter <Peter.Oberparleiter@de.ibm.com>
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/kdev_t.h>
#include <linux/bootmem.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include "sclp.h"

#define SCLP_VT220_PRINT_HEADER 	"sclp vt220 tty driver: "
#define SCLP_VT220_MAJOR		TTY_MAJOR
#define SCLP_VT220_MINOR		65
#define SCLP_VT220_DRIVER_NAME		"sclp_vt220"
#define SCLP_VT220_DEVICE_NAME		"ttysclp"
#define SCLP_VT220_CONSOLE_NAME		"ttyS"
#define SCLP_VT220_CONSOLE_INDEX	1	/* console=ttyS1 */
#define SCLP_VT220_BUF_SIZE		80

/* Representation of a single write request */
struct sclp_vt220_request {
	struct list_head list;
	struct sclp_req sclp_req;
	int retry_count;
};

/* VT220 SCCB */
struct sclp_vt220_sccb {
	struct sccb_header header;
	struct evbuf_header evbuf;
};

#define SCLP_VT220_MAX_CHARS_PER_BUFFER	(PAGE_SIZE - \
					 sizeof(struct sclp_vt220_request) - \
					 sizeof(struct sclp_vt220_sccb))

/* Structures and data needed to register tty driver */
static struct tty_driver *sclp_vt220_driver;

/* The tty_struct that the kernel associated with us */
static struct tty_struct *sclp_vt220_tty;

/* Lock to protect internal data from concurrent access */
static spinlock_t sclp_vt220_lock;

/* List of empty pages to be used as write request buffers */
static struct list_head sclp_vt220_empty;

/* List of pending requests */
static struct list_head sclp_vt220_outqueue;

/* Number of requests in outqueue */
static int sclp_vt220_outqueue_count;

/* Wait queue used to delay write requests while we've run out of buffers */
static wait_queue_head_t sclp_vt220_waitq;

/* Timer used for delaying write requests to merge subsequent messages into
 * a single buffer */
static struct timer_list sclp_vt220_timer;

/* Pointer to current request buffer which has been partially filled but not
 * yet sent */
static struct sclp_vt220_request *sclp_vt220_current_request;

/* Number of characters in current request buffer */
static int sclp_vt220_buffered_chars;

/* Flag indicating whether this driver has already been initialized */
static int sclp_vt220_initialized = 0;

/* Flag indicating that sclp_vt220_current_request should really
 * have been already queued but wasn't because the SCLP was processing
 * another buffer */
static int sclp_vt220_flush_later;

static void sclp_vt220_receiver_fn(struct evbuf_header *evbuf);
static int __sclp_vt220_emit(struct sclp_vt220_request *request);
static void sclp_vt220_emit_current(void);

/* Registration structure for our interest in SCLP event buffers */
static struct sclp_register sclp_vt220_register = {
	.send_mask		= EVTYP_VT220MSG_MASK,
	.receive_mask		= EVTYP_VT220MSG_MASK,
	.state_change_fn	= NULL,
	.receiver_fn		= sclp_vt220_receiver_fn
};


/*
 * Put provided request buffer back into queue and check emit pending
 * buffers if necessary.
 */
static void
sclp_vt220_process_queue(struct sclp_vt220_request *request)
{
	unsigned long flags;
	void *page;

	do {
		/* Put buffer back to list of empty buffers */
		page = request->sclp_req.sccb;
		spin_lock_irqsave(&sclp_vt220_lock, flags);
		/* Move request from outqueue to empty queue */
		list_del(&request->list);
		sclp_vt220_outqueue_count--;
		list_add_tail((struct list_head *) page, &sclp_vt220_empty);
		/* Check if there is a pending buffer on the out queue. */
		request = NULL;
		if (!list_empty(&sclp_vt220_outqueue))
			request = list_entry(sclp_vt220_outqueue.next,
					     struct sclp_vt220_request, list);
		spin_unlock_irqrestore(&sclp_vt220_lock, flags);
	} while (request && __sclp_vt220_emit(request));
	if (request == NULL && sclp_vt220_flush_later)
		sclp_vt220_emit_current();
	wake_up(&sclp_vt220_waitq);
	/* Check if the tty needs a wake up call */
	if (sclp_vt220_tty != NULL) {
		tty_wakeup(sclp_vt220_tty);
	}
}

#define SCLP_BUFFER_MAX_RETRY		1

/*
 * Callback through which the result of a write request is reported by the
 * SCLP.
 */
static void
sclp_vt220_callback(struct sclp_req *request, void *data)
{
	struct sclp_vt220_request *vt220_request;
	struct sclp_vt220_sccb *sccb;

	vt220_request = (struct sclp_vt220_request *) data;
	if (request->status == SCLP_REQ_FAILED) {
		sclp_vt220_process_queue(vt220_request);
		return;
	}
	sccb = (struct sclp_vt220_sccb *) vt220_request->sclp_req.sccb;

	/* Check SCLP response code and choose suitable action	*/
	switch (sccb->header.response_code) {
	case 0x0020 :
		break;

	case 0x05f0: /* Target resource in improper state */
		break;

	case 0x0340: /* Contained SCLP equipment check */
		if (++vt220_request->retry_count > SCLP_BUFFER_MAX_RETRY)
			break;
		/* Remove processed buffers and requeue rest */
		if (sclp_remove_processed((struct sccb_header *) sccb) > 0) {
			/* Not all buffers were processed */
			sccb->header.response_code = 0x0000;
			vt220_request->sclp_req.status = SCLP_REQ_FILLED;
			if (sclp_add_request(request) == 0)
				return;
		}
		break;

	case 0x0040: /* SCLP equipment check */
		if (++vt220_request->retry_count > SCLP_BUFFER_MAX_RETRY)
			break;
		sccb->header.response_code = 0x0000;
		vt220_request->sclp_req.status = SCLP_REQ_FILLED;
		if (sclp_add_request(request) == 0)
			return;
		break;

	default:
		break;
	}
	sclp_vt220_process_queue(vt220_request);
}

/*
 * Emit vt220 request buffer to SCLP. Return zero on success, non-zero
 * otherwise.
 */
static int
__sclp_vt220_emit(struct sclp_vt220_request *request)
{
	if (!(sclp_vt220_register.sclp_receive_mask & EVTYP_VT220MSG_MASK)) {
		request->sclp_req.status = SCLP_REQ_FAILED;
		return -EIO;
	}
	request->sclp_req.command = SCLP_CMDW_WRITE_EVENT_DATA;
	request->sclp_req.status = SCLP_REQ_FILLED;
	request->sclp_req.callback = sclp_vt220_callback;
	request->sclp_req.callback_data = (void *) request;

	return sclp_add_request(&request->sclp_req);
}

/*
 * Queue and emit given request.
 */
static void
sclp_vt220_emit(struct sclp_vt220_request *request)
{
	unsigned long flags;
	int count;

	spin_lock_irqsave(&sclp_vt220_lock, flags);
	list_add_tail(&request->list, &sclp_vt220_outqueue);
	count = sclp_vt220_outqueue_count++;
	spin_unlock_irqrestore(&sclp_vt220_lock, flags);
	/* Emit only the first buffer immediately - callback takes care of
	 * the rest */
	if (count == 0 && __sclp_vt220_emit(request))
		sclp_vt220_process_queue(request);
}

/*
 * Queue and emit current request. Return zero on success, non-zero otherwise.
 */
static void
sclp_vt220_emit_current(void)
{
	unsigned long flags;
	struct sclp_vt220_request *request;
	struct sclp_vt220_sccb *sccb;

	spin_lock_irqsave(&sclp_vt220_lock, flags);
	request = NULL;
	if (sclp_vt220_current_request != NULL) {
		sccb = (struct sclp_vt220_sccb *) 
				sclp_vt220_current_request->sclp_req.sccb;
		/* Only emit buffers with content */
		if (sccb->header.length != sizeof(struct sclp_vt220_sccb)) {
			request = sclp_vt220_current_request;
			sclp_vt220_current_request = NULL;
			if (timer_pending(&sclp_vt220_timer))
				del_timer(&sclp_vt220_timer);
		}
		sclp_vt220_flush_later = 0;
	}
	spin_unlock_irqrestore(&sclp_vt220_lock, flags);
	if (request != NULL)
		sclp_vt220_emit(request);
}

#define SCLP_NORMAL_WRITE	0x00

/*
 * Helper function to initialize a page with the sclp request structure.
 */
static struct sclp_vt220_request *
sclp_vt220_initialize_page(void *page)
{
	struct sclp_vt220_request *request;
	struct sclp_vt220_sccb *sccb;

	/* Place request structure at end of page */
	request = ((struct sclp_vt220_request *)
			((addr_t) page + PAGE_SIZE)) - 1;
	request->retry_count = 0;
	request->sclp_req.sccb = page;
	/* SCCB goes at start of page */
	sccb = (struct sclp_vt220_sccb *) page;
	memset((void *) sccb, 0, sizeof(struct sclp_vt220_sccb));
	sccb->header.length = sizeof(struct sclp_vt220_sccb);
	sccb->header.function_code = SCLP_NORMAL_WRITE;
	sccb->header.response_code = 0x0000;
	sccb->evbuf.type = EVTYP_VT220MSG;
	sccb->evbuf.length = sizeof(struct evbuf_header);

	return request;
}

static inline unsigned int
sclp_vt220_space_left(struct sclp_vt220_request *request)
{
	struct sclp_vt220_sccb *sccb;
	sccb = (struct sclp_vt220_sccb *) request->sclp_req.sccb;
	return PAGE_SIZE - sizeof(struct sclp_vt220_request) -
	       sccb->header.length;
}

static inline unsigned int
sclp_vt220_chars_stored(struct sclp_vt220_request *request)
{
	struct sclp_vt220_sccb *sccb;
	sccb = (struct sclp_vt220_sccb *) request->sclp_req.sccb;
	return sccb->evbuf.length - sizeof(struct evbuf_header);
}

/*
 * Add msg to buffer associated with request. Return the number of characters
 * added.
 */
static int
sclp_vt220_add_msg(struct sclp_vt220_request *request,
		   const unsigned char *msg, int count, int convertlf)
{
	struct sclp_vt220_sccb *sccb;
	void *buffer;
	unsigned char c;
	int from;
	int to;

	if (count > sclp_vt220_space_left(request))
		count = sclp_vt220_space_left(request);
	if (count <= 0)
		return 0;

	sccb = (struct sclp_vt220_sccb *) request->sclp_req.sccb;
	buffer = (void *) ((addr_t) sccb + sccb->header.length);

	if (convertlf) {
		/* Perform Linefeed conversion (0x0a -> 0x0a 0x0d)*/
		for (from=0, to=0;
		     (from < count) && (to < sclp_vt220_space_left(request));
		     from++) {
			/* Retrieve character */
			c = msg[from];
			/* Perform conversion */
			if (c == 0x0a) {
				if (to + 1 < sclp_vt220_space_left(request)) {
					((unsigned char *) buffer)[to++] = c;
					((unsigned char *) buffer)[to++] = 0x0d;
				} else
					break;

			} else
				((unsigned char *) buffer)[to++] = c;
		}
		sccb->header.length += to;
		sccb->evbuf.length += to;
		return from;
	} else {
		memcpy(buffer, (const void *) msg, count);
		sccb->header.length += count;
		sccb->evbuf.length += count;
		return count;
	}
}

/*
 * Emit buffer after having waited long enough for more data to arrive.
 */
static void
sclp_vt220_timeout(unsigned long data)
{
	sclp_vt220_emit_current();
}

#define BUFFER_MAX_DELAY	HZ/20

/* 
 * Internal implementation of the write function. Write COUNT bytes of data
 * from memory at BUF
 * to the SCLP interface. In case that the data does not fit into the current
 * write buffer, emit the current one and allocate a new one. If there are no
 * more empty buffers available, wait until one gets emptied. If DO_SCHEDULE
 * is non-zero, the buffer will be scheduled for emitting after a timeout -
 * otherwise the user has to explicitly call the flush function.
 * A non-zero CONVERTLF parameter indicates that 0x0a characters in the message
 * buffer should be converted to 0x0a 0x0d. After completion, return the number
 * of bytes written.
 */
static int
__sclp_vt220_write(const unsigned char *buf, int count, int do_schedule,
		   int convertlf, int may_schedule)
{
	unsigned long flags;
	void *page;
	int written;
	int overall_written;

	if (count <= 0)
		return 0;
	overall_written = 0;
	spin_lock_irqsave(&sclp_vt220_lock, flags);
	do {
		/* Create a sclp output buffer if none exists yet */
		if (sclp_vt220_current_request == NULL) {
			while (list_empty(&sclp_vt220_empty)) {
				spin_unlock_irqrestore(&sclp_vt220_lock, flags);
				if (in_interrupt() || !may_schedule)
					sclp_sync_wait();
				else
					wait_event(sclp_vt220_waitq,
						!list_empty(&sclp_vt220_empty));
				spin_lock_irqsave(&sclp_vt220_lock, flags);
			}
			page = (void *) sclp_vt220_empty.next;
			list_del((struct list_head *) page);
			sclp_vt220_current_request =
				sclp_vt220_initialize_page(page);
		}
		/* Try to write the string to the current request buffer */
		written = sclp_vt220_add_msg(sclp_vt220_current_request,
					     buf, count, convertlf);
		overall_written += written;
		if (written == count)
			break;
		/*
		 * Not all characters could be written to the current
		 * output buffer. Emit the buffer, create a new buffer
		 * and then output the rest of the string.
		 */
		spin_unlock_irqrestore(&sclp_vt220_lock, flags);
		sclp_vt220_emit_current();
		spin_lock_irqsave(&sclp_vt220_lock, flags);
		buf += written;
		count -= written;
	} while (count > 0);
	/* Setup timer to output current console buffer after some time */
	if (sclp_vt220_current_request != NULL &&
	    !timer_pending(&sclp_vt220_timer) && do_schedule) {
		sclp_vt220_timer.function = sclp_vt220_timeout;
		sclp_vt220_timer.data = 0UL;
		sclp_vt220_timer.expires = jiffies + BUFFER_MAX_DELAY;
		add_timer(&sclp_vt220_timer);
	}
	spin_unlock_irqrestore(&sclp_vt220_lock, flags);
	return overall_written;
}

/*
 * This routine is called by the kernel to write a series of
 * characters to the tty device.  The characters may come from
 * user space or kernel space.  This routine will return the
 * number of characters actually accepted for writing.
 */
static int
sclp_vt220_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	return __sclp_vt220_write(buf, count, 1, 0, 1);
}

#define SCLP_VT220_SESSION_ENDED	0x01
#define	SCLP_VT220_SESSION_STARTED	0x80
#define SCLP_VT220_SESSION_DATA		0x00

/*
 * Called by the SCLP to report incoming event buffers.
 */
static void
sclp_vt220_receiver_fn(struct evbuf_header *evbuf)
{
	char *buffer;
	unsigned int count;

	/* Ignore input if device is not open */
	if (sclp_vt220_tty == NULL)
		return;

	buffer = (char *) ((addr_t) evbuf + sizeof(struct evbuf_header));
	count = evbuf->length - sizeof(struct evbuf_header);

	switch (*buffer) {
	case SCLP_VT220_SESSION_ENDED:
	case SCLP_VT220_SESSION_STARTED:
		break;
	case SCLP_VT220_SESSION_DATA:
		/* Send input to line discipline */
		buffer++;
		count--;
		tty_insert_flip_string(sclp_vt220_tty, buffer, count);
		tty_flip_buffer_push(sclp_vt220_tty);
		break;
	}
}

/*
 * This routine is called when a particular tty device is opened.
 */
static int
sclp_vt220_open(struct tty_struct *tty, struct file *filp)
{
	if (tty->count == 1) {
		sclp_vt220_tty = tty;
		tty->driver_data = kmalloc(SCLP_VT220_BUF_SIZE, GFP_KERNEL);
		if (tty->driver_data == NULL)
			return -ENOMEM;
		tty->low_latency = 0;
	}
	return 0;
}

/*
 * This routine is called when a particular tty device is closed.
 */
static void
sclp_vt220_close(struct tty_struct *tty, struct file *filp)
{
	if (tty->count == 1) {
		sclp_vt220_tty = NULL;
		kfree(tty->driver_data);
		tty->driver_data = NULL;
	}
}

/*
 * This routine is called by the kernel to write a single
 * character to the tty device.  If the kernel uses this routine,
 * it must call the flush_chars() routine (if defined) when it is
 * done stuffing characters into the driver.
 *
 * NOTE: include/linux/tty_driver.h specifies that a character should be
 * ignored if there is no room in the queue. This driver implements a different
 * semantic in that it will block when there is no more room left.
 */
static void
sclp_vt220_put_char(struct tty_struct *tty, unsigned char ch)
{
	__sclp_vt220_write(&ch, 1, 0, 0, 1);
}

/*
 * This routine is called by the kernel after it has written a
 * series of characters to the tty device using put_char().  
 */
static void
sclp_vt220_flush_chars(struct tty_struct *tty)
{
	if (sclp_vt220_outqueue_count == 0)
		sclp_vt220_emit_current();
	else
		sclp_vt220_flush_later = 1;
}

/*
 * This routine returns the numbers of characters the tty driver
 * will accept for queuing to be written.  This number is subject
 * to change as output buffers get emptied, or if the output flow
 * control is acted.
 */
static int
sclp_vt220_write_room(struct tty_struct *tty)
{
	unsigned long flags;
	struct list_head *l;
	int count;

	spin_lock_irqsave(&sclp_vt220_lock, flags);
	count = 0;
	if (sclp_vt220_current_request != NULL)
		count = sclp_vt220_space_left(sclp_vt220_current_request);
	list_for_each(l, &sclp_vt220_empty)
		count += SCLP_VT220_MAX_CHARS_PER_BUFFER;
	spin_unlock_irqrestore(&sclp_vt220_lock, flags);
	return count;
}

/*
 * Return number of buffered chars.
 */
static int
sclp_vt220_chars_in_buffer(struct tty_struct *tty)
{
	unsigned long flags;
	struct list_head *l;
	struct sclp_vt220_request *r;
	int count;

	spin_lock_irqsave(&sclp_vt220_lock, flags);
	count = 0;
	if (sclp_vt220_current_request != NULL)
		count = sclp_vt220_chars_stored(sclp_vt220_current_request);
	list_for_each(l, &sclp_vt220_outqueue) {
		r = list_entry(l, struct sclp_vt220_request, list);
		count += sclp_vt220_chars_stored(r);
	}
	spin_unlock_irqrestore(&sclp_vt220_lock, flags);
	return count;
}

static void
__sclp_vt220_flush_buffer(void)
{
	unsigned long flags;

	sclp_vt220_emit_current();
	spin_lock_irqsave(&sclp_vt220_lock, flags);
	if (timer_pending(&sclp_vt220_timer))
		del_timer(&sclp_vt220_timer);
	while (sclp_vt220_outqueue_count > 0) {
		spin_unlock_irqrestore(&sclp_vt220_lock, flags);
		sclp_sync_wait();
		spin_lock_irqsave(&sclp_vt220_lock, flags);
	}
	spin_unlock_irqrestore(&sclp_vt220_lock, flags);
}

/*
 * Pass on all buffers to the hardware. Return only when there are no more
 * buffers pending.
 */
static void
sclp_vt220_flush_buffer(struct tty_struct *tty)
{
	sclp_vt220_emit_current();
}

/*
 * Initialize all relevant components and register driver with system.
 */
static void __init __sclp_vt220_cleanup(void)
{
	struct list_head *page, *p;

	list_for_each_safe(page, p, &sclp_vt220_empty) {
		list_del(page);
		if (slab_is_available())
			free_page((unsigned long) page);
		else
			free_bootmem((unsigned long) page, PAGE_SIZE);
	}
	if (!list_empty(&sclp_vt220_register.list))
		sclp_unregister(&sclp_vt220_register);
	sclp_vt220_initialized = 0;
}

static int __init __sclp_vt220_init(void)
{
	void *page;
	int i;
	int num_pages;
	int rc;

	if (sclp_vt220_initialized)
		return 0;
	sclp_vt220_initialized = 1;
	spin_lock_init(&sclp_vt220_lock);
	INIT_LIST_HEAD(&sclp_vt220_empty);
	INIT_LIST_HEAD(&sclp_vt220_outqueue);
	init_waitqueue_head(&sclp_vt220_waitq);
	init_timer(&sclp_vt220_timer);
	sclp_vt220_current_request = NULL;
	sclp_vt220_buffered_chars = 0;
	sclp_vt220_outqueue_count = 0;
	sclp_vt220_tty = NULL;
	sclp_vt220_flush_later = 0;

	/* Allocate pages for output buffering */
	num_pages = slab_is_available() ? MAX_KMEM_PAGES : MAX_CONSOLE_PAGES;
	for (i = 0; i < num_pages; i++) {
		if (slab_is_available())
			page = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
		else
			page = alloc_bootmem_low_pages(PAGE_SIZE);
		if (!page) {
			__sclp_vt220_cleanup();
			return -ENOMEM;
		}
		list_add_tail((struct list_head *) page, &sclp_vt220_empty);
	}
	rc = sclp_register(&sclp_vt220_register);
	if (rc) {
		printk(KERN_ERR SCLP_VT220_PRINT_HEADER
		       "could not register vt220 - "
		       "sclp_register returned %d\n", rc);
		__sclp_vt220_cleanup();
	}
	return rc;
}

static const struct tty_operations sclp_vt220_ops = {
	.open = sclp_vt220_open,
	.close = sclp_vt220_close,
	.write = sclp_vt220_write,
	.put_char = sclp_vt220_put_char,
	.flush_chars = sclp_vt220_flush_chars,
	.write_room = sclp_vt220_write_room,
	.chars_in_buffer = sclp_vt220_chars_in_buffer,
	.flush_buffer = sclp_vt220_flush_buffer,
};

/*
 * Register driver with SCLP and Linux and initialize internal tty structures.
 */
static int __init sclp_vt220_tty_init(void)
{
	struct tty_driver *driver;
	int rc;
	int cleanup;

	/* Note: we're not testing for CONSOLE_IS_SCLP here to preserve
	 * symmetry between VM and LPAR systems regarding ttyS1. */
	driver = alloc_tty_driver(1);
	if (!driver)
		return -ENOMEM;
	cleanup = !sclp_vt220_initialized;
	rc = __sclp_vt220_init();
	if (rc)
		goto out_driver;

	driver->owner = THIS_MODULE;
	driver->driver_name = SCLP_VT220_DRIVER_NAME;
	driver->name = SCLP_VT220_DEVICE_NAME;
	driver->major = SCLP_VT220_MAJOR;
	driver->minor_start = SCLP_VT220_MINOR;
	driver->type = TTY_DRIVER_TYPE_SYSTEM;
	driver->subtype = SYSTEM_TYPE_TTY;
	driver->init_termios = tty_std_termios;
	driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(driver, &sclp_vt220_ops);

	rc = tty_register_driver(driver);
	if (rc) {
		printk(KERN_ERR SCLP_VT220_PRINT_HEADER
		       "could not register tty - "
		       "tty_register_driver returned %d\n", rc);
		goto out_init;
	}
	sclp_vt220_driver = driver;
	return 0;

out_init:
	if (cleanup)
		__sclp_vt220_cleanup();
out_driver:
	put_tty_driver(driver);
	return rc;
}
__initcall(sclp_vt220_tty_init);

#ifdef CONFIG_SCLP_VT220_CONSOLE

static void
sclp_vt220_con_write(struct console *con, const char *buf, unsigned int count)
{
	__sclp_vt220_write((const unsigned char *) buf, count, 1, 1, 0);
}

static struct tty_driver *
sclp_vt220_con_device(struct console *c, int *index)
{
	*index = 0;
	return sclp_vt220_driver;
}

/*
 * This routine is called from panic when the kernel is going to give up.
 * We have to make sure that all buffers will be flushed to the SCLP.
 * Note that this function may be called from within an interrupt context.
 */
static void
sclp_vt220_con_unblank(void)
{
	__sclp_vt220_flush_buffer();
}

/* Structure needed to register with printk */
static struct console sclp_vt220_console =
{
	.name = SCLP_VT220_CONSOLE_NAME,
	.write = sclp_vt220_con_write,
	.device = sclp_vt220_con_device,
	.unblank = sclp_vt220_con_unblank,
	.flags = CON_PRINTBUFFER,
	.index = SCLP_VT220_CONSOLE_INDEX
};

static int __init
sclp_vt220_con_init(void)
{
	int rc;

	if (!CONSOLE_IS_SCLP)
		return 0;
	rc = __sclp_vt220_init();
	if (rc)
		return rc;
	/* Attach linux console */
	register_console(&sclp_vt220_console);
	return 0;
}

console_initcall(sclp_vt220_con_init);
#endif /* CONFIG_SCLP_VT220_CONSOLE */

