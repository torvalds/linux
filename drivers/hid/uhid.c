/*
 * User-space I/O driver support for HID subsystem
 * Copyright (c) 2012 David Herrmann
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/atomic.h>
#include <linux/compat.h>
#include <linux/cred.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/uhid.h>
#include <linux/wait.h>

#define UHID_NAME	"uhid"
#define UHID_BUFSIZE	32

struct uhid_device {
	struct mutex devlock;
	bool running;

	__u8 *rd_data;
	uint rd_size;

	struct hid_device *hid;
	struct uhid_event input_buf;

	wait_queue_head_t waitq;
	spinlock_t qlock;
	__u8 head;
	__u8 tail;
	struct uhid_event *outq[UHID_BUFSIZE];

	/* blocking GET_REPORT support; state changes protected by qlock */
	struct mutex report_lock;
	wait_queue_head_t report_wait;
	bool report_running;
	u32 report_id;
	u32 report_type;
	struct uhid_event report_buf;
	struct work_struct worker;
};

static struct miscdevice uhid_misc;

static void uhid_device_add_worker(struct work_struct *work)
{
	struct uhid_device *uhid = container_of(work, struct uhid_device, worker);
	int ret;

	ret = hid_add_device(uhid->hid);
	if (ret) {
		hid_err(uhid->hid, "Cannot register HID device: error %d\n", ret);

		hid_destroy_device(uhid->hid);
		uhid->hid = NULL;
		uhid->running = false;
	}
}

static void uhid_queue(struct uhid_device *uhid, struct uhid_event *ev)
{
	__u8 newhead;

	newhead = (uhid->head + 1) % UHID_BUFSIZE;

	if (newhead != uhid->tail) {
		uhid->outq[uhid->head] = ev;
		uhid->head = newhead;
		wake_up_interruptible(&uhid->waitq);
	} else {
		hid_warn(uhid->hid, "Output queue is full\n");
		kfree(ev);
	}
}

static int uhid_queue_event(struct uhid_device *uhid, __u32 event)
{
	unsigned long flags;
	struct uhid_event *ev;

	ev = kzalloc(sizeof(*ev), GFP_KERNEL);
	if (!ev)
		return -ENOMEM;

	ev->type = event;

	spin_lock_irqsave(&uhid->qlock, flags);
	uhid_queue(uhid, ev);
	spin_unlock_irqrestore(&uhid->qlock, flags);

	return 0;
}

static int uhid_hid_start(struct hid_device *hid)
{
	struct uhid_device *uhid = hid->driver_data;
	struct uhid_event *ev;
	unsigned long flags;

	ev = kzalloc(sizeof(*ev), GFP_KERNEL);
	if (!ev)
		return -ENOMEM;

	ev->type = UHID_START;

	if (hid->report_enum[HID_FEATURE_REPORT].numbered)
		ev->u.start.dev_flags |= UHID_DEV_NUMBERED_FEATURE_REPORTS;
	if (hid->report_enum[HID_OUTPUT_REPORT].numbered)
		ev->u.start.dev_flags |= UHID_DEV_NUMBERED_OUTPUT_REPORTS;
	if (hid->report_enum[HID_INPUT_REPORT].numbered)
		ev->u.start.dev_flags |= UHID_DEV_NUMBERED_INPUT_REPORTS;

	spin_lock_irqsave(&uhid->qlock, flags);
	uhid_queue(uhid, ev);
	spin_unlock_irqrestore(&uhid->qlock, flags);

	return 0;
}

static void uhid_hid_stop(struct hid_device *hid)
{
	struct uhid_device *uhid = hid->driver_data;

	hid->claimed = 0;
	uhid_queue_event(uhid, UHID_STOP);
}

static int uhid_hid_open(struct hid_device *hid)
{
	struct uhid_device *uhid = hid->driver_data;

	return uhid_queue_event(uhid, UHID_OPEN);
}

static void uhid_hid_close(struct hid_device *hid)
{
	struct uhid_device *uhid = hid->driver_data;

	uhid_queue_event(uhid, UHID_CLOSE);
}

static int uhid_hid_parse(struct hid_device *hid)
{
	struct uhid_device *uhid = hid->driver_data;

	return hid_parse_report(hid, uhid->rd_data, uhid->rd_size);
}

/* must be called with report_lock held */
static int __uhid_report_queue_and_wait(struct uhid_device *uhid,
					struct uhid_event *ev,
					__u32 *report_id)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&uhid->qlock, flags);
	*report_id = ++uhid->report_id;
	uhid->report_type = ev->type + 1;
	uhid->report_running = true;
	uhid_queue(uhid, ev);
	spin_unlock_irqrestore(&uhid->qlock, flags);

	ret = wait_event_interruptible_timeout(uhid->report_wait,
				!uhid->report_running || !uhid->running,
				5 * HZ);
	if (!ret || !uhid->running || uhid->report_running)
		ret = -EIO;
	else if (ret < 0)
		ret = -ERESTARTSYS;
	else
		ret = 0;

	uhid->report_running = false;

	return ret;
}

static void uhid_report_wake_up(struct uhid_device *uhid, u32 id,
				const struct uhid_event *ev)
{
	unsigned long flags;

	spin_lock_irqsave(&uhid->qlock, flags);

	/* id for old report; drop it silently */
	if (uhid->report_type != ev->type || uhid->report_id != id)
		goto unlock;
	if (!uhid->report_running)
		goto unlock;

	memcpy(&uhid->report_buf, ev, sizeof(*ev));
	uhid->report_running = false;
	wake_up_interruptible(&uhid->report_wait);

unlock:
	spin_unlock_irqrestore(&uhid->qlock, flags);
}

static int uhid_hid_get_report(struct hid_device *hid, unsigned char rnum,
			       u8 *buf, size_t count, u8 rtype)
{
	struct uhid_device *uhid = hid->driver_data;
	struct uhid_get_report_reply_req *req;
	struct uhid_event *ev;
	int ret;

	if (!uhid->running)
		return -EIO;

	ev = kzalloc(sizeof(*ev), GFP_KERNEL);
	if (!ev)
		return -ENOMEM;

	ev->type = UHID_GET_REPORT;
	ev->u.get_report.rnum = rnum;
	ev->u.get_report.rtype = rtype;

	ret = mutex_lock_interruptible(&uhid->report_lock);
	if (ret) {
		kfree(ev);
		return ret;
	}

	/* this _always_ takes ownership of @ev */
	ret = __uhid_report_queue_and_wait(uhid, ev, &ev->u.get_report.id);
	if (ret)
		goto unlock;

	req = &uhid->report_buf.u.get_report_reply;
	if (req->err) {
		ret = -EIO;
	} else {
		ret = min3(count, (size_t)req->size, (size_t)UHID_DATA_MAX);
		memcpy(buf, req->data, ret);
	}

unlock:
	mutex_unlock(&uhid->report_lock);
	return ret;
}

static int uhid_hid_set_report(struct hid_device *hid, unsigned char rnum,
			       const u8 *buf, size_t count, u8 rtype)
{
	struct uhid_device *uhid = hid->driver_data;
	struct uhid_event *ev;
	int ret;

	if (!uhid->running || count > UHID_DATA_MAX)
		return -EIO;

	ev = kzalloc(sizeof(*ev), GFP_KERNEL);
	if (!ev)
		return -ENOMEM;

	ev->type = UHID_SET_REPORT;
	ev->u.set_report.rnum = rnum;
	ev->u.set_report.rtype = rtype;
	ev->u.set_report.size = count;
	memcpy(ev->u.set_report.data, buf, count);

	ret = mutex_lock_interruptible(&uhid->report_lock);
	if (ret) {
		kfree(ev);
		return ret;
	}

	/* this _always_ takes ownership of @ev */
	ret = __uhid_report_queue_and_wait(uhid, ev, &ev->u.set_report.id);
	if (ret)
		goto unlock;

	if (uhid->report_buf.u.set_report_reply.err)
		ret = -EIO;
	else
		ret = count;

unlock:
	mutex_unlock(&uhid->report_lock);
	return ret;
}

static int uhid_hid_raw_request(struct hid_device *hid, unsigned char reportnum,
				__u8 *buf, size_t len, unsigned char rtype,
				int reqtype)
{
	u8 u_rtype;

	switch (rtype) {
	case HID_FEATURE_REPORT:
		u_rtype = UHID_FEATURE_REPORT;
		break;
	case HID_OUTPUT_REPORT:
		u_rtype = UHID_OUTPUT_REPORT;
		break;
	case HID_INPUT_REPORT:
		u_rtype = UHID_INPUT_REPORT;
		break;
	default:
		return -EINVAL;
	}

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		return uhid_hid_get_report(hid, reportnum, buf, len, u_rtype);
	case HID_REQ_SET_REPORT:
		return uhid_hid_set_report(hid, reportnum, buf, len, u_rtype);
	default:
		return -EIO;
	}
}

static int uhid_hid_output_raw(struct hid_device *hid, __u8 *buf, size_t count,
			       unsigned char report_type)
{
	struct uhid_device *uhid = hid->driver_data;
	__u8 rtype;
	unsigned long flags;
	struct uhid_event *ev;

	switch (report_type) {
	case HID_FEATURE_REPORT:
		rtype = UHID_FEATURE_REPORT;
		break;
	case HID_OUTPUT_REPORT:
		rtype = UHID_OUTPUT_REPORT;
		break;
	default:
		return -EINVAL;
	}

	if (count < 1 || count > UHID_DATA_MAX)
		return -EINVAL;

	ev = kzalloc(sizeof(*ev), GFP_KERNEL);
	if (!ev)
		return -ENOMEM;

	ev->type = UHID_OUTPUT;
	ev->u.output.size = count;
	ev->u.output.rtype = rtype;
	memcpy(ev->u.output.data, buf, count);

	spin_lock_irqsave(&uhid->qlock, flags);
	uhid_queue(uhid, ev);
	spin_unlock_irqrestore(&uhid->qlock, flags);

	return count;
}

static int uhid_hid_output_report(struct hid_device *hid, __u8 *buf,
				  size_t count)
{
	return uhid_hid_output_raw(hid, buf, count, HID_OUTPUT_REPORT);
}

struct hid_ll_driver uhid_hid_driver = {
	.start = uhid_hid_start,
	.stop = uhid_hid_stop,
	.open = uhid_hid_open,
	.close = uhid_hid_close,
	.parse = uhid_hid_parse,
	.raw_request = uhid_hid_raw_request,
	.output_report = uhid_hid_output_report,
};
EXPORT_SYMBOL_GPL(uhid_hid_driver);

#ifdef CONFIG_COMPAT

/* Apparently we haven't stepped on these rakes enough times yet. */
struct uhid_create_req_compat {
	__u8 name[128];
	__u8 phys[64];
	__u8 uniq[64];

	compat_uptr_t rd_data;
	__u16 rd_size;

	__u16 bus;
	__u32 vendor;
	__u32 product;
	__u32 version;
	__u32 country;
} __attribute__((__packed__));

static int uhid_event_from_user(const char __user *buffer, size_t len,
				struct uhid_event *event)
{
	if (in_compat_syscall()) {
		u32 type;

		if (get_user(type, buffer))
			return -EFAULT;

		if (type == UHID_CREATE) {
			/*
			 * This is our messed up request with compat pointer.
			 * It is largish (more than 256 bytes) so we better
			 * allocate it from the heap.
			 */
			struct uhid_create_req_compat *compat;

			compat = kzalloc(sizeof(*compat), GFP_KERNEL);
			if (!compat)
				return -ENOMEM;

			buffer += sizeof(type);
			len -= sizeof(type);
			if (copy_from_user(compat, buffer,
					   min(len, sizeof(*compat)))) {
				kfree(compat);
				return -EFAULT;
			}

			/* Shuffle the data over to proper structure */
			event->type = type;

			memcpy(event->u.create.name, compat->name,
				sizeof(compat->name));
			memcpy(event->u.create.phys, compat->phys,
				sizeof(compat->phys));
			memcpy(event->u.create.uniq, compat->uniq,
				sizeof(compat->uniq));

			event->u.create.rd_data = compat_ptr(compat->rd_data);
			event->u.create.rd_size = compat->rd_size;

			event->u.create.bus = compat->bus;
			event->u.create.vendor = compat->vendor;
			event->u.create.product = compat->product;
			event->u.create.version = compat->version;
			event->u.create.country = compat->country;

			kfree(compat);
			return 0;
		}
		/* All others can be copied directly */
	}

	if (copy_from_user(event, buffer, min(len, sizeof(*event))))
		return -EFAULT;

	return 0;
}
#else
static int uhid_event_from_user(const char __user *buffer, size_t len,
				struct uhid_event *event)
{
	if (copy_from_user(event, buffer, min(len, sizeof(*event))))
		return -EFAULT;

	return 0;
}
#endif

static int uhid_dev_create2(struct uhid_device *uhid,
			    const struct uhid_event *ev)
{
	struct hid_device *hid;
	size_t rd_size, len;
	void *rd_data;
	int ret;

	if (uhid->running)
		return -EALREADY;

	rd_size = ev->u.create2.rd_size;
	if (rd_size <= 0 || rd_size > HID_MAX_DESCRIPTOR_SIZE)
		return -EINVAL;

	rd_data = kmemdup(ev->u.create2.rd_data, rd_size, GFP_KERNEL);
	if (!rd_data)
		return -ENOMEM;

	uhid->rd_size = rd_size;
	uhid->rd_data = rd_data;

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		ret = PTR_ERR(hid);
		goto err_free;
	}

	/* @hid is zero-initialized, strncpy() is correct, strlcpy() not */
	len = min(sizeof(hid->name), sizeof(ev->u.create2.name)) - 1;
	strncpy(hid->name, ev->u.create2.name, len);
	len = min(sizeof(hid->phys), sizeof(ev->u.create2.phys)) - 1;
	strncpy(hid->phys, ev->u.create2.phys, len);
	len = min(sizeof(hid->uniq), sizeof(ev->u.create2.uniq)) - 1;
	strncpy(hid->uniq, ev->u.create2.uniq, len);

	hid->ll_driver = &uhid_hid_driver;
	hid->bus = ev->u.create2.bus;
	hid->vendor = ev->u.create2.vendor;
	hid->product = ev->u.create2.product;
	hid->version = ev->u.create2.version;
	hid->country = ev->u.create2.country;
	hid->driver_data = uhid;
	hid->dev.parent = uhid_misc.this_device;

	uhid->hid = hid;
	uhid->running = true;

	/* Adding of a HID device is done through a worker, to allow HID drivers
	 * which use feature requests during .probe to work, without they would
	 * be blocked on devlock, which is held by uhid_char_write.
	 */
	schedule_work(&uhid->worker);

	return 0;

err_free:
	kfree(uhid->rd_data);
	uhid->rd_data = NULL;
	uhid->rd_size = 0;
	return ret;
}

static int uhid_dev_create(struct uhid_device *uhid,
			   struct uhid_event *ev)
{
	struct uhid_create_req orig;

	orig = ev->u.create;

	if (orig.rd_size <= 0 || orig.rd_size > HID_MAX_DESCRIPTOR_SIZE)
		return -EINVAL;
	if (copy_from_user(&ev->u.create2.rd_data, orig.rd_data, orig.rd_size))
		return -EFAULT;

	memcpy(ev->u.create2.name, orig.name, sizeof(orig.name));
	memcpy(ev->u.create2.phys, orig.phys, sizeof(orig.phys));
	memcpy(ev->u.create2.uniq, orig.uniq, sizeof(orig.uniq));
	ev->u.create2.rd_size = orig.rd_size;
	ev->u.create2.bus = orig.bus;
	ev->u.create2.vendor = orig.vendor;
	ev->u.create2.product = orig.product;
	ev->u.create2.version = orig.version;
	ev->u.create2.country = orig.country;

	return uhid_dev_create2(uhid, ev);
}

static int uhid_dev_destroy(struct uhid_device *uhid)
{
	if (!uhid->running)
		return -EINVAL;

	uhid->running = false;
	wake_up_interruptible(&uhid->report_wait);

	cancel_work_sync(&uhid->worker);

	hid_destroy_device(uhid->hid);
	kfree(uhid->rd_data);

	return 0;
}

static int uhid_dev_input(struct uhid_device *uhid, struct uhid_event *ev)
{
	if (!uhid->running)
		return -EINVAL;

	hid_input_report(uhid->hid, HID_INPUT_REPORT, ev->u.input.data,
			 min_t(size_t, ev->u.input.size, UHID_DATA_MAX), 0);

	return 0;
}

static int uhid_dev_input2(struct uhid_device *uhid, struct uhid_event *ev)
{
	if (!uhid->running)
		return -EINVAL;

	hid_input_report(uhid->hid, HID_INPUT_REPORT, ev->u.input2.data,
			 min_t(size_t, ev->u.input2.size, UHID_DATA_MAX), 0);

	return 0;
}

static int uhid_dev_get_report_reply(struct uhid_device *uhid,
				     struct uhid_event *ev)
{
	if (!uhid->running)
		return -EINVAL;

	uhid_report_wake_up(uhid, ev->u.get_report_reply.id, ev);
	return 0;
}

static int uhid_dev_set_report_reply(struct uhid_device *uhid,
				     struct uhid_event *ev)
{
	if (!uhid->running)
		return -EINVAL;

	uhid_report_wake_up(uhid, ev->u.set_report_reply.id, ev);
	return 0;
}

static int uhid_char_open(struct inode *inode, struct file *file)
{
	struct uhid_device *uhid;

	uhid = kzalloc(sizeof(*uhid), GFP_KERNEL);
	if (!uhid)
		return -ENOMEM;

	mutex_init(&uhid->devlock);
	mutex_init(&uhid->report_lock);
	spin_lock_init(&uhid->qlock);
	init_waitqueue_head(&uhid->waitq);
	init_waitqueue_head(&uhid->report_wait);
	uhid->running = false;
	INIT_WORK(&uhid->worker, uhid_device_add_worker);

	file->private_data = uhid;
	nonseekable_open(inode, file);

	return 0;
}

static int uhid_char_release(struct inode *inode, struct file *file)
{
	struct uhid_device *uhid = file->private_data;
	unsigned int i;

	uhid_dev_destroy(uhid);

	for (i = 0; i < UHID_BUFSIZE; ++i)
		kfree(uhid->outq[i]);

	kfree(uhid);

	return 0;
}

static ssize_t uhid_char_read(struct file *file, char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct uhid_device *uhid = file->private_data;
	int ret;
	unsigned long flags;
	size_t len;

	/* they need at least the "type" member of uhid_event */
	if (count < sizeof(__u32))
		return -EINVAL;

try_again:
	if (file->f_flags & O_NONBLOCK) {
		if (uhid->head == uhid->tail)
			return -EAGAIN;
	} else {
		ret = wait_event_interruptible(uhid->waitq,
						uhid->head != uhid->tail);
		if (ret)
			return ret;
	}

	ret = mutex_lock_interruptible(&uhid->devlock);
	if (ret)
		return ret;

	if (uhid->head == uhid->tail) {
		mutex_unlock(&uhid->devlock);
		goto try_again;
	} else {
		len = min(count, sizeof(**uhid->outq));
		if (copy_to_user(buffer, uhid->outq[uhid->tail], len)) {
			ret = -EFAULT;
		} else {
			kfree(uhid->outq[uhid->tail]);
			uhid->outq[uhid->tail] = NULL;

			spin_lock_irqsave(&uhid->qlock, flags);
			uhid->tail = (uhid->tail + 1) % UHID_BUFSIZE;
			spin_unlock_irqrestore(&uhid->qlock, flags);
		}
	}

	mutex_unlock(&uhid->devlock);
	return ret ? ret : len;
}

static ssize_t uhid_char_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct uhid_device *uhid = file->private_data;
	int ret;
	size_t len;

	/* we need at least the "type" member of uhid_event */
	if (count < sizeof(__u32))
		return -EINVAL;

	ret = mutex_lock_interruptible(&uhid->devlock);
	if (ret)
		return ret;

	memset(&uhid->input_buf, 0, sizeof(uhid->input_buf));
	len = min(count, sizeof(uhid->input_buf));

	ret = uhid_event_from_user(buffer, len, &uhid->input_buf);
	if (ret)
		goto unlock;

	switch (uhid->input_buf.type) {
	case UHID_CREATE:
		/*
		 * 'struct uhid_create_req' contains a __user pointer which is
		 * copied from, so it's unsafe to allow this with elevated
		 * privileges (e.g. from a setuid binary) or via kernel_write().
		 */
		if (file->f_cred != current_cred() || uaccess_kernel()) {
			pr_err_once("UHID_CREATE from different security context by process %d (%s), this is not allowed.\n",
				    task_tgid_vnr(current), current->comm);
			ret = -EACCES;
			goto unlock;
		}
		ret = uhid_dev_create(uhid, &uhid->input_buf);
		break;
	case UHID_CREATE2:
		ret = uhid_dev_create2(uhid, &uhid->input_buf);
		break;
	case UHID_DESTROY:
		ret = uhid_dev_destroy(uhid);
		break;
	case UHID_INPUT:
		ret = uhid_dev_input(uhid, &uhid->input_buf);
		break;
	case UHID_INPUT2:
		ret = uhid_dev_input2(uhid, &uhid->input_buf);
		break;
	case UHID_GET_REPORT_REPLY:
		ret = uhid_dev_get_report_reply(uhid, &uhid->input_buf);
		break;
	case UHID_SET_REPORT_REPLY:
		ret = uhid_dev_set_report_reply(uhid, &uhid->input_buf);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

unlock:
	mutex_unlock(&uhid->devlock);

	/* return "count" not "len" to not confuse the caller */
	return ret ? ret : count;
}

static __poll_t uhid_char_poll(struct file *file, poll_table *wait)
{
	struct uhid_device *uhid = file->private_data;

	poll_wait(file, &uhid->waitq, wait);

	if (uhid->head != uhid->tail)
		return EPOLLIN | EPOLLRDNORM;

	return EPOLLOUT | EPOLLWRNORM;
}

static const struct file_operations uhid_fops = {
	.owner		= THIS_MODULE,
	.open		= uhid_char_open,
	.release	= uhid_char_release,
	.read		= uhid_char_read,
	.write		= uhid_char_write,
	.poll		= uhid_char_poll,
	.llseek		= no_llseek,
};

static struct miscdevice uhid_misc = {
	.fops		= &uhid_fops,
	.minor		= UHID_MINOR,
	.name		= UHID_NAME,
};
module_misc_device(uhid_misc);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Herrmann <dh.herrmann@gmail.com>");
MODULE_DESCRIPTION("User-space I/O driver support for HID subsystem");
MODULE_ALIAS_MISCDEV(UHID_MINOR);
MODULE_ALIAS("devname:" UHID_NAME);
