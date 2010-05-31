/*
 *  Character device driver for extended error reporting.
 *
 *  Copyright (C) 2005 IBM Corporation
 *  extended error reporting for DASD ECKD devices
 *  Author(s): Stefan Weinhuber <wein@de.ibm.com>
 */

#define KMSG_COMPONENT "dasd-eckd"

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/smp_lock.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <asm/ebcdic.h>

#include "dasd_int.h"
#include "dasd_eckd.h"

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER "dasd(eer):"

/*
 * SECTION: the internal buffer
 */

/*
 * The internal buffer is meant to store obaque blobs of data, so it does
 * not know of higher level concepts like triggers.
 * It consists of a number of pages that are used as a ringbuffer. Each data
 * blob is stored in a simple record that consists of an integer, which
 * contains the size of the following data, and the data bytes themselfes.
 *
 * To allow for multiple independent readers we create one internal buffer
 * each time the device is opened and destroy the buffer when the file is
 * closed again. The number of pages used for this buffer is determined by
 * the module parmeter eer_pages.
 *
 * One record can be written to a buffer by using the functions
 * - dasd_eer_start_record (one time per record to write the size to the
 *                          buffer and reserve the space for the data)
 * - dasd_eer_write_buffer (one or more times per record to write the data)
 * The data can be written in several steps but you will have to compute
 * the total size up front for the invocation of dasd_eer_start_record.
 * If the ringbuffer is full, dasd_eer_start_record will remove the required
 * number of old records.
 *
 * A record is typically read in two steps, first read the integer that
 * specifies the size of the following data, then read the data.
 * Both can be done by
 * - dasd_eer_read_buffer
 *
 * For all mentioned functions you need to get the bufferlock first and keep
 * it until a complete record is written or read.
 *
 * All information necessary to keep track of an internal buffer is kept in
 * a struct eerbuffer. The buffer specific to a file pointer is strored in
 * the private_data field of that file. To be able to write data to all
 * existing buffers, each buffer is also added to the bufferlist.
 * If the user does not want to read a complete record in one go, we have to
 * keep track of the rest of the record. residual stores the number of bytes
 * that are still to deliver. If the rest of the record is invalidated between
 * two reads then residual will be set to -1 so that the next read will fail.
 * All entries in the eerbuffer structure are protected with the bufferlock.
 * To avoid races between writing to a buffer on the one side and creating
 * and destroying buffers on the other side, the bufferlock must also be used
 * to protect the bufferlist.
 */

static int eer_pages = 5;
module_param(eer_pages, int, S_IRUGO|S_IWUSR);

struct eerbuffer {
	struct list_head list;
	char **buffer;
	int buffersize;
	int buffer_page_count;
	int head;
        int tail;
	int residual;
};

static LIST_HEAD(bufferlist);
static DEFINE_SPINLOCK(bufferlock);
static DECLARE_WAIT_QUEUE_HEAD(dasd_eer_read_wait_queue);

/*
 * How many free bytes are available on the buffer.
 * Needs to be called with bufferlock held.
 */
static int dasd_eer_get_free_bytes(struct eerbuffer *eerb)
{
	if (eerb->head < eerb->tail)
		return eerb->tail - eerb->head - 1;
	return eerb->buffersize - eerb->head + eerb->tail -1;
}

/*
 * How many bytes of buffer space are used.
 * Needs to be called with bufferlock held.
 */
static int dasd_eer_get_filled_bytes(struct eerbuffer *eerb)
{

	if (eerb->head >= eerb->tail)
		return eerb->head - eerb->tail;
	return eerb->buffersize - eerb->tail + eerb->head;
}

/*
 * The dasd_eer_write_buffer function just copies count bytes of data
 * to the buffer. Make sure to call dasd_eer_start_record first, to
 * make sure that enough free space is available.
 * Needs to be called with bufferlock held.
 */
static void dasd_eer_write_buffer(struct eerbuffer *eerb,
				  char *data, int count)
{

	unsigned long headindex,localhead;
	unsigned long rest, len;
	char *nextdata;

	nextdata = data;
	rest = count;
	while (rest > 0) {
 		headindex = eerb->head / PAGE_SIZE;
 		localhead = eerb->head % PAGE_SIZE;
		len = min(rest, PAGE_SIZE - localhead);
		memcpy(eerb->buffer[headindex]+localhead, nextdata, len);
		nextdata += len;
		rest -= len;
		eerb->head += len;
		if (eerb->head == eerb->buffersize)
			eerb->head = 0; /* wrap around */
		BUG_ON(eerb->head > eerb->buffersize);
	}
}

/*
 * Needs to be called with bufferlock held.
 */
static int dasd_eer_read_buffer(struct eerbuffer *eerb, char *data, int count)
{

	unsigned long tailindex,localtail;
	unsigned long rest, len, finalcount;
	char *nextdata;

	finalcount = min(count, dasd_eer_get_filled_bytes(eerb));
	nextdata = data;
	rest = finalcount;
	while (rest > 0) {
 		tailindex = eerb->tail / PAGE_SIZE;
 		localtail = eerb->tail % PAGE_SIZE;
		len = min(rest, PAGE_SIZE - localtail);
		memcpy(nextdata, eerb->buffer[tailindex] + localtail, len);
		nextdata += len;
		rest -= len;
		eerb->tail += len;
		if (eerb->tail == eerb->buffersize)
			eerb->tail = 0; /* wrap around */
		BUG_ON(eerb->tail > eerb->buffersize);
	}
	return finalcount;
}

/*
 * Whenever you want to write a blob of data to the internal buffer you
 * have to start by using this function first. It will write the number
 * of bytes that will be written to the buffer. If necessary it will remove
 * old records to make room for the new one.
 * Needs to be called with bufferlock held.
 */
static int dasd_eer_start_record(struct eerbuffer *eerb, int count)
{
	int tailcount;

	if (count + sizeof(count) > eerb->buffersize)
		return -ENOMEM;
	while (dasd_eer_get_free_bytes(eerb) < count + sizeof(count)) {
		if (eerb->residual > 0) {
			eerb->tail += eerb->residual;
			if (eerb->tail >= eerb->buffersize)
				eerb->tail -= eerb->buffersize;
			eerb->residual = -1;
		}
		dasd_eer_read_buffer(eerb, (char *) &tailcount,
				     sizeof(tailcount));
		eerb->tail += tailcount;
		if (eerb->tail >= eerb->buffersize)
			eerb->tail -= eerb->buffersize;
	}
	dasd_eer_write_buffer(eerb, (char*) &count, sizeof(count));

	return 0;
};

/*
 * Release pages that are not used anymore.
 */
static void dasd_eer_free_buffer_pages(char **buf, int no_pages)
{
	int i;

	for (i = 0; i < no_pages; i++)
		free_page((unsigned long) buf[i]);
}

/*
 * Allocate a new set of memory pages.
 */
static int dasd_eer_allocate_buffer_pages(char **buf, int no_pages)
{
	int i;

	for (i = 0; i < no_pages; i++) {
		buf[i] = (char *) get_zeroed_page(GFP_KERNEL);
		if (!buf[i]) {
			dasd_eer_free_buffer_pages(buf, i);
			return -ENOMEM;
		}
	}
	return 0;
}

/*
 * SECTION: The extended error reporting functionality
 */

/*
 * When a DASD device driver wants to report an error, it calls the
 * function dasd_eer_write and gives the respective trigger ID as
 * parameter. Currently there are four kinds of triggers:
 *
 * DASD_EER_FATALERROR:  all kinds of unrecoverable I/O problems
 * DASD_EER_PPRCSUSPEND: PPRC was suspended
 * DASD_EER_NOPATH:      There is no path to the device left.
 * DASD_EER_STATECHANGE: The state of the device has changed.
 *
 * For the first three triggers all required information can be supplied by
 * the caller. For these triggers a record is written by the function
 * dasd_eer_write_standard_trigger.
 *
 * The DASD_EER_STATECHANGE trigger is special since a sense subsystem
 * status ccw need to be executed to gather the necessary sense data first.
 * The dasd_eer_snss function will queue the SNSS request and the request
 * callback will then call dasd_eer_write with the DASD_EER_STATCHANGE
 * trigger.
 *
 * To avoid memory allocations at runtime, the necessary memory is allocated
 * when the extended error reporting is enabled for a device (by
 * dasd_eer_probe). There is one sense subsystem status request for each
 * eer enabled DASD device. The presence of the cqr in device->eer_cqr
 * indicates that eer is enable for the device. The use of the snss request
 * is protected by the DASD_FLAG_EER_IN_USE bit. When this flag indicates
 * that the cqr is currently in use, dasd_eer_snss cannot start a second
 * request but sets the DASD_FLAG_EER_SNSS flag instead. The callback of
 * the SNSS request will check the bit and call dasd_eer_snss again.
 */

#define SNSS_DATA_SIZE 44

#define DASD_EER_BUSID_SIZE 10
struct dasd_eer_header {
	__u32 total_size;
	__u32 trigger;
	__u64 tv_sec;
	__u64 tv_usec;
	char busid[DASD_EER_BUSID_SIZE];
} __attribute__ ((packed));

/*
 * The following function can be used for those triggers that have
 * all necessary data available when the function is called.
 * If the parameter cqr is not NULL, the chain of requests will be searched
 * for valid sense data, and all valid sense data sets will be added to
 * the triggers data.
 */
static void dasd_eer_write_standard_trigger(struct dasd_device *device,
					    struct dasd_ccw_req *cqr,
					    int trigger)
{
	struct dasd_ccw_req *temp_cqr;
	int data_size;
	struct timeval tv;
	struct dasd_eer_header header;
	unsigned long flags;
	struct eerbuffer *eerb;
	char *sense;

	/* go through cqr chain and count the valid sense data sets */
	data_size = 0;
	for (temp_cqr = cqr; temp_cqr; temp_cqr = temp_cqr->refers)
		if (dasd_get_sense(&temp_cqr->irb))
			data_size += 32;

	header.total_size = sizeof(header) + data_size + 4; /* "EOR" */
	header.trigger = trigger;
	do_gettimeofday(&tv);
	header.tv_sec = tv.tv_sec;
	header.tv_usec = tv.tv_usec;
	strncpy(header.busid, dev_name(&device->cdev->dev),
		DASD_EER_BUSID_SIZE);

	spin_lock_irqsave(&bufferlock, flags);
	list_for_each_entry(eerb, &bufferlist, list) {
		dasd_eer_start_record(eerb, header.total_size);
		dasd_eer_write_buffer(eerb, (char *) &header, sizeof(header));
		for (temp_cqr = cqr; temp_cqr; temp_cqr = temp_cqr->refers) {
			sense = dasd_get_sense(&temp_cqr->irb);
			if (sense)
				dasd_eer_write_buffer(eerb, sense, 32);
		}
		dasd_eer_write_buffer(eerb, "EOR", 4);
	}
	spin_unlock_irqrestore(&bufferlock, flags);
	wake_up_interruptible(&dasd_eer_read_wait_queue);
}

/*
 * This function writes a DASD_EER_STATECHANGE trigger.
 */
static void dasd_eer_write_snss_trigger(struct dasd_device *device,
					struct dasd_ccw_req *cqr,
					int trigger)
{
	int data_size;
	int snss_rc;
	struct timeval tv;
	struct dasd_eer_header header;
	unsigned long flags;
	struct eerbuffer *eerb;

	snss_rc = (cqr->status == DASD_CQR_DONE) ? 0 : -EIO;
	if (snss_rc)
		data_size = 0;
	else
		data_size = SNSS_DATA_SIZE;

	header.total_size = sizeof(header) + data_size + 4; /* "EOR" */
	header.trigger = DASD_EER_STATECHANGE;
	do_gettimeofday(&tv);
	header.tv_sec = tv.tv_sec;
	header.tv_usec = tv.tv_usec;
	strncpy(header.busid, dev_name(&device->cdev->dev),
		DASD_EER_BUSID_SIZE);

	spin_lock_irqsave(&bufferlock, flags);
	list_for_each_entry(eerb, &bufferlist, list) {
		dasd_eer_start_record(eerb, header.total_size);
		dasd_eer_write_buffer(eerb, (char *) &header , sizeof(header));
		if (!snss_rc)
			dasd_eer_write_buffer(eerb, cqr->data, SNSS_DATA_SIZE);
		dasd_eer_write_buffer(eerb, "EOR", 4);
	}
	spin_unlock_irqrestore(&bufferlock, flags);
	wake_up_interruptible(&dasd_eer_read_wait_queue);
}

/*
 * This function is called for all triggers. It calls the appropriate
 * function that writes the actual trigger records.
 */
void dasd_eer_write(struct dasd_device *device, struct dasd_ccw_req *cqr,
		    unsigned int id)
{
	if (!device->eer_cqr)
		return;
	switch (id) {
	case DASD_EER_FATALERROR:
	case DASD_EER_PPRCSUSPEND:
		dasd_eer_write_standard_trigger(device, cqr, id);
		break;
	case DASD_EER_NOPATH:
		dasd_eer_write_standard_trigger(device, NULL, id);
		break;
	case DASD_EER_STATECHANGE:
		dasd_eer_write_snss_trigger(device, cqr, id);
		break;
	default: /* unknown trigger, so we write it without any sense data */
		dasd_eer_write_standard_trigger(device, NULL, id);
		break;
	}
}
EXPORT_SYMBOL(dasd_eer_write);

/*
 * Start a sense subsystem status request.
 * Needs to be called with the device held.
 */
void dasd_eer_snss(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;

	cqr = device->eer_cqr;
	if (!cqr)	/* Device not eer enabled. */
		return;
	if (test_and_set_bit(DASD_FLAG_EER_IN_USE, &device->flags)) {
		/* Sense subsystem status request in use. */
		set_bit(DASD_FLAG_EER_SNSS, &device->flags);
		return;
	}
	/* cdev is already locked, can't use dasd_add_request_head */
	clear_bit(DASD_FLAG_EER_SNSS, &device->flags);
	cqr->status = DASD_CQR_QUEUED;
	list_add(&cqr->devlist, &device->ccw_queue);
	dasd_schedule_device_bh(device);
}

/*
 * Callback function for use with sense subsystem status request.
 */
static void dasd_eer_snss_cb(struct dasd_ccw_req *cqr, void *data)
{
	struct dasd_device *device = cqr->startdev;
	unsigned long flags;

	dasd_eer_write(device, cqr, DASD_EER_STATECHANGE);
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	if (device->eer_cqr == cqr) {
		clear_bit(DASD_FLAG_EER_IN_USE, &device->flags);
		if (test_bit(DASD_FLAG_EER_SNSS, &device->flags))
			/* Another SNSS has been requested in the meantime. */
			dasd_eer_snss(device);
		cqr = NULL;
	}
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	if (cqr)
		/*
		 * Extended error recovery has been switched off while
		 * the SNSS request was running. It could even have
		 * been switched off and on again in which case there
		 * is a new ccw in device->eer_cqr. Free the "old"
		 * snss request now.
		 */
		dasd_kfree_request(cqr, device);
}

/*
 * Enable error reporting on a given device.
 */
int dasd_eer_enable(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;
	unsigned long flags;
	struct ccw1 *ccw;

	if (device->eer_cqr)
		return 0;

	if (!device->discipline || strcmp(device->discipline->name, "ECKD"))
		return -EPERM;	/* FIXME: -EMEDIUMTYPE ? */

	cqr = dasd_kmalloc_request(DASD_ECKD_MAGIC, 1 /* SNSS */,
				   SNSS_DATA_SIZE, device);
	if (IS_ERR(cqr))
		return -ENOMEM;

	cqr->startdev = device;
	cqr->retries = 255;
	cqr->expires = 10 * HZ;
	clear_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);

	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_SNSS;
	ccw->count = SNSS_DATA_SIZE;
	ccw->flags = 0;
	ccw->cda = (__u32)(addr_t) cqr->data;

	cqr->buildclk = get_clock();
	cqr->status = DASD_CQR_FILLED;
	cqr->callback = dasd_eer_snss_cb;

	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	if (!device->eer_cqr) {
		device->eer_cqr = cqr;
		cqr = NULL;
	}
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	if (cqr)
		dasd_kfree_request(cqr, device);
	return 0;
}

/*
 * Disable error reporting on a given device.
 */
void dasd_eer_disable(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;
	unsigned long flags;
	int in_use;

	if (!device->eer_cqr)
		return;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	cqr = device->eer_cqr;
	device->eer_cqr = NULL;
	clear_bit(DASD_FLAG_EER_SNSS, &device->flags);
	in_use = test_and_clear_bit(DASD_FLAG_EER_IN_USE, &device->flags);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	if (cqr && !in_use)
		dasd_kfree_request(cqr, device);
}

/*
 * SECTION: the device operations
 */

/*
 * On the one side we need a lock to access our internal buffer, on the
 * other side a copy_to_user can sleep. So we need to copy the data we have
 * to transfer in a readbuffer, which is protected by the readbuffer_mutex.
 */
static char readbuffer[PAGE_SIZE];
static DEFINE_MUTEX(readbuffer_mutex);

static int dasd_eer_open(struct inode *inp, struct file *filp)
{
	struct eerbuffer *eerb;
	unsigned long flags;

	eerb = kzalloc(sizeof(struct eerbuffer), GFP_KERNEL);
	if (!eerb)
		return -ENOMEM;
	eerb->buffer_page_count = eer_pages;
	if (eerb->buffer_page_count < 1 ||
	    eerb->buffer_page_count > INT_MAX / PAGE_SIZE) {
		kfree(eerb);
		DBF_EVENT(DBF_WARNING, "can't open device since module "
			"parameter eer_pages is smaller than 1 or"
			" bigger than %d", (int)(INT_MAX / PAGE_SIZE));
		return -EINVAL;
	}
	eerb->buffersize = eerb->buffer_page_count * PAGE_SIZE;
	eerb->buffer = kmalloc(eerb->buffer_page_count * sizeof(char *),
			       GFP_KERNEL);
        if (!eerb->buffer) {
		kfree(eerb);
                return -ENOMEM;
	}
	if (dasd_eer_allocate_buffer_pages(eerb->buffer,
					   eerb->buffer_page_count)) {
		kfree(eerb->buffer);
		kfree(eerb);
		return -ENOMEM;
	}
	filp->private_data = eerb;
	spin_lock_irqsave(&bufferlock, flags);
	list_add(&eerb->list, &bufferlist);
	spin_unlock_irqrestore(&bufferlock, flags);

	return nonseekable_open(inp,filp);
}

static int dasd_eer_close(struct inode *inp, struct file *filp)
{
	struct eerbuffer *eerb;
	unsigned long flags;

	eerb = (struct eerbuffer *) filp->private_data;
	spin_lock_irqsave(&bufferlock, flags);
	list_del(&eerb->list);
	spin_unlock_irqrestore(&bufferlock, flags);
	dasd_eer_free_buffer_pages(eerb->buffer, eerb->buffer_page_count);
	kfree(eerb->buffer);
	kfree(eerb);

	return 0;
}

static ssize_t dasd_eer_read(struct file *filp, char __user *buf,
			     size_t count, loff_t *ppos)
{
	int tc,rc;
	int tailcount,effective_count;
        unsigned long flags;
	struct eerbuffer *eerb;

	eerb = (struct eerbuffer *) filp->private_data;
	if (mutex_lock_interruptible(&readbuffer_mutex))
		return -ERESTARTSYS;

	spin_lock_irqsave(&bufferlock, flags);

	if (eerb->residual < 0) { /* the remainder of this record */
		                  /* has been deleted             */
		eerb->residual = 0;
		spin_unlock_irqrestore(&bufferlock, flags);
		mutex_unlock(&readbuffer_mutex);
		return -EIO;
	} else if (eerb->residual > 0) {
		/* OK we still have a second half of a record to deliver */
		effective_count = min(eerb->residual, (int) count);
		eerb->residual -= effective_count;
	} else {
		tc = 0;
		while (!tc) {
			tc = dasd_eer_read_buffer(eerb, (char *) &tailcount,
						  sizeof(tailcount));
			if (!tc) {
				/* no data available */
				spin_unlock_irqrestore(&bufferlock, flags);
				mutex_unlock(&readbuffer_mutex);
				if (filp->f_flags & O_NONBLOCK)
					return -EAGAIN;
				rc = wait_event_interruptible(
					dasd_eer_read_wait_queue,
					eerb->head != eerb->tail);
				if (rc)
					return rc;
				if (mutex_lock_interruptible(&readbuffer_mutex))
					return -ERESTARTSYS;
				spin_lock_irqsave(&bufferlock, flags);
			}
		}
		WARN_ON(tc != sizeof(tailcount));
		effective_count = min(tailcount,(int)count);
		eerb->residual = tailcount - effective_count;
	}

	tc = dasd_eer_read_buffer(eerb, readbuffer, effective_count);
	WARN_ON(tc != effective_count);

	spin_unlock_irqrestore(&bufferlock, flags);

	if (copy_to_user(buf, readbuffer, effective_count)) {
		mutex_unlock(&readbuffer_mutex);
		return -EFAULT;
	}

	mutex_unlock(&readbuffer_mutex);
	return effective_count;
}

static unsigned int dasd_eer_poll(struct file *filp, poll_table *ptable)
{
	unsigned int mask;
	unsigned long flags;
	struct eerbuffer *eerb;

	eerb = (struct eerbuffer *) filp->private_data;
	poll_wait(filp, &dasd_eer_read_wait_queue, ptable);
	spin_lock_irqsave(&bufferlock, flags);
	if (eerb->head != eerb->tail)
		mask = POLLIN | POLLRDNORM ;
	else
		mask = 0;
	spin_unlock_irqrestore(&bufferlock, flags);
	return mask;
}

static const struct file_operations dasd_eer_fops = {
	.open		= &dasd_eer_open,
	.release	= &dasd_eer_close,
	.read		= &dasd_eer_read,
	.poll		= &dasd_eer_poll,
	.owner		= THIS_MODULE,
};

static struct miscdevice *dasd_eer_dev = NULL;

int __init dasd_eer_init(void)
{
	int rc;

	dasd_eer_dev = kzalloc(sizeof(*dasd_eer_dev), GFP_KERNEL);
	if (!dasd_eer_dev)
		return -ENOMEM;

	dasd_eer_dev->minor = MISC_DYNAMIC_MINOR;
	dasd_eer_dev->name  = "dasd_eer";
	dasd_eer_dev->fops  = &dasd_eer_fops;

	rc = misc_register(dasd_eer_dev);
	if (rc) {
		kfree(dasd_eer_dev);
		dasd_eer_dev = NULL;
		DBF_EVENT(DBF_ERR, "%s", "dasd_eer_init could not "
		       "register misc device");
		return rc;
	}

	return 0;
}

void dasd_eer_exit(void)
{
	if (dasd_eer_dev) {
		WARN_ON(misc_deregister(dasd_eer_dev) != 0);
		kfree(dasd_eer_dev);
		dasd_eer_dev = NULL;
	}
}
