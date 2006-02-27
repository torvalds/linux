/*
 * The USB Monitor, inspired by Dave Harding's USBMon.
 *
 * This is a text format reader.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>

#include "usb_mon.h"

/*
 * No, we do not want arbitrarily long data strings.
 * Use the binary interface if you want to capture bulk data!
 */
#define DATA_MAX  32

/*
 * Defined by USB 2.0 clause 9.3, table 9.2.
 */
#define SETUP_MAX  8

/*
 * This limit exists to prevent OOMs when the user process stops reading.
 */
#define EVENT_MAX  25

#define PRINTF_DFL  130

struct mon_event_text {
	struct list_head e_link;
	int type;		/* submit, complete, etc. */
	unsigned int pipe;	/* Pipe */
	unsigned long id;	/* From pointer, most of the time */
	unsigned int tstamp;
	int length;		/* Depends on type: xfer length or act length */
	int status;
	char setup_flag;
	char data_flag;
	unsigned char setup[SETUP_MAX];
	unsigned char data[DATA_MAX];
};

#define SLAB_NAME_SZ  30
struct mon_reader_text {
	kmem_cache_t *e_slab;
	int nevents;
	struct list_head e_list;
	struct mon_reader r;	/* In C, parent class can be placed anywhere */

	wait_queue_head_t wait;
	int printf_size;
	char *printf_buf;
	struct mutex printf_lock;

	char slab_name[SLAB_NAME_SZ];
};

static void mon_text_ctor(void *, kmem_cache_t *, unsigned long);
static void mon_text_dtor(void *, kmem_cache_t *, unsigned long);

/*
 * mon_text_submit
 * mon_text_complete
 *
 * May be called from an interrupt.
 *
 * This is called with the whole mon_bus locked, so no additional lock.
 */

static inline char mon_text_get_setup(struct mon_event_text *ep,
    struct urb *urb, char ev_type)
{

	if (!usb_pipecontrol(urb->pipe) || ev_type != 'S')
		return '-';

	if (urb->transfer_flags & URB_NO_SETUP_DMA_MAP)
		return mon_dmapeek(ep->setup, urb->setup_dma, SETUP_MAX);
	if (urb->setup_packet == NULL)
		return 'Z';	/* '0' would be not as pretty. */

	memcpy(ep->setup, urb->setup_packet, SETUP_MAX);
	return 0;
}

static inline char mon_text_get_data(struct mon_event_text *ep, struct urb *urb,
    int len, char ev_type)
{
	int pipe = urb->pipe;

	if (len <= 0)
		return 'L';
	if (len >= DATA_MAX)
		len = DATA_MAX;

	if (usb_pipein(pipe)) {
		if (ev_type == 'S')
			return '<';
	} else {
		if (ev_type == 'C')
			return '>';
	}

	/*
	 * The check to see if it's safe to poke at data has an enormous
	 * number of corner cases, but it seems that the following is
	 * more or less safe.
	 *
	 * We do not even try to look transfer_buffer, because it can
	 * contain non-NULL garbage in case the upper level promised to
	 * set DMA for the HCD.
	 */
	if (urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)
		return mon_dmapeek(ep->data, urb->transfer_dma, len);

	if (urb->transfer_buffer == NULL)
		return 'Z';	/* '0' would be not as pretty. */

	memcpy(ep->data, urb->transfer_buffer, len);
	return 0;
}

static inline unsigned int mon_get_timestamp(void)
{
	struct timeval tval;
	unsigned int stamp;

	do_gettimeofday(&tval);
	stamp = tval.tv_sec & 0xFFFF;	/* 2^32 = 4294967296. Limit to 4096s. */
	stamp = stamp * 1000000 + tval.tv_usec;
	return stamp;
}

static void mon_text_event(struct mon_reader_text *rp, struct urb *urb,
    char ev_type)
{
	struct mon_event_text *ep;
	unsigned int stamp;

	stamp = mon_get_timestamp();

	if (rp->nevents >= EVENT_MAX ||
	    (ep = kmem_cache_alloc(rp->e_slab, SLAB_ATOMIC)) == NULL) {
		rp->r.m_bus->cnt_text_lost++;
		return;
	}

	ep->type = ev_type;
	ep->pipe = urb->pipe;
	ep->id = (unsigned long) urb;
	ep->tstamp = stamp;
	ep->length = (ev_type == 'S') ?
	    urb->transfer_buffer_length : urb->actual_length;
	/* Collecting status makes debugging sense for submits, too */
	ep->status = urb->status;

	ep->setup_flag = mon_text_get_setup(ep, urb, ev_type);
	ep->data_flag = mon_text_get_data(ep, urb, ep->length, ev_type);

	rp->nevents++;
	list_add_tail(&ep->e_link, &rp->e_list);
	wake_up(&rp->wait);
}

static void mon_text_submit(void *data, struct urb *urb)
{
	struct mon_reader_text *rp = data;
	mon_text_event(rp, urb, 'S');
}

static void mon_text_complete(void *data, struct urb *urb)
{
	struct mon_reader_text *rp = data;
	mon_text_event(rp, urb, 'C');
}

/*
 * Fetch next event from the circular buffer.
 */
static struct mon_event_text *mon_text_fetch(struct mon_reader_text *rp,
    struct mon_bus *mbus)
{
	struct list_head *p;
	unsigned long flags;

	spin_lock_irqsave(&mbus->lock, flags);
	if (list_empty(&rp->e_list)) {
		spin_unlock_irqrestore(&mbus->lock, flags);
		return NULL;
	}
	p = rp->e_list.next;
	list_del(p);
	--rp->nevents;
	spin_unlock_irqrestore(&mbus->lock, flags);
	return list_entry(p, struct mon_event_text, e_link);
}

/*
 */
static int mon_text_open(struct inode *inode, struct file *file)
{
	struct mon_bus *mbus;
	struct usb_bus *ubus;
	struct mon_reader_text *rp;
	int rc;

	mutex_lock(&mon_lock);
	mbus = inode->u.generic_ip;
	ubus = mbus->u_bus;

	rp = kzalloc(sizeof(struct mon_reader_text), GFP_KERNEL);
	if (rp == NULL) {
		rc = -ENOMEM;
		goto err_alloc;
	}
	INIT_LIST_HEAD(&rp->e_list);
	init_waitqueue_head(&rp->wait);
	mutex_init(&rp->printf_lock);

	rp->printf_size = PRINTF_DFL;
	rp->printf_buf = kmalloc(rp->printf_size, GFP_KERNEL);
	if (rp->printf_buf == NULL) {
		rc = -ENOMEM;
		goto err_alloc_pr;
	}

	rp->r.m_bus = mbus;
	rp->r.r_data = rp;
	rp->r.rnf_submit = mon_text_submit;
	rp->r.rnf_complete = mon_text_complete;

	snprintf(rp->slab_name, SLAB_NAME_SZ, "mon%dt_%lx", ubus->busnum,
	    (long)rp);
	rp->e_slab = kmem_cache_create(rp->slab_name,
	    sizeof(struct mon_event_text), sizeof(long), 0,
	    mon_text_ctor, mon_text_dtor);
	if (rp->e_slab == NULL) {
		rc = -ENOMEM;
		goto err_slab;
	}

	mon_reader_add(mbus, &rp->r);

	file->private_data = rp;
	mutex_unlock(&mon_lock);
	return 0;

// err_busy:
//	kmem_cache_destroy(rp->e_slab);
err_slab:
	kfree(rp->printf_buf);
err_alloc_pr:
	kfree(rp);
err_alloc:
	mutex_unlock(&mon_lock);
	return rc;
}

/*
 * For simplicity, we read one record in one system call and throw out
 * what does not fit. This means that the following does not work:
 *   dd if=/dbg/usbmon/0t bs=10
 * Also, we do not allow seeks and do not bother advancing the offset.
 */
static ssize_t mon_text_read(struct file *file, char __user *buf,
				size_t nbytes, loff_t *ppos)
{
	struct mon_reader_text *rp = file->private_data;
	struct mon_bus *mbus = rp->r.m_bus;
	DECLARE_WAITQUEUE(waita, current);
	struct mon_event_text *ep;
	int cnt, limit;
	char *pbuf;
	char udir, utype;
	int data_len, i;

	add_wait_queue(&rp->wait, &waita);
	set_current_state(TASK_INTERRUPTIBLE);
	while ((ep = mon_text_fetch(rp, mbus)) == NULL) {
		if (file->f_flags & O_NONBLOCK) {
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&rp->wait, &waita);
			return -EWOULDBLOCK;	/* Same as EAGAIN in Linux */
		}
		/*
		 * We do not count nwaiters, because ->release is supposed
		 * to be called when all openers are gone only.
		 */
		schedule();
		if (signal_pending(current)) {
			remove_wait_queue(&rp->wait, &waita);
			return -EINTR;
		}
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&rp->wait, &waita);

	mutex_lock(&rp->printf_lock);
	cnt = 0;
	pbuf = rp->printf_buf;
	limit = rp->printf_size;

	udir = usb_pipein(ep->pipe) ? 'i' : 'o';
	switch (usb_pipetype(ep->pipe)) {
	case PIPE_ISOCHRONOUS:	utype = 'Z'; break;
	case PIPE_INTERRUPT:	utype = 'I'; break;
	case PIPE_CONTROL:	utype = 'C'; break;
	default: /* PIPE_BULK */  utype = 'B';
	}
	cnt += snprintf(pbuf + cnt, limit - cnt,
	    "%lx %u %c %c%c:%03u:%02u",
	    ep->id, ep->tstamp, ep->type,
	    utype, udir, usb_pipedevice(ep->pipe), usb_pipeendpoint(ep->pipe));

	if (ep->setup_flag == 0) {   /* Setup packet is present and captured */
		cnt += snprintf(pbuf + cnt, limit - cnt,
		    " s %02x %02x %04x %04x %04x",
		    ep->setup[0],
		    ep->setup[1],
		    (ep->setup[3] << 8) | ep->setup[2],
		    (ep->setup[5] << 8) | ep->setup[4],
		    (ep->setup[7] << 8) | ep->setup[6]);
	} else if (ep->setup_flag != '-') { /* Unable to capture setup packet */
		cnt += snprintf(pbuf + cnt, limit - cnt,
		    " %c __ __ ____ ____ ____", ep->setup_flag);
	} else {                     /* No setup for this kind of URB */
		cnt += snprintf(pbuf + cnt, limit - cnt, " %d", ep->status);
	}
	cnt += snprintf(pbuf + cnt, limit - cnt, " %d", ep->length);

	if ((data_len = ep->length) > 0) {
		if (ep->data_flag == 0) {
			cnt += snprintf(pbuf + cnt, limit - cnt, " =");
			if (data_len >= DATA_MAX)
				data_len = DATA_MAX;
			for (i = 0; i < data_len; i++) {
				if (i % 4 == 0) {
					cnt += snprintf(pbuf + cnt, limit - cnt,
					    " ");
				}
				cnt += snprintf(pbuf + cnt, limit - cnt,
				    "%02x", ep->data[i]);
			}
			cnt += snprintf(pbuf + cnt, limit - cnt, "\n");
		} else {
			cnt += snprintf(pbuf + cnt, limit - cnt,
			    " %c\n", ep->data_flag);
		}
	} else {
		cnt += snprintf(pbuf + cnt, limit - cnt, "\n");
	}

	if (copy_to_user(buf, rp->printf_buf, cnt))
		cnt = -EFAULT;
	mutex_unlock(&rp->printf_lock);
	kmem_cache_free(rp->e_slab, ep);
	return cnt;
}

static int mon_text_release(struct inode *inode, struct file *file)
{
	struct mon_reader_text *rp = file->private_data;
	struct mon_bus *mbus;
	/* unsigned long flags; */
	struct list_head *p;
	struct mon_event_text *ep;

	mutex_lock(&mon_lock);
	mbus = inode->u.generic_ip;

	if (mbus->nreaders <= 0) {
		printk(KERN_ERR TAG ": consistency error on close\n");
		mutex_unlock(&mon_lock);
		return 0;
	}
	mon_reader_del(mbus, &rp->r);

	/*
	 * In theory, e_list is protected by mbus->lock. However,
	 * after mon_reader_del has finished, the following is the case:
	 *  - we are not on reader list anymore, so new events won't be added;
	 *  - whole mbus may be dropped if it was orphaned.
	 * So, we better not touch mbus.
	 */
	/* spin_lock_irqsave(&mbus->lock, flags); */
	while (!list_empty(&rp->e_list)) {
		p = rp->e_list.next;
		ep = list_entry(p, struct mon_event_text, e_link);
		list_del(p);
		--rp->nevents;
		kmem_cache_free(rp->e_slab, ep);
	}
	/* spin_unlock_irqrestore(&mbus->lock, flags); */

	kmem_cache_destroy(rp->e_slab);
	kfree(rp->printf_buf);
	kfree(rp);

	mutex_unlock(&mon_lock);
	return 0;
}

struct file_operations mon_fops_text = {
	.owner =	THIS_MODULE,
	.open =		mon_text_open,
	.llseek =	no_llseek,
	.read =		mon_text_read,
	/* .write =	mon_text_write, */
	/* .poll =		mon_text_poll, */
	/* .ioctl =	mon_text_ioctl, */
	.release =	mon_text_release,
};

/*
 * Slab interface: constructor.
 */
static void mon_text_ctor(void *mem, kmem_cache_t *slab, unsigned long sflags)
{
	/*
	 * Nothing to initialize. No, really!
	 * So, we fill it with garbage to emulate a reused object.
	 */
	memset(mem, 0xe5, sizeof(struct mon_event_text));
}

static void mon_text_dtor(void *mem, kmem_cache_t *slab, unsigned long sflags)
{
	;
}
