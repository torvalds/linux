/*
 * The USB Monitor, inspired by Dave Harding's USBMon.
 *
 * This is a text format reader.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <linux/scatterlist.h>
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
 * If usbmon were available to unprivileged processes, it might be open
 * to a local DoS. But we have to keep to root in order to prevent
 * password sniffing from HID devices.
 */
#define EVENT_MAX  (4*PAGE_SIZE / sizeof(struct mon_event_text))

/*
 * Potentially unlimited number; we limit it for similar allocations.
 * The usbfs limits this to 128, but we're not quite as generous.
 */
#define ISODESC_MAX   5

#define PRINTF_DFL  250   /* with 5 ISOs segs */

struct mon_iso_desc {
	int status;
	unsigned int offset;
	unsigned int length;	/* Unsigned here, signed in URB. Historic. */
};

struct mon_event_text {
	struct list_head e_link;
	int type;		/* submit, complete, etc. */
	unsigned long id;	/* From pointer, most of the time */
	unsigned int tstamp;
	int busnum;
	char devnum;
	char epnum;
	char is_in;
	char xfertype;
	int length;		/* Depends on type: xfer length or act length */
	int status;
	int interval;
	int start_frame;
	int error_count;
	char setup_flag;
	char data_flag;
	int numdesc;		/* Full number */
	struct mon_iso_desc isodesc[ISODESC_MAX];
	unsigned char setup[SETUP_MAX];
	unsigned char data[DATA_MAX];
};

#define SLAB_NAME_SZ  30
struct mon_reader_text {
	struct kmem_cache *e_slab;
	int nevents;
	struct list_head e_list;
	struct mon_reader r;	/* In C, parent class can be placed anywhere */

	wait_queue_head_t wait;
	int printf_size;
	char *printf_buf;
	struct mutex printf_lock;

	char slab_name[SLAB_NAME_SZ];
};

static struct dentry *mon_dir;		/* Usually /sys/kernel/debug/usbmon */

static void mon_text_ctor(void *);

struct mon_text_ptr {
	int cnt, limit;
	char *pbuf;
};

static struct mon_event_text *
    mon_text_read_wait(struct mon_reader_text *rp, struct file *file);
static void mon_text_read_head_t(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep);
static void mon_text_read_head_u(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep);
static void mon_text_read_statset(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep);
static void mon_text_read_intstat(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep);
static void mon_text_read_isostat(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep);
static void mon_text_read_isodesc(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep);
static void mon_text_read_data(struct mon_reader_text *rp,
    struct mon_text_ptr *p, const struct mon_event_text *ep);

/*
 * mon_text_submit
 * mon_text_complete
 *
 * May be called from an interrupt.
 *
 * This is called with the whole mon_bus locked, so no additional lock.
 */

static inline char mon_text_get_setup(struct mon_event_text *ep,
    struct urb *urb, char ev_type, struct mon_bus *mbus)
{

	if (ep->xfertype != USB_ENDPOINT_XFER_CONTROL || ev_type != 'S')
		return '-';

	if (urb->setup_packet == NULL)
		return 'Z';	/* '0' would be not as pretty. */

	memcpy(ep->setup, urb->setup_packet, SETUP_MAX);
	return 0;
}

static inline char mon_text_get_data(struct mon_event_text *ep, struct urb *urb,
    int len, char ev_type, struct mon_bus *mbus)
{
	void *src;

	if (len <= 0)
		return 'L';
	if (len >= DATA_MAX)
		len = DATA_MAX;

	if (ep->is_in) {
		if (ev_type != 'C')
			return '<';
	} else {
		if (ev_type != 'S')
			return '>';
	}

	if (urb->num_sgs == 0) {
		src = urb->transfer_buffer;
		if (src == NULL)
			return 'Z';	/* '0' would be not as pretty. */
	} else {
		struct scatterlist *sg = urb->sg;

		if (PageHighMem(sg_page(sg)))
			return 'D';

		/* For the text interface we copy only the first sg buffer */
		len = min_t(int, sg->length, len);
		src = sg_virt(sg);
	}

	memcpy(ep->data, src, len);
	return 0;
}

static inline unsigned int mon_get_timestamp(void)
{
	struct timeval tval;
	unsigned int stamp;

	do_gettimeofday(&tval);
	stamp = tval.tv_sec & 0xFFF;	/* 2^32 = 4294967296. Limit to 4096s. */
	stamp = stamp * 1000000 + tval.tv_usec;
	return stamp;
}

static void mon_text_event(struct mon_reader_text *rp, struct urb *urb,
    char ev_type, int status)
{
	struct mon_event_text *ep;
	unsigned int stamp;
	struct usb_iso_packet_descriptor *fp;
	struct mon_iso_desc *dp;
	int i, ndesc;

	stamp = mon_get_timestamp();

	if (rp->nevents >= EVENT_MAX ||
	    (ep = kmem_cache_alloc(rp->e_slab, GFP_ATOMIC)) == NULL) {
		rp->r.m_bus->cnt_text_lost++;
		return;
	}

	ep->type = ev_type;
	ep->id = (unsigned long) urb;
	ep->busnum = urb->dev->bus->busnum;
	ep->devnum = urb->dev->devnum;
	ep->epnum = usb_endpoint_num(&urb->ep->desc);
	ep->xfertype = usb_endpoint_type(&urb->ep->desc);
	ep->is_in = usb_urb_dir_in(urb);
	ep->tstamp = stamp;
	ep->length = (ev_type == 'S') ?
	    urb->transfer_buffer_length : urb->actual_length;
	/* Collecting status makes debugging sense for submits, too */
	ep->status = status;

	if (ep->xfertype == USB_ENDPOINT_XFER_INT) {
		ep->interval = urb->interval;
	} else if (ep->xfertype == USB_ENDPOINT_XFER_ISOC) {
		ep->interval = urb->interval;
		ep->start_frame = urb->start_frame;
		ep->error_count = urb->error_count;
	}
	ep->numdesc = urb->number_of_packets;
	if (ep->xfertype == USB_ENDPOINT_XFER_ISOC &&
			urb->number_of_packets > 0) {
		if ((ndesc = urb->number_of_packets) > ISODESC_MAX)
			ndesc = ISODESC_MAX;
		fp = urb->iso_frame_desc;
		dp = ep->isodesc;
		for (i = 0; i < ndesc; i++) {
			dp->status = fp->status;
			dp->offset = fp->offset;
			dp->length = (ev_type == 'S') ?
			    fp->length : fp->actual_length;
			fp++;
			dp++;
		}
		/* Wasteful, but simple to understand: ISO 'C' is sparse. */
		if (ev_type == 'C')
			ep->length = urb->transfer_buffer_length;
	}

	ep->setup_flag = mon_text_get_setup(ep, urb, ev_type, rp->r.m_bus);
	ep->data_flag = mon_text_get_data(ep, urb, ep->length, ev_type,
			rp->r.m_bus);

	rp->nevents++;
	list_add_tail(&ep->e_link, &rp->e_list);
	wake_up(&rp->wait);
}

static void mon_text_submit(void *data, struct urb *urb)
{
	struct mon_reader_text *rp = data;
	mon_text_event(rp, urb, 'S', -EINPROGRESS);
}

static void mon_text_complete(void *data, struct urb *urb, int status)
{
	struct mon_reader_text *rp = data;
	mon_text_event(rp, urb, 'C', status);
}

static void mon_text_error(void *data, struct urb *urb, int error)
{
	struct mon_reader_text *rp = data;
	struct mon_event_text *ep;

	if (rp->nevents >= EVENT_MAX ||
	    (ep = kmem_cache_alloc(rp->e_slab, GFP_ATOMIC)) == NULL) {
		rp->r.m_bus->cnt_text_lost++;
		return;
	}

	ep->type = 'E';
	ep->id = (unsigned long) urb;
	ep->busnum = urb->dev->bus->busnum;
	ep->devnum = urb->dev->devnum;
	ep->epnum = usb_endpoint_num(&urb->ep->desc);
	ep->xfertype = usb_endpoint_type(&urb->ep->desc);
	ep->is_in = usb_urb_dir_in(urb);
	ep->tstamp = mon_get_timestamp();
	ep->length = 0;
	ep->status = error;

	ep->setup_flag = '-';
	ep->data_flag = 'E';

	rp->nevents++;
	list_add_tail(&ep->e_link, &rp->e_list);
	wake_up(&rp->wait);
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
	struct mon_reader_text *rp;
	int rc;

	mutex_lock(&mon_lock);
	mbus = inode->i_private;

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
	rp->r.rnf_error = mon_text_error;
	rp->r.rnf_complete = mon_text_complete;

	snprintf(rp->slab_name, SLAB_NAME_SZ, "mon_text_%p", rp);
	rp->e_slab = kmem_cache_create(rp->slab_name,
	    sizeof(struct mon_event_text), sizeof(long), 0,
	    mon_text_ctor);
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
static ssize_t mon_text_read_t(struct file *file, char __user *buf,
				size_t nbytes, loff_t *ppos)
{
	struct mon_reader_text *rp = file->private_data;
	struct mon_event_text *ep;
	struct mon_text_ptr ptr;

	if (IS_ERR(ep = mon_text_read_wait(rp, file)))
		return PTR_ERR(ep);
	mutex_lock(&rp->printf_lock);
	ptr.cnt = 0;
	ptr.pbuf = rp->printf_buf;
	ptr.limit = rp->printf_size;

	mon_text_read_head_t(rp, &ptr, ep);
	mon_text_read_statset(rp, &ptr, ep);
	ptr.cnt += snprintf(ptr.pbuf + ptr.cnt, ptr.limit - ptr.cnt,
	    " %d", ep->length);
	mon_text_read_data(rp, &ptr, ep);

	if (copy_to_user(buf, rp->printf_buf, ptr.cnt))
		ptr.cnt = -EFAULT;
	mutex_unlock(&rp->printf_lock);
	kmem_cache_free(rp->e_slab, ep);
	return ptr.cnt;
}

static ssize_t mon_text_read_u(struct file *file, char __user *buf,
				size_t nbytes, loff_t *ppos)
{
	struct mon_reader_text *rp = file->private_data;
	struct mon_event_text *ep;
	struct mon_text_ptr ptr;

	if (IS_ERR(ep = mon_text_read_wait(rp, file)))
		return PTR_ERR(ep);
	mutex_lock(&rp->printf_lock);
	ptr.cnt = 0;
	ptr.pbuf = rp->printf_buf;
	ptr.limit = rp->printf_size;

	mon_text_read_head_u(rp, &ptr, ep);
	if (ep->type == 'E') {
		mon_text_read_statset(rp, &ptr, ep);
	} else if (ep->xfertype == USB_ENDPOINT_XFER_ISOC) {
		mon_text_read_isostat(rp, &ptr, ep);
		mon_text_read_isodesc(rp, &ptr, ep);
	} else if (ep->xfertype == USB_ENDPOINT_XFER_INT) {
		mon_text_read_intstat(rp, &ptr, ep);
	} else {
		mon_text_read_statset(rp, &ptr, ep);
	}
	ptr.cnt += snprintf(ptr.pbuf + ptr.cnt, ptr.limit - ptr.cnt,
	    " %d", ep->length);
	mon_text_read_data(rp, &ptr, ep);

	if (copy_to_user(buf, rp->printf_buf, ptr.cnt))
		ptr.cnt = -EFAULT;
	mutex_unlock(&rp->printf_lock);
	kmem_cache_free(rp->e_slab, ep);
	return ptr.cnt;
}

static struct mon_event_text *mon_text_read_wait(struct mon_reader_text *rp,
    struct file *file)
{
	struct mon_bus *mbus = rp->r.m_bus;
	DECLARE_WAITQUEUE(waita, current);
	struct mon_event_text *ep;

	add_wait_queue(&rp->wait, &waita);
	set_current_state(TASK_INTERRUPTIBLE);
	while ((ep = mon_text_fetch(rp, mbus)) == NULL) {
		if (file->f_flags & O_NONBLOCK) {
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&rp->wait, &waita);
			return ERR_PTR(-EWOULDBLOCK);
		}
		/*
		 * We do not count nwaiters, because ->release is supposed
		 * to be called when all openers are gone only.
		 */
		schedule();
		if (signal_pending(current)) {
			remove_wait_queue(&rp->wait, &waita);
			return ERR_PTR(-EINTR);
		}
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&rp->wait, &waita);
	return ep;
}

static void mon_text_read_head_t(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep)
{
	char udir, utype;

	udir = (ep->is_in ? 'i' : 'o');
	switch (ep->xfertype) {
	case USB_ENDPOINT_XFER_ISOC:	utype = 'Z'; break;
	case USB_ENDPOINT_XFER_INT:	utype = 'I'; break;
	case USB_ENDPOINT_XFER_CONTROL:	utype = 'C'; break;
	default: /* PIPE_BULK */  utype = 'B';
	}
	p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
	    "%lx %u %c %c%c:%03u:%02u",
	    ep->id, ep->tstamp, ep->type,
	    utype, udir, ep->devnum, ep->epnum);
}

static void mon_text_read_head_u(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep)
{
	char udir, utype;

	udir = (ep->is_in ? 'i' : 'o');
	switch (ep->xfertype) {
	case USB_ENDPOINT_XFER_ISOC:	utype = 'Z'; break;
	case USB_ENDPOINT_XFER_INT:	utype = 'I'; break;
	case USB_ENDPOINT_XFER_CONTROL:	utype = 'C'; break;
	default: /* PIPE_BULK */  utype = 'B';
	}
	p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
	    "%lx %u %c %c%c:%d:%03u:%u",
	    ep->id, ep->tstamp, ep->type,
	    utype, udir, ep->busnum, ep->devnum, ep->epnum);
}

static void mon_text_read_statset(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep)
{

	if (ep->setup_flag == 0) {   /* Setup packet is present and captured */
		p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
		    " s %02x %02x %04x %04x %04x",
		    ep->setup[0],
		    ep->setup[1],
		    (ep->setup[3] << 8) | ep->setup[2],
		    (ep->setup[5] << 8) | ep->setup[4],
		    (ep->setup[7] << 8) | ep->setup[6]);
	} else if (ep->setup_flag != '-') { /* Unable to capture setup packet */
		p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
		    " %c __ __ ____ ____ ____", ep->setup_flag);
	} else {                     /* No setup for this kind of URB */
		p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
		    " %d", ep->status);
	}
}

static void mon_text_read_intstat(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep)
{
	p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
	    " %d:%d", ep->status, ep->interval);
}

static void mon_text_read_isostat(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep)
{
	if (ep->type == 'S') {
		p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
		    " %d:%d:%d", ep->status, ep->interval, ep->start_frame);
	} else {
		p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
		    " %d:%d:%d:%d",
		    ep->status, ep->interval, ep->start_frame, ep->error_count);
	}
}

static void mon_text_read_isodesc(struct mon_reader_text *rp,
	struct mon_text_ptr *p, const struct mon_event_text *ep)
{
	int ndesc;	/* Display this many */
	int i;
	const struct mon_iso_desc *dp;

	p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
	    " %d", ep->numdesc);
	ndesc = ep->numdesc;
	if (ndesc > ISODESC_MAX)
		ndesc = ISODESC_MAX;
	if (ndesc < 0)
		ndesc = 0;
	dp = ep->isodesc;
	for (i = 0; i < ndesc; i++) {
		p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
		    " %d:%u:%u", dp->status, dp->offset, dp->length);
		dp++;
	}
}

static void mon_text_read_data(struct mon_reader_text *rp,
    struct mon_text_ptr *p, const struct mon_event_text *ep)
{
	int data_len, i;

	if ((data_len = ep->length) > 0) {
		if (ep->data_flag == 0) {
			p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
			    " =");
			if (data_len >= DATA_MAX)
				data_len = DATA_MAX;
			for (i = 0; i < data_len; i++) {
				if (i % 4 == 0) {
					p->cnt += snprintf(p->pbuf + p->cnt,
					    p->limit - p->cnt,
					    " ");
				}
				p->cnt += snprintf(p->pbuf + p->cnt,
				    p->limit - p->cnt,
				    "%02x", ep->data[i]);
			}
			p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
			    "\n");
		} else {
			p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt,
			    " %c\n", ep->data_flag);
		}
	} else {
		p->cnt += snprintf(p->pbuf + p->cnt, p->limit - p->cnt, "\n");
	}
}

static int mon_text_release(struct inode *inode, struct file *file)
{
	struct mon_reader_text *rp = file->private_data;
	struct mon_bus *mbus;
	/* unsigned long flags; */
	struct list_head *p;
	struct mon_event_text *ep;

	mutex_lock(&mon_lock);
	mbus = inode->i_private;

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

static const struct file_operations mon_fops_text_t = {
	.owner =	THIS_MODULE,
	.open =		mon_text_open,
	.llseek =	no_llseek,
	.read =		mon_text_read_t,
	.release =	mon_text_release,
};

static const struct file_operations mon_fops_text_u = {
	.owner =	THIS_MODULE,
	.open =		mon_text_open,
	.llseek =	no_llseek,
	.read =		mon_text_read_u,
	.release =	mon_text_release,
};

int mon_text_add(struct mon_bus *mbus, const struct usb_bus *ubus)
{
	struct dentry *d;
	enum { NAMESZ = 10 };
	char name[NAMESZ];
	int busnum = ubus? ubus->busnum: 0;
	int rc;

	if (mon_dir == NULL)
		return 0;

	if (ubus != NULL) {
		rc = snprintf(name, NAMESZ, "%dt", busnum);
		if (rc <= 0 || rc >= NAMESZ)
			goto err_print_t;
		d = debugfs_create_file(name, 0600, mon_dir, mbus,
							     &mon_fops_text_t);
		if (d == NULL)
			goto err_create_t;
		mbus->dent_t = d;
	}

	rc = snprintf(name, NAMESZ, "%du", busnum);
	if (rc <= 0 || rc >= NAMESZ)
		goto err_print_u;
	d = debugfs_create_file(name, 0600, mon_dir, mbus, &mon_fops_text_u);
	if (d == NULL)
		goto err_create_u;
	mbus->dent_u = d;

	rc = snprintf(name, NAMESZ, "%ds", busnum);
	if (rc <= 0 || rc >= NAMESZ)
		goto err_print_s;
	d = debugfs_create_file(name, 0600, mon_dir, mbus, &mon_fops_stat);
	if (d == NULL)
		goto err_create_s;
	mbus->dent_s = d;

	return 1;

err_create_s:
err_print_s:
	debugfs_remove(mbus->dent_u);
	mbus->dent_u = NULL;
err_create_u:
err_print_u:
	if (ubus != NULL) {
		debugfs_remove(mbus->dent_t);
		mbus->dent_t = NULL;
	}
err_create_t:
err_print_t:
	return 0;
}

void mon_text_del(struct mon_bus *mbus)
{
	debugfs_remove(mbus->dent_u);
	if (mbus->dent_t != NULL)
		debugfs_remove(mbus->dent_t);
	debugfs_remove(mbus->dent_s);
}

/*
 * Slab interface: constructor.
 */
static void mon_text_ctor(void *mem)
{
	/*
	 * Nothing to initialize. No, really!
	 * So, we fill it with garbage to emulate a reused object.
	 */
	memset(mem, 0xe5, sizeof(struct mon_event_text));
}

int __init mon_text_init(void)
{
	struct dentry *mondir;

	mondir = debugfs_create_dir("usbmon", usb_debug_root);
	if (IS_ERR(mondir)) {
		/* debugfs not available, but we can use usbmon without it */
		return 0;
	}
	if (mondir == NULL) {
		printk(KERN_NOTICE TAG ": unable to create usbmon directory\n");
		return -ENOMEM;
	}
	mon_dir = mondir;
	return 0;
}

void mon_text_exit(void)
{
	debugfs_remove(mon_dir);
}
