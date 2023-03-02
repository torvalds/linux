// SPDX-License-Identifier: GPL-2.0
/*
 * 3215 line mode terminal driver.
 *
 * Copyright IBM Corp. 1999, 2009
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *
 * Updated:
 *  Aug-2000: Added tab support
 *	      Dan Morrison, IBM Corporation <dmorriso@cse.buffalo.edu>
 */

#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/vt_kern.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/panic_notifier.h>
#include <linux/reboot.h>
#include <linux/serial.h> /* ASYNC_* flags */
#include <linux/slab.h>
#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/io.h>
#include <asm/ebcdic.h>
#include <linux/uaccess.h>
#include <asm/delay.h>
#include <asm/cpcmd.h>
#include <asm/setup.h>

#include "ctrlchar.h"

#define NR_3215		    1
#define NR_3215_REQ	    (4*NR_3215)
#define RAW3215_BUFFER_SIZE 65536     /* output buffer size */
#define RAW3215_INBUF_SIZE  256	      /* input buffer size */
#define RAW3215_MIN_SPACE   128	      /* minimum free space for wakeup */
#define RAW3215_MIN_WRITE   1024      /* min. length for immediate output */
#define RAW3215_MAX_BYTES   3968      /* max. bytes to write with one ssch */
#define RAW3215_MAX_NEWLINE 50	      /* max. lines to write with one ssch */
#define RAW3215_NR_CCWS	    3
#define RAW3215_TIMEOUT	    HZ/10     /* time for delayed output */

#define RAW3215_FIXED	    1	      /* 3215 console device is not be freed */
#define RAW3215_WORKING	    4	      /* set if a request is being worked on */
#define RAW3215_THROTTLED   8	      /* set if reading is disabled */
#define RAW3215_STOPPED	    16	      /* set if writing is disabled */
#define RAW3215_TIMER_RUNS  64	      /* set if the output delay timer is on */
#define RAW3215_FLUSHING    128	      /* set to flush buffer (no delay) */

#define TAB_STOP_SIZE	    8	      /* tab stop size */

/*
 * Request types for a 3215 device
 */
enum raw3215_type {
	RAW3215_FREE, RAW3215_READ, RAW3215_WRITE
};

/*
 * Request structure for a 3215 device
 */
struct raw3215_req {
	enum raw3215_type type;	      /* type of the request */
	int start, len;		      /* start index & len in output buffer */
	int delayable;		      /* indication to wait for more data */
	int residual;		      /* residual count for read request */
	struct ccw1 ccws[RAW3215_NR_CCWS]; /* space for the channel program */
	struct raw3215_info *info;    /* pointer to main structure */
	struct raw3215_req *next;     /* pointer to next request */
} __attribute__ ((aligned(8)));

struct raw3215_info {
	struct tty_port port;
	struct ccw_device *cdev;      /* device for tty driver */
	spinlock_t *lock;	      /* pointer to irq lock */
	int flags;		      /* state flags */
	char *buffer;		      /* pointer to output buffer */
	char *inbuf;		      /* pointer to input buffer */
	int head;		      /* first free byte in output buffer */
	int count;		      /* number of bytes in output buffer */
	int written;		      /* number of bytes in write requests */
	struct raw3215_req *queued_read; /* pointer to queued read requests */
	struct raw3215_req *queued_write;/* pointer to queued write requests */
	wait_queue_head_t empty_wait; /* wait queue for flushing */
	struct timer_list timer;      /* timer for delayed output */
	int line_pos;		      /* position on the line (for tabs) */
	char ubuffer[80];	      /* copy_from_user buffer */
};

/* array of 3215 devices structures */
static struct raw3215_info *raw3215[NR_3215];
/* spinlock to protect the raw3215 array */
static DEFINE_SPINLOCK(raw3215_device_lock);
/* list of free request structures */
static struct raw3215_req *raw3215_freelist;
/* spinlock to protect free list */
static DEFINE_SPINLOCK(raw3215_freelist_lock);

static struct tty_driver *tty3215_driver;
static bool con3215_drop = true;

/*
 * Get a request structure from the free list
 */
static inline struct raw3215_req *raw3215_alloc_req(void)
{
	struct raw3215_req *req;
	unsigned long flags;

	spin_lock_irqsave(&raw3215_freelist_lock, flags);
	req = raw3215_freelist;
	raw3215_freelist = req->next;
	spin_unlock_irqrestore(&raw3215_freelist_lock, flags);
	return req;
}

/*
 * Put a request structure back to the free list
 */
static inline void raw3215_free_req(struct raw3215_req *req)
{
	unsigned long flags;

	if (req->type == RAW3215_FREE)
		return;		/* don't free a free request */
	req->type = RAW3215_FREE;
	spin_lock_irqsave(&raw3215_freelist_lock, flags);
	req->next = raw3215_freelist;
	raw3215_freelist = req;
	spin_unlock_irqrestore(&raw3215_freelist_lock, flags);
}

/*
 * Set up a read request that reads up to 160 byte from the 3215 device.
 * If there is a queued read request it is used, but that shouldn't happen
 * because a 3215 terminal won't accept a new read before the old one is
 * completed.
 */
static void raw3215_mk_read_req(struct raw3215_info *raw)
{
	struct raw3215_req *req;
	struct ccw1 *ccw;

	/* there can only be ONE read request at a time */
	req = raw->queued_read;
	if (req == NULL) {
		/* no queued read request, use new req structure */
		req = raw3215_alloc_req();
		req->type = RAW3215_READ;
		req->info = raw;
		raw->queued_read = req;
	}

	ccw = req->ccws;
	ccw->cmd_code = 0x0A; /* read inquiry */
	ccw->flags = 0x20;    /* ignore incorrect length */
	ccw->count = 160;
	ccw->cda = (__u32)__pa(raw->inbuf);
}

/*
 * Set up a write request with the information from the main structure.
 * A ccw chain is created that writes as much as possible from the output
 * buffer to the 3215 device. If a queued write exists it is replaced by
 * the new, probably lengthened request.
 */
static void raw3215_mk_write_req(struct raw3215_info *raw)
{
	struct raw3215_req *req;
	struct ccw1 *ccw;
	int len, count, ix, lines;

	if (raw->count <= raw->written)
		return;
	/* check if there is a queued write request */
	req = raw->queued_write;
	if (req == NULL) {
		/* no queued write request, use new req structure */
		req = raw3215_alloc_req();
		req->type = RAW3215_WRITE;
		req->info = raw;
		raw->queued_write = req;
	} else {
		raw->written -= req->len;
	}

	ccw = req->ccws;
	req->start = (raw->head - raw->count + raw->written) &
		     (RAW3215_BUFFER_SIZE - 1);
	/*
	 * now we have to count newlines. We can at max accept
	 * RAW3215_MAX_NEWLINE newlines in a single ssch due to
	 * a restriction in VM
	 */
	lines = 0;
	ix = req->start;
	while (lines < RAW3215_MAX_NEWLINE && ix != raw->head) {
		if (raw->buffer[ix] == 0x15)
			lines++;
		ix = (ix + 1) & (RAW3215_BUFFER_SIZE - 1);
	}
	len = ((ix - 1 - req->start) & (RAW3215_BUFFER_SIZE - 1)) + 1;
	if (len > RAW3215_MAX_BYTES)
		len = RAW3215_MAX_BYTES;
	req->len = len;
	raw->written += len;

	/* set the indication if we should try to enlarge this request */
	req->delayable = (ix == raw->head) && (len < RAW3215_MIN_WRITE);

	ix = req->start;
	while (len > 0) {
		if (ccw > req->ccws)
			ccw[-1].flags |= 0x40; /* use command chaining */
		ccw->cmd_code = 0x01; /* write, auto carrier return */
		ccw->flags = 0x20;    /* ignore incorrect length ind.  */
		ccw->cda = (__u32)__pa(raw->buffer + ix);
		count = len;
		if (ix + count > RAW3215_BUFFER_SIZE)
			count = RAW3215_BUFFER_SIZE - ix;
		ccw->count = count;
		len -= count;
		ix = (ix + count) & (RAW3215_BUFFER_SIZE - 1);
		ccw++;
	}
	/*
	 * Add a NOP to the channel program. 3215 devices are purely
	 * emulated and its much better to avoid the channel end
	 * interrupt in this case.
	 */
	if (ccw > req->ccws)
		ccw[-1].flags |= 0x40; /* use command chaining */
	ccw->cmd_code = 0x03; /* NOP */
	ccw->flags = 0;
	ccw->cda = 0;
	ccw->count = 1;
}

/*
 * Start a read or a write request
 */
static void raw3215_start_io(struct raw3215_info *raw)
{
	struct raw3215_req *req;
	int res;

	req = raw->queued_read;
	if (req != NULL &&
	    !(raw->flags & (RAW3215_WORKING | RAW3215_THROTTLED))) {
		/* dequeue request */
		raw->queued_read = NULL;
		res = ccw_device_start(raw->cdev, req->ccws,
				       (unsigned long) req, 0, 0);
		if (res != 0) {
			/* do_IO failed, put request back to queue */
			raw->queued_read = req;
		} else {
			raw->flags |= RAW3215_WORKING;
		}
	}
	req = raw->queued_write;
	if (req != NULL &&
	    !(raw->flags & (RAW3215_WORKING | RAW3215_STOPPED))) {
		/* dequeue request */
		raw->queued_write = NULL;
		res = ccw_device_start(raw->cdev, req->ccws,
				       (unsigned long) req, 0, 0);
		if (res != 0) {
			/* do_IO failed, put request back to queue */
			raw->queued_write = req;
		} else {
			raw->flags |= RAW3215_WORKING;
		}
	}
}

/*
 * Function to start a delayed output after RAW3215_TIMEOUT seconds
 */
static void raw3215_timeout(struct timer_list *t)
{
	struct raw3215_info *raw = from_timer(raw, t, timer);
	unsigned long flags;

	spin_lock_irqsave(get_ccwdev_lock(raw->cdev), flags);
	raw->flags &= ~RAW3215_TIMER_RUNS;
	raw3215_mk_write_req(raw);
	raw3215_start_io(raw);
	if ((raw->queued_read || raw->queued_write) &&
	    !(raw->flags & RAW3215_WORKING) &&
	    !(raw->flags & RAW3215_TIMER_RUNS)) {
		raw->timer.expires = RAW3215_TIMEOUT + jiffies;
		add_timer(&raw->timer);
		raw->flags |= RAW3215_TIMER_RUNS;
	}
	spin_unlock_irqrestore(get_ccwdev_lock(raw->cdev), flags);
}

/*
 * Function to conditionally start an IO. A read is started immediately,
 * a write is only started immediately if the flush flag is on or the
 * amount of data is bigger than RAW3215_MIN_WRITE. If a write is not
 * done immediately a timer is started with a delay of RAW3215_TIMEOUT.
 */
static inline void raw3215_try_io(struct raw3215_info *raw)
{
	if (!tty_port_initialized(&raw->port))
		return;
	if (raw->queued_read != NULL)
		raw3215_start_io(raw);
	else if (raw->queued_write != NULL) {
		if ((raw->queued_write->delayable == 0) ||
		    (raw->flags & RAW3215_FLUSHING)) {
			/* execute write requests bigger than minimum size */
			raw3215_start_io(raw);
		}
	}
	if ((raw->queued_read || raw->queued_write) &&
	    !(raw->flags & RAW3215_WORKING) &&
	    !(raw->flags & RAW3215_TIMER_RUNS)) {
		raw->timer.expires = RAW3215_TIMEOUT + jiffies;
		add_timer(&raw->timer);
		raw->flags |= RAW3215_TIMER_RUNS;
	}
}

/*
 * Try to start the next IO and wake up processes waiting on the tty.
 */
static void raw3215_next_io(struct raw3215_info *raw, struct tty_struct *tty)
{
	raw3215_mk_write_req(raw);
	raw3215_try_io(raw);
	if (tty && RAW3215_BUFFER_SIZE - raw->count >= RAW3215_MIN_SPACE)
		tty_wakeup(tty);
}

/*
 * Interrupt routine, called from common io layer
 */
static void raw3215_irq(struct ccw_device *cdev, unsigned long intparm,
			struct irb *irb)
{
	struct raw3215_info *raw;
	struct raw3215_req *req;
	struct tty_struct *tty;
	int cstat, dstat;
	int count;

	raw = dev_get_drvdata(&cdev->dev);
	req = (struct raw3215_req *) intparm;
	tty = tty_port_tty_get(&raw->port);
	cstat = irb->scsw.cmd.cstat;
	dstat = irb->scsw.cmd.dstat;
	if (cstat != 0)
		raw3215_next_io(raw, tty);
	if (dstat & 0x01) { /* we got a unit exception */
		dstat &= ~0x01;	 /* we can ignore it */
	}
	switch (dstat) {
	case 0x80:
		if (cstat != 0)
			break;
		/* Attention interrupt, someone hit the enter key */
		raw3215_mk_read_req(raw);
		raw3215_next_io(raw, tty);
		break;
	case 0x08:
	case 0x0C:
		/* Channel end interrupt. */
		if ((raw = req->info) == NULL)
			goto put_tty;	     /* That shouldn't happen ... */
		if (req->type == RAW3215_READ) {
			/* store residual count, then wait for device end */
			req->residual = irb->scsw.cmd.count;
		}
		if (dstat == 0x08)
			break;
		fallthrough;
	case 0x04:
		/* Device end interrupt. */
		if ((raw = req->info) == NULL)
			goto put_tty;	     /* That shouldn't happen ... */
		if (req->type == RAW3215_READ && tty != NULL) {
			unsigned int cchar;

			count = 160 - req->residual;
			EBCASC(raw->inbuf, count);
			cchar = ctrlchar_handle(raw->inbuf, count, tty);
			switch (cchar & CTRLCHAR_MASK) {
			case CTRLCHAR_SYSRQ:
				break;

			case CTRLCHAR_CTRL:
				tty_insert_flip_char(&raw->port, cchar,
						TTY_NORMAL);
				tty_flip_buffer_push(&raw->port);
				break;

			case CTRLCHAR_NONE:
				if (count < 2 ||
				    (strncmp(raw->inbuf+count-2, "\252n", 2) &&
				     strncmp(raw->inbuf+count-2, "^n", 2)) ) {
					/* add the auto \n */
					raw->inbuf[count] = '\n';
					count++;
				} else
					count -= 2;
				tty_insert_flip_string(&raw->port, raw->inbuf,
						count);
				tty_flip_buffer_push(&raw->port);
				break;
			}
		} else if (req->type == RAW3215_WRITE) {
			raw->count -= req->len;
			raw->written -= req->len;
		}
		raw->flags &= ~RAW3215_WORKING;
		raw3215_free_req(req);
		/* check for empty wait */
		if (waitqueue_active(&raw->empty_wait) &&
		    raw->queued_write == NULL &&
		    raw->queued_read == NULL) {
			wake_up_interruptible(&raw->empty_wait);
		}
		raw3215_next_io(raw, tty);
		break;
	default:
		/* Strange interrupt, I'll do my best to clean up */
		if (req != NULL && req->type != RAW3215_FREE) {
			if (req->type == RAW3215_WRITE) {
				raw->count -= req->len;
				raw->written -= req->len;
			}
			raw->flags &= ~RAW3215_WORKING;
			raw3215_free_req(req);
		}
		raw3215_next_io(raw, tty);
	}
put_tty:
	tty_kref_put(tty);
}

/*
 * Need to drop data to avoid blocking. Drop as much data as possible.
 * This is unqueued part in the buffer and the queued part in the request.
 * Also adjust the head position to append new data and set count
 * accordingly.
 *
 * Return number of bytes available in buffer.
 */
static unsigned int raw3215_drop(struct raw3215_info *raw)
{
	struct raw3215_req *req;

	req = raw->queued_write;
	if (req) {
		/* Drop queued data and delete request */
		raw->written -= req->len;
		raw3215_free_req(req);
		raw->queued_write = NULL;
	}
	raw->head = (raw->head - raw->count + raw->written) &
		    (RAW3215_BUFFER_SIZE - 1);
	raw->count = raw->written;

	return RAW3215_BUFFER_SIZE - raw->count;
}

/*
 * Wait until length bytes are available int the output buffer.
 * If drop mode is active and wait condition holds true, start dropping
 * data.
 * Has to be called with the s390irq lock held. Can be called
 * disabled.
 */
static unsigned int raw3215_make_room(struct raw3215_info *raw,
				      unsigned int length, bool drop)
{
	while (RAW3215_BUFFER_SIZE - raw->count < length) {
		if (drop)
			return raw3215_drop(raw);

		/* there might be a request pending */
		raw->flags |= RAW3215_FLUSHING;
		raw3215_mk_write_req(raw);
		raw3215_try_io(raw);
		raw->flags &= ~RAW3215_FLUSHING;
#ifdef CONFIG_TN3215_CONSOLE
		ccw_device_wait_idle(raw->cdev);
#endif
		/* Enough room freed up ? */
		if (RAW3215_BUFFER_SIZE - raw->count >= length)
			break;
		/* there might be another cpu waiting for the lock */
		spin_unlock(get_ccwdev_lock(raw->cdev));
		udelay(100);
		spin_lock(get_ccwdev_lock(raw->cdev));
	}
	return length;
}

#define	RAW3215_COUNT	1
#define	RAW3215_STORE	2

/*
 * Add text to console buffer. Find tabs in input and calculate size
 * including tab replacement.
 * This function operates in 2 different modes, depending on parameter
 * opmode:
 * RAW3215_COUNT: Get the size needed for the input string with
 *	proper tab replacement calculation.
 *	Return value is the number of bytes required to store the
 *	input. However no data is actually stored.
 *	The parameter todrop is not used.
 * RAW3215_STORE: Add data to the console buffer. The parameter todrop is
 *	valid and contains the number of bytes to be dropped from head of
 *	string	without blocking.
 *	Return value is the number of bytes copied.
 */
static unsigned int raw3215_addtext(const char *str, unsigned int length,
				    struct raw3215_info *raw, int opmode,
				    unsigned int todrop)
{
	unsigned int c, ch, i, blanks, expanded_size = 0;
	unsigned int column = raw->line_pos;

	if (opmode == RAW3215_COUNT)
		todrop = 0;

	for (c = 0; c < length; ++c) {
		blanks = 1;
		ch = str[c];

		switch (ch) {
		case '\n':
			expanded_size++;
			column = 0;
			break;
		case '\t':
			blanks = TAB_STOP_SIZE - (column % TAB_STOP_SIZE);
			column += blanks;
			expanded_size += blanks;
			ch = ' ';
			break;
		default:
			expanded_size++;
			column++;
			break;
		}

		if (opmode == RAW3215_COUNT)
			continue;
		if (todrop && expanded_size < todrop)	/* Drop head data */
			continue;
		for (i = 0; i < blanks; i++) {
			raw->buffer[raw->head] = (char)_ascebc[(int)ch];
			raw->head = (raw->head + 1) & (RAW3215_BUFFER_SIZE - 1);
			raw->count++;
		}
		raw->line_pos = column;
	}
	return expanded_size - todrop;
}

/*
 * String write routine for 3215 devices
 */
static void raw3215_write(struct raw3215_info *raw, const char *str,
			  unsigned int length)
{
	unsigned int count, avail;
	unsigned long flags;

	spin_lock_irqsave(get_ccwdev_lock(raw->cdev), flags);

	count = raw3215_addtext(str, length, raw, RAW3215_COUNT, 0);

	avail = raw3215_make_room(raw, count, con3215_drop);
	if (avail) {
		raw3215_addtext(str, length, raw, RAW3215_STORE,
				count - avail);
	}
	if (!(raw->flags & RAW3215_WORKING)) {
		raw3215_mk_write_req(raw);
		/* start or queue request */
		raw3215_try_io(raw);
	}
	spin_unlock_irqrestore(get_ccwdev_lock(raw->cdev), flags);
}

/*
 * Put character routine for 3215 devices
 */
static void raw3215_putchar(struct raw3215_info *raw, unsigned char ch)
{
	raw3215_write(raw, &ch, 1);
}

/*
 * Flush routine, it simply sets the flush flag and tries to start
 * pending IO.
 */
static void raw3215_flush_buffer(struct raw3215_info *raw)
{
	unsigned long flags;

	spin_lock_irqsave(get_ccwdev_lock(raw->cdev), flags);
	if (raw->count > 0) {
		raw->flags |= RAW3215_FLUSHING;
		raw3215_try_io(raw);
		raw->flags &= ~RAW3215_FLUSHING;
	}
	spin_unlock_irqrestore(get_ccwdev_lock(raw->cdev), flags);
}

/*
 * Fire up a 3215 device.
 */
static int raw3215_startup(struct raw3215_info *raw)
{
	unsigned long flags;

	if (tty_port_initialized(&raw->port))
		return 0;
	raw->line_pos = 0;
	tty_port_set_initialized(&raw->port, true);
	spin_lock_irqsave(get_ccwdev_lock(raw->cdev), flags);
	raw3215_try_io(raw);
	spin_unlock_irqrestore(get_ccwdev_lock(raw->cdev), flags);

	return 0;
}

/*
 * Shutdown a 3215 device.
 */
static void raw3215_shutdown(struct raw3215_info *raw)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;

	if (!tty_port_initialized(&raw->port) || (raw->flags & RAW3215_FIXED))
		return;
	/* Wait for outstanding requests, then free irq */
	spin_lock_irqsave(get_ccwdev_lock(raw->cdev), flags);
	if ((raw->flags & RAW3215_WORKING) ||
	    raw->queued_write != NULL ||
	    raw->queued_read != NULL) {
		add_wait_queue(&raw->empty_wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(get_ccwdev_lock(raw->cdev), flags);
		schedule();
		spin_lock_irqsave(get_ccwdev_lock(raw->cdev), flags);
		remove_wait_queue(&raw->empty_wait, &wait);
		set_current_state(TASK_RUNNING);
		tty_port_set_initialized(&raw->port, true);
	}
	spin_unlock_irqrestore(get_ccwdev_lock(raw->cdev), flags);
}

static struct raw3215_info *raw3215_alloc_info(void)
{
	struct raw3215_info *info;

	info = kzalloc(sizeof(struct raw3215_info), GFP_KERNEL | GFP_DMA);
	if (!info)
		return NULL;

	info->buffer = kzalloc(RAW3215_BUFFER_SIZE, GFP_KERNEL | GFP_DMA);
	info->inbuf = kzalloc(RAW3215_INBUF_SIZE, GFP_KERNEL | GFP_DMA);
	if (!info->buffer || !info->inbuf) {
		kfree(info->inbuf);
		kfree(info->buffer);
		kfree(info);
		return NULL;
	}

	timer_setup(&info->timer, raw3215_timeout, 0);
	init_waitqueue_head(&info->empty_wait);
	tty_port_init(&info->port);

	return info;
}

static void raw3215_free_info(struct raw3215_info *raw)
{
	kfree(raw->inbuf);
	kfree(raw->buffer);
	tty_port_destroy(&raw->port);
	kfree(raw);
}

static int raw3215_probe(struct ccw_device *cdev)
{
	struct raw3215_info *raw;
	int line;

	/* Console is special. */
	if (raw3215[0] && (raw3215[0] == dev_get_drvdata(&cdev->dev)))
		return 0;

	raw = raw3215_alloc_info();
	if (raw == NULL)
		return -ENOMEM;

	raw->cdev = cdev;
	dev_set_drvdata(&cdev->dev, raw);
	cdev->handler = raw3215_irq;

	spin_lock(&raw3215_device_lock);
	for (line = 0; line < NR_3215; line++) {
		if (!raw3215[line]) {
			raw3215[line] = raw;
			break;
		}
	}
	spin_unlock(&raw3215_device_lock);
	if (line == NR_3215) {
		raw3215_free_info(raw);
		return -ENODEV;
	}

	return 0;
}

static void raw3215_remove(struct ccw_device *cdev)
{
	struct raw3215_info *raw;
	unsigned int line;

	ccw_device_set_offline(cdev);
	raw = dev_get_drvdata(&cdev->dev);
	if (raw) {
		spin_lock(&raw3215_device_lock);
		for (line = 0; line < NR_3215; line++)
			if (raw3215[line] == raw)
				break;
		raw3215[line] = NULL;
		spin_unlock(&raw3215_device_lock);
		dev_set_drvdata(&cdev->dev, NULL);
		raw3215_free_info(raw);
	}
}

static int raw3215_set_online(struct ccw_device *cdev)
{
	struct raw3215_info *raw;

	raw = dev_get_drvdata(&cdev->dev);
	if (!raw)
		return -ENODEV;

	return raw3215_startup(raw);
}

static int raw3215_set_offline(struct ccw_device *cdev)
{
	struct raw3215_info *raw;

	raw = dev_get_drvdata(&cdev->dev);
	if (!raw)
		return -ENODEV;

	raw3215_shutdown(raw);

	return 0;
}

static struct ccw_device_id raw3215_id[] = {
	{ CCW_DEVICE(0x3215, 0) },
	{ /* end of list */ },
};

static ssize_t con_drop_store(struct device_driver *dev, const char *buf, size_t count)
{
	bool drop;
	int rc;

	rc = kstrtobool(buf, &drop);
	if (!rc)
		con3215_drop = drop;
	return rc ?: count;
}

static ssize_t con_drop_show(struct device_driver *dev, char *buf)
{
	return sysfs_emit(buf, "%d\n", con3215_drop ? 1 : 0);
}

static DRIVER_ATTR_RW(con_drop);

static struct attribute *con3215_drv_attrs[] = {
	&driver_attr_con_drop.attr,
	NULL,
};

static struct attribute_group con3215_drv_attr_group = {
	.attrs = con3215_drv_attrs,
	NULL,
};

static const struct attribute_group *con3215_drv_attr_groups[] = {
	&con3215_drv_attr_group,
	NULL,
};

static struct ccw_driver raw3215_ccw_driver = {
	.driver = {
		.name	= "3215",
		.groups = con3215_drv_attr_groups,
		.owner	= THIS_MODULE,
	},
	.ids		= raw3215_id,
	.probe		= &raw3215_probe,
	.remove		= &raw3215_remove,
	.set_online	= &raw3215_set_online,
	.set_offline	= &raw3215_set_offline,
	.int_class	= IRQIO_C15,
};

static void handle_write(struct raw3215_info *raw, const char *str, int count)
{
	int i;

	while (count > 0) {
		i = min_t(int, count, RAW3215_BUFFER_SIZE - 1);
		raw3215_write(raw, str, i);
		count -= i;
		str += i;
	}
}

#ifdef CONFIG_TN3215_CONSOLE
/*
 * Write a string to the 3215 console
 */
static void con3215_write(struct console *co, const char *str, unsigned int count)
{
	handle_write(raw3215[0], str, count);
}

static struct tty_driver *con3215_device(struct console *c, int *index)
{
	*index = c->index;
	return tty3215_driver;
}

/*
 * The below function is called as a panic/reboot notifier before the
 * system enters a disabled, endless loop.
 *
 * Notice we must use the spin_trylock() alternative, to prevent lockups
 * in atomic context (panic routine runs with secondary CPUs, local IRQs
 * and preemption disabled).
 */
static int con3215_notify(struct notifier_block *self,
			  unsigned long event, void *data)
{
	struct raw3215_info *raw;
	unsigned long flags;

	raw = raw3215[0];  /* console 3215 is the first one */
	if (!spin_trylock_irqsave(get_ccwdev_lock(raw->cdev), flags))
		return NOTIFY_DONE;
	raw3215_make_room(raw, RAW3215_BUFFER_SIZE, false);
	spin_unlock_irqrestore(get_ccwdev_lock(raw->cdev), flags);

	return NOTIFY_DONE;
}

static struct notifier_block on_panic_nb = {
	.notifier_call = con3215_notify,
	.priority = INT_MIN + 1, /* run the callback late */
};

static struct notifier_block on_reboot_nb = {
	.notifier_call = con3215_notify,
	.priority = INT_MIN + 1, /* run the callback late */
};

/*
 *  The console structure for the 3215 console
 */
static struct console con3215 = {
	.name	 = "ttyS",
	.write	 = con3215_write,
	.device	 = con3215_device,
	.flags	 = CON_PRINTBUFFER,
};

/*
 * 3215 console initialization code called from console_init().
 */
static int __init con3215_init(void)
{
	struct ccw_device *cdev;
	struct raw3215_info *raw;
	struct raw3215_req *req;
	int i;

	/* Check if 3215 is to be the console */
	if (!CONSOLE_IS_3215)
		return -ENODEV;

	/* Set the console mode for VM */
	if (MACHINE_IS_VM) {
		cpcmd("TERM CONMODE 3215", NULL, 0, NULL);
		cpcmd("TERM AUTOCR OFF", NULL, 0, NULL);
	}

	/* allocate 3215 request structures */
	raw3215_freelist = NULL;
	for (i = 0; i < NR_3215_REQ; i++) {
		req = kzalloc(sizeof(struct raw3215_req), GFP_KERNEL | GFP_DMA);
		if (!req)
			return -ENOMEM;
		req->next = raw3215_freelist;
		raw3215_freelist = req;
	}

	cdev = ccw_device_create_console(&raw3215_ccw_driver);
	if (IS_ERR(cdev))
		return -ENODEV;

	raw3215[0] = raw = raw3215_alloc_info();
	raw->cdev = cdev;
	dev_set_drvdata(&cdev->dev, raw);
	cdev->handler = raw3215_irq;

	raw->flags |= RAW3215_FIXED;
	if (ccw_device_enable_console(cdev)) {
		ccw_device_destroy_console(cdev);
		raw3215_free_info(raw);
		raw3215[0] = NULL;
		return -ENODEV;
	}

	/* Request the console irq */
	if (raw3215_startup(raw) != 0) {
		raw3215_free_info(raw);
		raw3215[0] = NULL;
		return -ENODEV;
	}
	atomic_notifier_chain_register(&panic_notifier_list, &on_panic_nb);
	register_reboot_notifier(&on_reboot_nb);
	register_console(&con3215);
	return 0;
}
console_initcall(con3215_init);
#endif

static int tty3215_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct raw3215_info *raw;

	raw = raw3215[tty->index];
	if (raw == NULL)
		return -ENODEV;

	tty->driver_data = raw;

	return tty_port_install(&raw->port, driver, tty);
}

/*
 * tty3215_open
 *
 * This routine is called whenever a 3215 tty is opened.
 */
static int tty3215_open(struct tty_struct *tty, struct file * filp)
{
	struct raw3215_info *raw = tty->driver_data;

	tty_port_tty_set(&raw->port, tty);

	/*
	 * Start up 3215 device
	 */
	return raw3215_startup(raw);
}

/*
 * tty3215_close()
 *
 * This routine is called when the 3215 tty is closed. We wait
 * for the remaining request to be completed. Then we clean up.
 */
static void tty3215_close(struct tty_struct *tty, struct file * filp)
{
	struct raw3215_info *raw = tty->driver_data;

	if (raw == NULL || tty->count > 1)
		return;
	tty->closing = 1;
	/* Shutdown the terminal */
	raw3215_shutdown(raw);
	tty->closing = 0;
	tty_port_tty_set(&raw->port, NULL);
}

/*
 * Returns the amount of free space in the output buffer.
 */
static unsigned int tty3215_write_room(struct tty_struct *tty)
{
	struct raw3215_info *raw = tty->driver_data;

	/* Subtract TAB_STOP_SIZE to allow for a tab, 8 <<< 64K */
	if ((RAW3215_BUFFER_SIZE - raw->count - TAB_STOP_SIZE) >= 0)
		return RAW3215_BUFFER_SIZE - raw->count - TAB_STOP_SIZE;
	else
		return 0;
}

/*
 * String write routine for 3215 ttys
 */
static int tty3215_write(struct tty_struct *tty,
			 const unsigned char *buf, int count)
{
	handle_write(tty->driver_data, buf, count);
	return count;
}

/*
 * Put character routine for 3215 ttys
 */
static int tty3215_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct raw3215_info *raw = tty->driver_data;

	raw3215_putchar(raw, ch);

	return 1;
}

static void tty3215_flush_chars(struct tty_struct *tty)
{
}

/*
 * Returns the number of characters in the output buffer
 */
static unsigned int tty3215_chars_in_buffer(struct tty_struct *tty)
{
	struct raw3215_info *raw = tty->driver_data;

	return raw->count;
}

static void tty3215_flush_buffer(struct tty_struct *tty)
{
	struct raw3215_info *raw = tty->driver_data;

	raw3215_flush_buffer(raw);
	tty_wakeup(tty);
}

/*
 * Disable reading from a 3215 tty
 */
static void tty3215_throttle(struct tty_struct *tty)
{
	struct raw3215_info *raw = tty->driver_data;

	raw->flags |= RAW3215_THROTTLED;
}

/*
 * Enable reading from a 3215 tty
 */
static void tty3215_unthrottle(struct tty_struct *tty)
{
	struct raw3215_info *raw = tty->driver_data;
	unsigned long flags;

	if (raw->flags & RAW3215_THROTTLED) {
		spin_lock_irqsave(get_ccwdev_lock(raw->cdev), flags);
		raw->flags &= ~RAW3215_THROTTLED;
		raw3215_try_io(raw);
		spin_unlock_irqrestore(get_ccwdev_lock(raw->cdev), flags);
	}
}

/*
 * Disable writing to a 3215 tty
 */
static void tty3215_stop(struct tty_struct *tty)
{
	struct raw3215_info *raw = tty->driver_data;

	raw->flags |= RAW3215_STOPPED;
}

/*
 * Enable writing to a 3215 tty
 */
static void tty3215_start(struct tty_struct *tty)
{
	struct raw3215_info *raw = tty->driver_data;
	unsigned long flags;

	if (raw->flags & RAW3215_STOPPED) {
		spin_lock_irqsave(get_ccwdev_lock(raw->cdev), flags);
		raw->flags &= ~RAW3215_STOPPED;
		raw3215_try_io(raw);
		spin_unlock_irqrestore(get_ccwdev_lock(raw->cdev), flags);
	}
}

static const struct tty_operations tty3215_ops = {
	.install = tty3215_install,
	.open = tty3215_open,
	.close = tty3215_close,
	.write = tty3215_write,
	.put_char = tty3215_put_char,
	.flush_chars = tty3215_flush_chars,
	.write_room = tty3215_write_room,
	.chars_in_buffer = tty3215_chars_in_buffer,
	.flush_buffer = tty3215_flush_buffer,
	.throttle = tty3215_throttle,
	.unthrottle = tty3215_unthrottle,
	.stop = tty3215_stop,
	.start = tty3215_start,
};

static int __init con3215_setup_drop(char *str)
{
	bool drop;
	int rc;

	rc = kstrtobool(str, &drop);
	if (!rc)
		con3215_drop = drop;
	return rc;
}
early_param("con3215_drop", con3215_setup_drop);

/*
 * 3215 tty registration code called from tty_init().
 * Most kernel services (incl. kmalloc) are available at this poimt.
 */
static int __init tty3215_init(void)
{
	struct tty_driver *driver;
	int ret;

	if (!CONSOLE_IS_3215)
		return 0;

	driver = tty_alloc_driver(NR_3215, TTY_DRIVER_REAL_RAW);
	if (IS_ERR(driver))
		return PTR_ERR(driver);

	ret = ccw_driver_register(&raw3215_ccw_driver);
	if (ret) {
		tty_driver_kref_put(driver);
		return ret;
	}
	/*
	 * Initialize the tty_driver structure
	 * Entries in tty3215_driver that are NOT initialized:
	 * proc_entry, set_termios, flush_buffer, set_ldisc, write_proc
	 */

	driver->driver_name = "tty3215";
	driver->name = "ttyS";
	driver->major = TTY_MAJOR;
	driver->minor_start = 64;
	driver->type = TTY_DRIVER_TYPE_SYSTEM;
	driver->subtype = SYSTEM_TYPE_TTY;
	driver->init_termios = tty_std_termios;
	driver->init_termios.c_iflag = IGNBRK | IGNPAR;
	driver->init_termios.c_oflag = ONLCR;
	driver->init_termios.c_lflag = ISIG;
	tty_set_operations(driver, &tty3215_ops);
	ret = tty_register_driver(driver);
	if (ret) {
		tty_driver_kref_put(driver);
		return ret;
	}
	tty3215_driver = driver;
	return 0;
}
device_initcall(tty3215_init);
