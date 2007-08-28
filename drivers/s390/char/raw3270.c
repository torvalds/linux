/*
 *  drivers/s390/char/raw3270.c
 *    IBM/3270 Driver - core functions.
 *
 *  Author(s):
 *    Original 3270 Code for 2.4 written by Richard Hitt (UTS Global)
 *    Rewritten for 2.5 by Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	-- Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 */

#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/ebcdic.h>
#include <asm/diag.h>

#include "raw3270.h"

#include <linux/major.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/mutex.h>

static struct class *class3270;

/* The main 3270 data structure. */
struct raw3270 {
	struct list_head list;
	struct ccw_device *cdev;
	int minor;

	short model, rows, cols;
	unsigned long flags;

	struct list_head req_queue;	/* Request queue. */
	struct list_head view_list;	/* List of available views. */
	struct raw3270_view *view;	/* Active view. */

	struct timer_list timer;	/* Device timer. */

	unsigned char *ascebc;		/* ascii -> ebcdic table */
	struct class_device *clttydev;	/* 3270-class tty device ptr */
	struct class_device *cltubdev;	/* 3270-class tub device ptr */

	struct raw3270_request init_request;
	unsigned char init_data[256];
};

/* raw3270->flags */
#define RAW3270_FLAGS_14BITADDR	0	/* 14-bit buffer addresses */
#define RAW3270_FLAGS_BUSY	1	/* Device busy, leave it alone */
#define RAW3270_FLAGS_ATTN	2	/* Device sent an ATTN interrupt */
#define RAW3270_FLAGS_READY	4	/* Device is useable by views */
#define RAW3270_FLAGS_CONSOLE	8	/* Device is the console. */

/* Semaphore to protect global data of raw3270 (devices, views, etc). */
static DEFINE_MUTEX(raw3270_mutex);

/* List of 3270 devices. */
static struct list_head raw3270_devices = LIST_HEAD_INIT(raw3270_devices);

/*
 * Flag to indicate if the driver has been registered. Some operations
 * like waiting for the end of i/o need to be done differently as long
 * as the kernel is still starting up (console support).
 */
static int raw3270_registered;

/* Module parameters */
static int tubxcorrect = 0;
module_param(tubxcorrect, bool, 0);

/*
 * Wait queue for device init/delete, view delete.
 */
DECLARE_WAIT_QUEUE_HEAD(raw3270_wait_queue);

/*
 * Encode array for 12 bit 3270 addresses.
 */
static unsigned char raw3270_ebcgraf[64] =	{
	0x40, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
	0xc8, 0xc9, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
	0xd8, 0xd9, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x61, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
	0xe8, 0xe9, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
	0xf8, 0xf9, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f
};

void
raw3270_buffer_address(struct raw3270 *rp, char *cp, unsigned short addr)
{
	if (test_bit(RAW3270_FLAGS_14BITADDR, &rp->flags)) {
		cp[0] = (addr >> 8) & 0x3f;
		cp[1] = addr & 0xff;
	} else {
		cp[0] = raw3270_ebcgraf[(addr >> 6) & 0x3f];
		cp[1] = raw3270_ebcgraf[addr & 0x3f];
	}
}

/*
 * Allocate a new 3270 ccw request
 */
struct raw3270_request *
raw3270_request_alloc(size_t size)
{
	struct raw3270_request *rq;

	/* Allocate request structure */
	rq = kzalloc(sizeof(struct raw3270_request), GFP_KERNEL | GFP_DMA);
	if (!rq)
		return ERR_PTR(-ENOMEM);

	/* alloc output buffer. */
	if (size > 0) {
		rq->buffer = kmalloc(size, GFP_KERNEL | GFP_DMA);
		if (!rq->buffer) {
			kfree(rq);
			return ERR_PTR(-ENOMEM);
		}
	}
	rq->size = size;
	INIT_LIST_HEAD(&rq->list);

	/*
	 * Setup ccw.
	 */
	rq->ccw.cda = __pa(rq->buffer);
	rq->ccw.flags = CCW_FLAG_SLI;

	return rq;
}

#ifdef CONFIG_TN3270_CONSOLE
/*
 * Allocate a new 3270 ccw request from bootmem. Only works very
 * early in the boot process. Only con3270.c should be using this.
 */
struct raw3270_request __init *raw3270_request_alloc_bootmem(size_t size)
{
	struct raw3270_request *rq;

	rq = alloc_bootmem_low(sizeof(struct raw3270));
	if (!rq)
		return ERR_PTR(-ENOMEM);
	memset(rq, 0, sizeof(struct raw3270_request));

	/* alloc output buffer. */
	if (size > 0) {
		rq->buffer = alloc_bootmem_low(size);
		if (!rq->buffer) {
			free_bootmem((unsigned long) rq,
				     sizeof(struct raw3270));
			return ERR_PTR(-ENOMEM);
		}
	}
	rq->size = size;
	INIT_LIST_HEAD(&rq->list);

	/*
	 * Setup ccw.
	 */
	rq->ccw.cda = __pa(rq->buffer);
	rq->ccw.flags = CCW_FLAG_SLI;

	return rq;
}
#endif

/*
 * Free 3270 ccw request
 */
void
raw3270_request_free (struct raw3270_request *rq)
{
	kfree(rq->buffer);
	kfree(rq);
}

/*
 * Reset request to initial state.
 */
void
raw3270_request_reset(struct raw3270_request *rq)
{
	BUG_ON(!list_empty(&rq->list));
	rq->ccw.cmd_code = 0;
	rq->ccw.count = 0;
	rq->ccw.cda = __pa(rq->buffer);
	rq->ccw.flags = CCW_FLAG_SLI;
	rq->rescnt = 0;
	rq->rc = 0;
}

/*
 * Set command code to ccw of a request.
 */
void
raw3270_request_set_cmd(struct raw3270_request *rq, u8 cmd)
{
	rq->ccw.cmd_code = cmd;
}

/*
 * Add data fragment to output buffer.
 */
int
raw3270_request_add_data(struct raw3270_request *rq, void *data, size_t size)
{
	if (size + rq->ccw.count > rq->size)
		return -E2BIG;
	memcpy(rq->buffer + rq->ccw.count, data, size);
	rq->ccw.count += size;
	return 0;
}

/*
 * Set address/length pair to ccw of a request.
 */
void
raw3270_request_set_data(struct raw3270_request *rq, void *data, size_t size)
{
	rq->ccw.cda = __pa(data);
	rq->ccw.count = size;
}

/*
 * Set idal buffer to ccw of a request.
 */
void
raw3270_request_set_idal(struct raw3270_request *rq, struct idal_buffer *ib)
{
	rq->ccw.cda = __pa(ib->data);
	rq->ccw.count = ib->size;
	rq->ccw.flags |= CCW_FLAG_IDA;
}

/*
 * Stop running ccw.
 */
static int
raw3270_halt_io_nolock(struct raw3270 *rp, struct raw3270_request *rq)
{
	int retries;
	int rc;

	if (raw3270_request_final(rq))
		return 0;
	/* Check if interrupt has already been processed */
	for (retries = 0; retries < 5; retries++) {
		if (retries < 2)
			rc = ccw_device_halt(rp->cdev, (long) rq);
		else
			rc = ccw_device_clear(rp->cdev, (long) rq);
		if (rc == 0)
			break;		/* termination successful */
	}
	return rc;
}

static int
raw3270_halt_io(struct raw3270 *rp, struct raw3270_request *rq)
{
	unsigned long flags;
	int rc;

	spin_lock_irqsave(get_ccwdev_lock(rp->cdev), flags);
	rc = raw3270_halt_io_nolock(rp, rq);
	spin_unlock_irqrestore(get_ccwdev_lock(rp->cdev), flags);
	return rc;
}

/*
 * Add the request to the request queue, try to start it if the
 * 3270 device is idle. Return without waiting for end of i/o.
 */
static int
__raw3270_start(struct raw3270 *rp, struct raw3270_view *view,
		struct raw3270_request *rq)
{
	rq->view = view;
	raw3270_get_view(view);
	if (list_empty(&rp->req_queue) &&
	    !test_bit(RAW3270_FLAGS_BUSY, &rp->flags)) {
		/* No other requests are on the queue. Start this one. */
		rq->rc = ccw_device_start(rp->cdev, &rq->ccw,
					       (unsigned long) rq, 0, 0);
		if (rq->rc) {
			raw3270_put_view(view);
			return rq->rc;
		}
	}
	list_add_tail(&rq->list, &rp->req_queue);
	return 0;
}

int
raw3270_start(struct raw3270_view *view, struct raw3270_request *rq)
{
	unsigned long flags;
	struct raw3270 *rp;
	int rc;

	spin_lock_irqsave(get_ccwdev_lock(view->dev->cdev), flags);
	rp = view->dev;
	if (!rp || rp->view != view)
		rc = -EACCES;
	else if (!test_bit(RAW3270_FLAGS_READY, &rp->flags))
		rc = -ENODEV;
	else
		rc =  __raw3270_start(rp, view, rq);
	spin_unlock_irqrestore(get_ccwdev_lock(view->dev->cdev), flags);
	return rc;
}

int
raw3270_start_locked(struct raw3270_view *view, struct raw3270_request *rq)
{
	struct raw3270 *rp;
	int rc;

	rp = view->dev;
	if (!rp || rp->view != view)
		rc = -EACCES;
	else if (!test_bit(RAW3270_FLAGS_READY, &rp->flags))
		rc = -ENODEV;
	else
		rc =  __raw3270_start(rp, view, rq);
	return rc;
}

int
raw3270_start_irq(struct raw3270_view *view, struct raw3270_request *rq)
{
	struct raw3270 *rp;

	rp = view->dev;
	rq->view = view;
	raw3270_get_view(view);
	list_add_tail(&rq->list, &rp->req_queue);
	return 0;
}

/*
 * 3270 interrupt routine, called from the ccw_device layer
 */
static void
raw3270_irq (struct ccw_device *cdev, unsigned long intparm, struct irb *irb)
{
	struct raw3270 *rp;
	struct raw3270_view *view;
	struct raw3270_request *rq;
	int rc;

	rp = (struct raw3270 *) cdev->dev.driver_data;
	if (!rp)
		return;
	rq = (struct raw3270_request *) intparm;
	view = rq ? rq->view : rp->view;

	if (IS_ERR(irb))
		rc = RAW3270_IO_RETRY;
	else if (irb->scsw.fctl & SCSW_FCTL_HALT_FUNC) {
		rq->rc = -EIO;
		rc = RAW3270_IO_DONE;
	} else if (irb->scsw.dstat ==  (DEV_STAT_CHN_END | DEV_STAT_DEV_END |
					DEV_STAT_UNIT_EXCEP)) {
		/* Handle CE-DE-UE and subsequent UDE */
		set_bit(RAW3270_FLAGS_BUSY, &rp->flags);
		rc = RAW3270_IO_BUSY;
	} else if (test_bit(RAW3270_FLAGS_BUSY, &rp->flags)) {
		/* Wait for UDE if busy flag is set. */
		if (irb->scsw.dstat & DEV_STAT_DEV_END) {
			clear_bit(RAW3270_FLAGS_BUSY, &rp->flags);
			/* Got it, now retry. */
			rc = RAW3270_IO_RETRY;
		} else
			rc = RAW3270_IO_BUSY;
	} else if (view)
		rc = view->fn->intv(view, rq, irb);
	else
		rc = RAW3270_IO_DONE;

	switch (rc) {
	case RAW3270_IO_DONE:
		break;
	case RAW3270_IO_BUSY:
		/* 
		 * Intervention required by the operator. We have to wait
		 * for unsolicited device end.
		 */
		return;
	case RAW3270_IO_RETRY:
		if (!rq)
			break;
		rq->rc = ccw_device_start(rp->cdev, &rq->ccw,
					  (unsigned long) rq, 0, 0);
		if (rq->rc == 0)
			return;	/* Sucessfully restarted. */
		break;
	case RAW3270_IO_STOP:
		if (!rq)
			break;
		raw3270_halt_io_nolock(rp, rq);
		rq->rc = -EIO;
		break;
	default:
		BUG();
	}
	if (rq) {
		BUG_ON(list_empty(&rq->list));
		/* The request completed, remove from queue and do callback. */
		list_del_init(&rq->list);
		if (rq->callback)
			rq->callback(rq, rq->callback_data);
		/* Do put_device for get_device in raw3270_start. */
		raw3270_put_view(view);
	}
	/*
	 * Try to start each request on request queue until one is
	 * started successful.
	 */
	while (!list_empty(&rp->req_queue)) {
		rq = list_entry(rp->req_queue.next,struct raw3270_request,list);
		rq->rc = ccw_device_start(rp->cdev, &rq->ccw,
					  (unsigned long) rq, 0, 0);
		if (rq->rc == 0)
			break;
		/* Start failed. Remove request and do callback. */
		list_del_init(&rq->list);
		if (rq->callback)
			rq->callback(rq, rq->callback_data);
		/* Do put_device for get_device in raw3270_start. */
		raw3270_put_view(view);
	}
}

/*
 * Size sensing.
 */

struct raw3270_ua {	/* Query Reply structure for Usable Area */
	struct {	/* Usable Area Query Reply Base */
		short l;	/* Length of this structured field */
		char  sfid;	/* 0x81 if Query Reply */
		char  qcode;	/* 0x81 if Usable Area */
		char  flags0;
		char  flags1;
		short w;	/* Width of usable area */
		short h;	/* Heigth of usavle area */
		char  units;	/* 0x00:in; 0x01:mm */
		int   xr;
		int   yr;
		char  aw;
		char  ah;
		short buffsz;	/* Character buffer size, bytes */
		char  xmin;
		char  ymin;
		char  xmax;
		char  ymax;
	} __attribute__ ((packed)) uab;
	struct {	/* Alternate Usable Area Self-Defining Parameter */
		char  l;	/* Length of this Self-Defining Parm */
		char  sdpid;	/* 0x02 if Alternate Usable Area */
		char  res;
		char  auaid;	/* 0x01 is Id for the A U A */
		short wauai;	/* Width of AUAi */
		short hauai;	/* Height of AUAi */
		char  auaunits;	/* 0x00:in, 0x01:mm */
		int   auaxr;
		int   auayr;
		char  awauai;
		char  ahauai;
	} __attribute__ ((packed)) aua;
} __attribute__ ((packed));

static struct diag210 raw3270_init_diag210;
static DEFINE_MUTEX(raw3270_init_mutex);

static int
raw3270_init_irq(struct raw3270_view *view, struct raw3270_request *rq,
		 struct irb *irb)
{
	/*
	 * Unit-Check Processing:
	 * Expect Command Reject or Intervention Required.
	 */
	if (irb->scsw.dstat & DEV_STAT_UNIT_CHECK) {
		/* Request finished abnormally. */
		if (irb->ecw[0] & SNS0_INTERVENTION_REQ) {
			set_bit(RAW3270_FLAGS_BUSY, &view->dev->flags);
			return RAW3270_IO_BUSY;
		}
	}
	if (rq) {
		if (irb->scsw.dstat & DEV_STAT_UNIT_CHECK) {
			if (irb->ecw[0] & SNS0_CMD_REJECT)
				rq->rc = -EOPNOTSUPP;
			else
				rq->rc = -EIO;
		} else
			/* Request finished normally. Copy residual count. */
			rq->rescnt = irb->scsw.count;
	}
	if (irb->scsw.dstat & DEV_STAT_ATTENTION) {
		set_bit(RAW3270_FLAGS_ATTN, &view->dev->flags);
		wake_up(&raw3270_wait_queue);
	}
	return RAW3270_IO_DONE;
}

static struct raw3270_fn raw3270_init_fn = {
	.intv = raw3270_init_irq
};

static struct raw3270_view raw3270_init_view = {
	.fn = &raw3270_init_fn
};

/*
 * raw3270_wait/raw3270_wait_interruptible/__raw3270_wakeup
 * Wait for end of request. The request must have been started
 * with raw3270_start, rc = 0. The device lock may NOT have been
 * released between calling raw3270_start and raw3270_wait.
 */
static void
raw3270_wake_init(struct raw3270_request *rq, void *data)
{
	wake_up((wait_queue_head_t *) data);
}

/*
 * Special wait function that can cope with console initialization.
 */
static int
raw3270_start_init(struct raw3270 *rp, struct raw3270_view *view,
		   struct raw3270_request *rq)
{
	unsigned long flags;
	wait_queue_head_t wq;
	int rc;

#ifdef CONFIG_TN3270_CONSOLE
	if (raw3270_registered == 0) {
		spin_lock_irqsave(get_ccwdev_lock(view->dev->cdev), flags);
		rq->callback = NULL;
		rc = __raw3270_start(rp, view, rq);
		if (rc == 0)
			while (!raw3270_request_final(rq)) {
				wait_cons_dev();
				barrier();
			}
		spin_unlock_irqrestore(get_ccwdev_lock(view->dev->cdev), flags);
		return rq->rc;
	}
#endif
	init_waitqueue_head(&wq);
	rq->callback = raw3270_wake_init;
	rq->callback_data = &wq;
	spin_lock_irqsave(get_ccwdev_lock(view->dev->cdev), flags);
	rc = __raw3270_start(rp, view, rq);
	spin_unlock_irqrestore(get_ccwdev_lock(view->dev->cdev), flags);
	if (rc)
		return rc;
	/* Now wait for the completion. */
	rc = wait_event_interruptible(wq, raw3270_request_final(rq));
	if (rc == -ERESTARTSYS) {	/* Interrupted by a signal. */
		raw3270_halt_io(view->dev, rq);
		/* No wait for the halt to complete. */
		wait_event(wq, raw3270_request_final(rq));
		return -ERESTARTSYS;
	}
	return rq->rc;
}

static int
__raw3270_size_device_vm(struct raw3270 *rp)
{
	int rc, model;
	struct ccw_dev_id dev_id;

	ccw_device_get_id(rp->cdev, &dev_id);
	raw3270_init_diag210.vrdcdvno = dev_id.devno;
	raw3270_init_diag210.vrdclen = sizeof(struct diag210);
	rc = diag210(&raw3270_init_diag210);
	if (rc)
		return rc;
	model = raw3270_init_diag210.vrdccrmd;
	switch (model) {
	case 2:
		rp->model = model;
		rp->rows = 24;
		rp->cols = 80;
		break;
	case 3:
		rp->model = model;
		rp->rows = 32;
		rp->cols = 80;
		break;
	case 4:
		rp->model = model;
		rp->rows = 43;
		rp->cols = 80;
		break;
	case 5:
		rp->model = model;
		rp->rows = 27;
		rp->cols = 132;
		break;
	default:
		printk(KERN_WARNING "vrdccrmd is 0x%.8x\n", model);
		rc = -EOPNOTSUPP;
		break;
	}
	return rc;
}

static int
__raw3270_size_device(struct raw3270 *rp)
{
	static const unsigned char wbuf[] =
		{ 0x00, 0x07, 0x01, 0xff, 0x03, 0x00, 0x81 };
	struct raw3270_ua *uap;
	unsigned short count;
	int rc;

	/*
	 * To determine the size of the 3270 device we need to do:
	 * 1) send a 'read partition' data stream to the device
	 * 2) wait for the attn interrupt that preceeds the query reply
	 * 3) do a read modified to get the query reply
	 * To make things worse we have to cope with intervention
	 * required (3270 device switched to 'stand-by') and command
	 * rejects (old devices that can't do 'read partition').
	 */
	memset(&rp->init_request, 0, sizeof(rp->init_request));
	memset(&rp->init_data, 0, 256);
	/* Store 'read partition' data stream to init_data */
	memcpy(&rp->init_data, wbuf, sizeof(wbuf));
	INIT_LIST_HEAD(&rp->init_request.list);
	rp->init_request.ccw.cmd_code = TC_WRITESF;
	rp->init_request.ccw.flags = CCW_FLAG_SLI;
	rp->init_request.ccw.count = sizeof(wbuf);
	rp->init_request.ccw.cda = (__u32) __pa(&rp->init_data);

	rc = raw3270_start_init(rp, &raw3270_init_view, &rp->init_request);
	if (rc)
		/* Check error cases: -ERESTARTSYS, -EIO and -EOPNOTSUPP */
		return rc;

	/* Wait for attention interrupt. */
#ifdef CONFIG_TN3270_CONSOLE
	if (raw3270_registered == 0) {
		unsigned long flags;

		spin_lock_irqsave(get_ccwdev_lock(rp->cdev), flags);
		while (!test_and_clear_bit(RAW3270_FLAGS_ATTN, &rp->flags))
			wait_cons_dev();
		spin_unlock_irqrestore(get_ccwdev_lock(rp->cdev), flags);
	} else
#endif
		rc = wait_event_interruptible(raw3270_wait_queue,
			test_and_clear_bit(RAW3270_FLAGS_ATTN, &rp->flags));
	if (rc)
		return rc;

	/*
	 * The device accepted the 'read partition' command. Now
	 * set up a read ccw and issue it.
	 */
	rp->init_request.ccw.cmd_code = TC_READMOD;
	rp->init_request.ccw.flags = CCW_FLAG_SLI;
	rp->init_request.ccw.count = sizeof(rp->init_data);
	rp->init_request.ccw.cda = (__u32) __pa(rp->init_data);
	rc = raw3270_start_init(rp, &raw3270_init_view, &rp->init_request);
	if (rc)
		return rc;
	/* Got a Query Reply */
	count = sizeof(rp->init_data) - rp->init_request.rescnt;
	uap = (struct raw3270_ua *) (rp->init_data + 1);
	/* Paranoia check. */
	if (rp->init_data[0] != 0x88 || uap->uab.qcode != 0x81)
		return -EOPNOTSUPP;
	/* Copy rows/columns of default Usable Area */
	rp->rows = uap->uab.h;
	rp->cols = uap->uab.w;
	/* Check for 14 bit addressing */
	if ((uap->uab.flags0 & 0x0d) == 0x01)
		set_bit(RAW3270_FLAGS_14BITADDR, &rp->flags);
	/* Check for Alternate Usable Area */
	if (uap->uab.l == sizeof(struct raw3270_ua) &&
	    uap->aua.sdpid == 0x02) {
		rp->rows = uap->aua.hauai;
		rp->cols = uap->aua.wauai;
	}
	return 0;
}

static int
raw3270_size_device(struct raw3270 *rp)
{
	int rc;

	mutex_lock(&raw3270_init_mutex);
	rp->view = &raw3270_init_view;
	raw3270_init_view.dev = rp;
	if (MACHINE_IS_VM)
		rc = __raw3270_size_device_vm(rp);
	else
		rc = __raw3270_size_device(rp);
	raw3270_init_view.dev = NULL;
	rp->view = NULL;
	mutex_unlock(&raw3270_init_mutex);
	if (rc == 0) {	/* Found something. */
		/* Try to find a model. */
		rp->model = 0;
		if (rp->rows == 24 && rp->cols == 80)
			rp->model = 2;
		if (rp->rows == 32 && rp->cols == 80)
			rp->model = 3;
		if (rp->rows == 43 && rp->cols == 80)
			rp->model = 4;
		if (rp->rows == 27 && rp->cols == 132)
			rp->model = 5;
	} else {
		/* Couldn't detect size. Use default model 2. */
		rp->model = 2;
		rp->rows = 24;
		rp->cols = 80;
		return 0;
	}
	return rc;
}

static int
raw3270_reset_device(struct raw3270 *rp)
{
	int rc;

	mutex_lock(&raw3270_init_mutex);
	memset(&rp->init_request, 0, sizeof(rp->init_request));
	memset(&rp->init_data, 0, sizeof(rp->init_data));
	/* Store reset data stream to init_data/init_request */
	rp->init_data[0] = TW_KR;
	INIT_LIST_HEAD(&rp->init_request.list);
	rp->init_request.ccw.cmd_code = TC_EWRITEA;
	rp->init_request.ccw.flags = CCW_FLAG_SLI;
	rp->init_request.ccw.count = 1;
	rp->init_request.ccw.cda = (__u32) __pa(rp->init_data);
	rp->view = &raw3270_init_view;
	raw3270_init_view.dev = rp;
	rc = raw3270_start_init(rp, &raw3270_init_view, &rp->init_request);
	raw3270_init_view.dev = NULL;
	rp->view = NULL;
	mutex_unlock(&raw3270_init_mutex);
	return rc;
}

int
raw3270_reset(struct raw3270_view *view)
{
	struct raw3270 *rp;
	int rc;

	rp = view->dev;
	if (!rp || rp->view != view)
		rc = -EACCES;
	else if (!test_bit(RAW3270_FLAGS_READY, &rp->flags))
		rc = -ENODEV;
	else
		rc = raw3270_reset_device(view->dev);
	return rc;
}

/*
 * Setup new 3270 device.
 */
static int
raw3270_setup_device(struct ccw_device *cdev, struct raw3270 *rp, char *ascebc)
{
	struct list_head *l;
	struct raw3270 *tmp;
	int minor;

	memset(rp, 0, sizeof(struct raw3270));
	/* Copy ebcdic -> ascii translation table. */
	memcpy(ascebc, _ascebc, 256);
	if (tubxcorrect) {
		/* correct brackets and circumflex */
		ascebc['['] = 0xad;
		ascebc[']'] = 0xbd;
		ascebc['^'] = 0xb0;
	}
	rp->ascebc = ascebc;

	/* Set defaults. */
	rp->rows = 24;
	rp->cols = 80;

	INIT_LIST_HEAD(&rp->req_queue);
	INIT_LIST_HEAD(&rp->view_list);

	/*
	 * Add device to list and find the smallest unused minor
	 * number for it. Note: there is no device with minor 0,
	 * see special case for fs3270.c:fs3270_open().
	 */
	mutex_lock(&raw3270_mutex);
	/* Keep the list sorted. */
	minor = RAW3270_FIRSTMINOR;
	rp->minor = -1;
	list_for_each(l, &raw3270_devices) {
		tmp = list_entry(l, struct raw3270, list);
		if (tmp->minor > minor) {
			rp->minor = minor;
			__list_add(&rp->list, l->prev, l);
			break;
		}
		minor++;
	}
	if (rp->minor == -1 && minor < RAW3270_MAXDEVS + RAW3270_FIRSTMINOR) {
		rp->minor = minor;
		list_add_tail(&rp->list, &raw3270_devices);
	}
	mutex_unlock(&raw3270_mutex);
	/* No free minor number? Then give up. */
	if (rp->minor == -1)
		return -EUSERS;
	rp->cdev = cdev;
	cdev->dev.driver_data = rp;
	cdev->handler = raw3270_irq;
	return 0;
}

#ifdef CONFIG_TN3270_CONSOLE
/*
 * Setup 3270 device configured as console.
 */
struct raw3270 __init *raw3270_setup_console(struct ccw_device *cdev)
{
	struct raw3270 *rp;
	char *ascebc;
	int rc;

	rp = (struct raw3270 *) alloc_bootmem_low(sizeof(struct raw3270));
	ascebc = (char *) alloc_bootmem(256);
	rc = raw3270_setup_device(cdev, rp, ascebc);
	if (rc)
		return ERR_PTR(rc);
	set_bit(RAW3270_FLAGS_CONSOLE, &rp->flags);
	rc = raw3270_reset_device(rp);
	if (rc)
		return ERR_PTR(rc);
	rc = raw3270_size_device(rp);
	if (rc)
		return ERR_PTR(rc);
	rc = raw3270_reset_device(rp);
	if (rc)
		return ERR_PTR(rc);
	set_bit(RAW3270_FLAGS_READY, &rp->flags);
	return rp;
}

void
raw3270_wait_cons_dev(struct raw3270 *rp)
{
	unsigned long flags;

	spin_lock_irqsave(get_ccwdev_lock(rp->cdev), flags);
	wait_cons_dev();
	spin_unlock_irqrestore(get_ccwdev_lock(rp->cdev), flags);
}

#endif

/*
 * Create a 3270 device structure.
 */
static struct raw3270 *
raw3270_create_device(struct ccw_device *cdev)
{
	struct raw3270 *rp;
	char *ascebc;
	int rc;

	rp = kmalloc(sizeof(struct raw3270), GFP_KERNEL | GFP_DMA);
	if (!rp)
		return ERR_PTR(-ENOMEM);
	ascebc = kmalloc(256, GFP_KERNEL);
	if (!ascebc) {
		kfree(rp);
		return ERR_PTR(-ENOMEM);
	}
	rc = raw3270_setup_device(cdev, rp, ascebc);
	if (rc) {
		kfree(rp->ascebc);
		kfree(rp);
		rp = ERR_PTR(rc);
	}
	/* Get reference to ccw_device structure. */
	get_device(&cdev->dev);
	return rp;
}

/*
 * Activate a view.
 */
int
raw3270_activate_view(struct raw3270_view *view)
{
	struct raw3270 *rp;
	struct raw3270_view *oldview, *nv;
	unsigned long flags;
	int rc;

	rp = view->dev;
	if (!rp)
		return -ENODEV;
	spin_lock_irqsave(get_ccwdev_lock(rp->cdev), flags);
	if (rp->view == view)
		rc = 0;
	else if (!test_bit(RAW3270_FLAGS_READY, &rp->flags))
		rc = -ENODEV;
	else {
		oldview = NULL;
		if (rp->view) {
			oldview = rp->view;
			oldview->fn->deactivate(oldview);
		}
		rp->view = view;
		rc = view->fn->activate(view);
		if (rc) {
			/* Didn't work. Try to reactivate the old view. */
			rp->view = oldview;
			if (!oldview || oldview->fn->activate(oldview) != 0) {
				/* Didn't work as well. Try any other view. */
				list_for_each_entry(nv, &rp->view_list, list)
					if (nv != view && nv != oldview) {
						rp->view = nv;
						if (nv->fn->activate(nv) == 0)
							break;
						rp->view = NULL;
					}
			}
		}
	}
	spin_unlock_irqrestore(get_ccwdev_lock(rp->cdev), flags);
	return rc;
}

/*
 * Deactivate current view.
 */
void
raw3270_deactivate_view(struct raw3270_view *view)
{
	unsigned long flags;
	struct raw3270 *rp;

	rp = view->dev;
	if (!rp)
		return;
	spin_lock_irqsave(get_ccwdev_lock(rp->cdev), flags);
	if (rp->view == view) {
		view->fn->deactivate(view);
		rp->view = NULL;
		/* Move deactivated view to end of list. */
		list_del_init(&view->list);
		list_add_tail(&view->list, &rp->view_list);
		/* Try to activate another view. */
		if (test_bit(RAW3270_FLAGS_READY, &rp->flags)) {
			list_for_each_entry(view, &rp->view_list, list) {
				rp->view = view;
				if (view->fn->activate(view) == 0)
					break;
				rp->view = NULL;
			}
		}
	}
	spin_unlock_irqrestore(get_ccwdev_lock(rp->cdev), flags);
}

/*
 * Add view to device with minor "minor".
 */
int
raw3270_add_view(struct raw3270_view *view, struct raw3270_fn *fn, int minor)
{
	unsigned long flags;
	struct raw3270 *rp;
	int rc;

	if (minor <= 0)
		return -ENODEV;
	mutex_lock(&raw3270_mutex);
	rc = -ENODEV;
	list_for_each_entry(rp, &raw3270_devices, list) {
		if (rp->minor != minor)
			continue;
		spin_lock_irqsave(get_ccwdev_lock(rp->cdev), flags);
		if (test_bit(RAW3270_FLAGS_READY, &rp->flags)) {
			atomic_set(&view->ref_count, 2);
			view->dev = rp;
			view->fn = fn;
			view->model = rp->model;
			view->rows = rp->rows;
			view->cols = rp->cols;
			view->ascebc = rp->ascebc;
			spin_lock_init(&view->lock);
			list_add(&view->list, &rp->view_list);
			rc = 0;
		}
		spin_unlock_irqrestore(get_ccwdev_lock(rp->cdev), flags);
		break;
	}
	mutex_unlock(&raw3270_mutex);
	return rc;
}

/*
 * Find specific view of device with minor "minor".
 */
struct raw3270_view *
raw3270_find_view(struct raw3270_fn *fn, int minor)
{
	struct raw3270 *rp;
	struct raw3270_view *view, *tmp;
	unsigned long flags;

	mutex_lock(&raw3270_mutex);
	view = ERR_PTR(-ENODEV);
	list_for_each_entry(rp, &raw3270_devices, list) {
		if (rp->minor != minor)
			continue;
		spin_lock_irqsave(get_ccwdev_lock(rp->cdev), flags);
		if (test_bit(RAW3270_FLAGS_READY, &rp->flags)) {
			view = ERR_PTR(-ENOENT);
			list_for_each_entry(tmp, &rp->view_list, list) {
				if (tmp->fn == fn) {
					raw3270_get_view(tmp);
					view = tmp;
					break;
				}
			}
		}
		spin_unlock_irqrestore(get_ccwdev_lock(rp->cdev), flags);
		break;
	}
	mutex_unlock(&raw3270_mutex);
	return view;
}

/*
 * Remove view from device and free view structure via call to view->fn->free.
 */
void
raw3270_del_view(struct raw3270_view *view)
{
	unsigned long flags;
	struct raw3270 *rp;
	struct raw3270_view *nv;

	rp = view->dev;
	spin_lock_irqsave(get_ccwdev_lock(rp->cdev), flags);
	if (rp->view == view) {
		view->fn->deactivate(view);
		rp->view = NULL;
	}
	list_del_init(&view->list);
	if (!rp->view && test_bit(RAW3270_FLAGS_READY, &rp->flags)) {
		/* Try to activate another view. */
		list_for_each_entry(nv, &rp->view_list, list) {
			if (nv->fn->activate(nv) == 0) {
				rp->view = nv;
				break;
			}
		}
	}
	spin_unlock_irqrestore(get_ccwdev_lock(rp->cdev), flags);
	/* Wait for reference counter to drop to zero. */
	atomic_dec(&view->ref_count);
	wait_event(raw3270_wait_queue, atomic_read(&view->ref_count) == 0);
	if (view->fn->free)
		view->fn->free(view);
}

/*
 * Remove a 3270 device structure.
 */
static void
raw3270_delete_device(struct raw3270 *rp)
{
	struct ccw_device *cdev;

	/* Remove from device chain. */
	mutex_lock(&raw3270_mutex);
	if (rp->clttydev && !IS_ERR(rp->clttydev))
		class_device_destroy(class3270,
				     MKDEV(IBM_TTY3270_MAJOR, rp->minor));
	if (rp->cltubdev && !IS_ERR(rp->cltubdev))
		class_device_destroy(class3270,
				     MKDEV(IBM_FS3270_MAJOR, rp->minor));
	list_del_init(&rp->list);
	mutex_unlock(&raw3270_mutex);

	/* Disconnect from ccw_device. */
	cdev = rp->cdev;
	rp->cdev = NULL;
	cdev->dev.driver_data = NULL;
	cdev->handler = NULL;

	/* Put ccw_device structure. */
	put_device(&cdev->dev);

	/* Now free raw3270 structure. */
	kfree(rp->ascebc);
	kfree(rp);
}

static int
raw3270_probe (struct ccw_device *cdev)
{
	return 0;
}

/*
 * Additional attributes for a 3270 device
 */
static ssize_t
raw3270_model_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%i\n",
			((struct raw3270 *) dev->driver_data)->model);
}
static DEVICE_ATTR(model, 0444, raw3270_model_show, NULL);

static ssize_t
raw3270_rows_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%i\n",
			((struct raw3270 *) dev->driver_data)->rows);
}
static DEVICE_ATTR(rows, 0444, raw3270_rows_show, NULL);

static ssize_t
raw3270_columns_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%i\n",
			((struct raw3270 *) dev->driver_data)->cols);
}
static DEVICE_ATTR(columns, 0444, raw3270_columns_show, NULL);

static struct attribute * raw3270_attrs[] = {
	&dev_attr_model.attr,
	&dev_attr_rows.attr,
	&dev_attr_columns.attr,
	NULL,
};

static struct attribute_group raw3270_attr_group = {
	.attrs = raw3270_attrs,
};

static int raw3270_create_attributes(struct raw3270 *rp)
{
	int rc;

	rc = sysfs_create_group(&rp->cdev->dev.kobj, &raw3270_attr_group);
	if (rc)
		goto out;

	rp->clttydev = class_device_create(class3270, NULL,
					   MKDEV(IBM_TTY3270_MAJOR, rp->minor),
					   &rp->cdev->dev, "tty%s",
					   rp->cdev->dev.bus_id);
	if (IS_ERR(rp->clttydev)) {
		rc = PTR_ERR(rp->clttydev);
		goto out_ttydev;
	}

	rp->cltubdev = class_device_create(class3270, NULL,
					   MKDEV(IBM_FS3270_MAJOR, rp->minor),
					   &rp->cdev->dev, "tub%s",
					   rp->cdev->dev.bus_id);
	if (!IS_ERR(rp->cltubdev))
		goto out;

	rc = PTR_ERR(rp->cltubdev);
	class_device_destroy(class3270, MKDEV(IBM_TTY3270_MAJOR, rp->minor));

out_ttydev:
	sysfs_remove_group(&rp->cdev->dev.kobj, &raw3270_attr_group);
out:
	return rc;
}

/*
 * Notifier for device addition/removal
 */
struct raw3270_notifier {
	struct list_head list;
	void (*notifier)(int, int);
};

static struct list_head raw3270_notifier = LIST_HEAD_INIT(raw3270_notifier);

int raw3270_register_notifier(void (*notifier)(int, int))
{
	struct raw3270_notifier *np;
	struct raw3270 *rp;

	np = kmalloc(sizeof(struct raw3270_notifier), GFP_KERNEL);
	if (!np)
		return -ENOMEM;
	np->notifier = notifier;
	mutex_lock(&raw3270_mutex);
	list_add_tail(&np->list, &raw3270_notifier);
	list_for_each_entry(rp, &raw3270_devices, list) {
		get_device(&rp->cdev->dev);
		notifier(rp->minor, 1);
	}
	mutex_unlock(&raw3270_mutex);
	return 0;
}

void raw3270_unregister_notifier(void (*notifier)(int, int))
{
	struct raw3270_notifier *np;

	mutex_lock(&raw3270_mutex);
	list_for_each_entry(np, &raw3270_notifier, list)
		if (np->notifier == notifier) {
			list_del(&np->list);
			kfree(np);
			break;
		}
	mutex_unlock(&raw3270_mutex);
}

/*
 * Set 3270 device online.
 */
static int
raw3270_set_online (struct ccw_device *cdev)
{
	struct raw3270 *rp;
	struct raw3270_notifier *np;
	int rc;

	rp = raw3270_create_device(cdev);
	if (IS_ERR(rp))
		return PTR_ERR(rp);
	rc = raw3270_reset_device(rp);
	if (rc)
		goto failure;
	rc = raw3270_size_device(rp);
	if (rc)
		goto failure;
	rc = raw3270_reset_device(rp);
	if (rc)
		goto failure;
	rc = raw3270_create_attributes(rp);
	if (rc)
		goto failure;
	set_bit(RAW3270_FLAGS_READY, &rp->flags);
	mutex_lock(&raw3270_mutex);
	list_for_each_entry(np, &raw3270_notifier, list)
		np->notifier(rp->minor, 1);
	mutex_unlock(&raw3270_mutex);
	return 0;

failure:
	raw3270_delete_device(rp);
	return rc;
}

/*
 * Remove 3270 device structure.
 */
static void
raw3270_remove (struct ccw_device *cdev)
{
	unsigned long flags;
	struct raw3270 *rp;
	struct raw3270_view *v;
	struct raw3270_notifier *np;

	rp = cdev->dev.driver_data;
	/*
	 * _remove is the opposite of _probe; it's probe that
	 * should set up rp.  raw3270_remove gets entered for
	 * devices even if they haven't been varied online.
	 * Thus, rp may validly be NULL here.
	 */
	if (rp == NULL)
		return;
	clear_bit(RAW3270_FLAGS_READY, &rp->flags);

	sysfs_remove_group(&cdev->dev.kobj, &raw3270_attr_group);

	/* Deactivate current view and remove all views. */
	spin_lock_irqsave(get_ccwdev_lock(cdev), flags);
	if (rp->view) {
		rp->view->fn->deactivate(rp->view);
		rp->view = NULL;
	}
	while (!list_empty(&rp->view_list)) {
		v = list_entry(rp->view_list.next, struct raw3270_view, list);
		if (v->fn->release)
			v->fn->release(v);
		spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);
		raw3270_del_view(v);
		spin_lock_irqsave(get_ccwdev_lock(cdev), flags);
	}
	spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);

	mutex_lock(&raw3270_mutex);
	list_for_each_entry(np, &raw3270_notifier, list)
		np->notifier(rp->minor, 0);
	mutex_unlock(&raw3270_mutex);

	/* Reset 3270 device. */
	raw3270_reset_device(rp);
	/* And finally remove it. */
	raw3270_delete_device(rp);
}

/*
 * Set 3270 device offline.
 */
static int
raw3270_set_offline (struct ccw_device *cdev)
{
	struct raw3270 *rp;

	rp = cdev->dev.driver_data;
	if (test_bit(RAW3270_FLAGS_CONSOLE, &rp->flags))
		return -EBUSY;
	raw3270_remove(cdev);
	return 0;
}

static struct ccw_device_id raw3270_id[] = {
	{ CCW_DEVICE(0x3270, 0) },
	{ CCW_DEVICE(0x3271, 0) },
	{ CCW_DEVICE(0x3272, 0) },
	{ CCW_DEVICE(0x3273, 0) },
	{ CCW_DEVICE(0x3274, 0) },
	{ CCW_DEVICE(0x3275, 0) },
	{ CCW_DEVICE(0x3276, 0) },
	{ CCW_DEVICE(0x3277, 0) },
	{ CCW_DEVICE(0x3278, 0) },
	{ CCW_DEVICE(0x3279, 0) },
	{ CCW_DEVICE(0x3174, 0) },
	{ /* end of list */ },
};

static struct ccw_driver raw3270_ccw_driver = {
	.name		= "3270",
	.owner		= THIS_MODULE,
	.ids		= raw3270_id,
	.probe		= &raw3270_probe,
	.remove		= &raw3270_remove,
	.set_online	= &raw3270_set_online,
	.set_offline	= &raw3270_set_offline,
};

static int
raw3270_init(void)
{
	struct raw3270 *rp;
	int rc;

	if (raw3270_registered)
		return 0;
	raw3270_registered = 1;
	rc = ccw_driver_register(&raw3270_ccw_driver);
	if (rc == 0) {
		/* Create attributes for early (= console) device. */
		mutex_lock(&raw3270_mutex);
		class3270 = class_create(THIS_MODULE, "3270");
		list_for_each_entry(rp, &raw3270_devices, list) {
			get_device(&rp->cdev->dev);
			raw3270_create_attributes(rp);
		}
		mutex_unlock(&raw3270_mutex);
	}
	return rc;
}

static void
raw3270_exit(void)
{
	ccw_driver_unregister(&raw3270_ccw_driver);
	class_destroy(class3270);
}

MODULE_LICENSE("GPL");

module_init(raw3270_init);
module_exit(raw3270_exit);

EXPORT_SYMBOL(raw3270_request_alloc);
EXPORT_SYMBOL(raw3270_request_free);
EXPORT_SYMBOL(raw3270_request_reset);
EXPORT_SYMBOL(raw3270_request_set_cmd);
EXPORT_SYMBOL(raw3270_request_add_data);
EXPORT_SYMBOL(raw3270_request_set_data);
EXPORT_SYMBOL(raw3270_request_set_idal);
EXPORT_SYMBOL(raw3270_buffer_address);
EXPORT_SYMBOL(raw3270_add_view);
EXPORT_SYMBOL(raw3270_del_view);
EXPORT_SYMBOL(raw3270_find_view);
EXPORT_SYMBOL(raw3270_activate_view);
EXPORT_SYMBOL(raw3270_deactivate_view);
EXPORT_SYMBOL(raw3270_start);
EXPORT_SYMBOL(raw3270_start_locked);
EXPORT_SYMBOL(raw3270_start_irq);
EXPORT_SYMBOL(raw3270_reset);
EXPORT_SYMBOL(raw3270_register_notifier);
EXPORT_SYMBOL(raw3270_unregister_notifier);
EXPORT_SYMBOL(raw3270_wait_queue);
