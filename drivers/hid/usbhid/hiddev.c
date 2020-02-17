// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2001 Paul Stewart
 *  Copyright (c) 2001 Vojtech Pavlik
 *
 *  HID char devices, giving access to raw HID device events.
 */

/*
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to Paul Stewart <stewart@wetlogic.net>
 */

#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/hiddev.h>
#include <linux/compat.h>
#include <linux/vmalloc.h>
#include <linux/nospec.h>
#include "usbhid.h"

#ifdef CONFIG_USB_DYNAMIC_MINORS
#define HIDDEV_MINOR_BASE	0
#define HIDDEV_MINORS		256
#else
#define HIDDEV_MINOR_BASE	96
#define HIDDEV_MINORS		16
#endif
#define HIDDEV_BUFFER_SIZE	2048

struct hiddev_list {
	struct hiddev_usage_ref buffer[HIDDEV_BUFFER_SIZE];
	int head;
	int tail;
	unsigned flags;
	struct fasync_struct *fasync;
	struct hiddev *hiddev;
	struct list_head node;
	struct mutex thread_lock;
};

/*
 * Find a report, given the report's type and ID.  The ID can be specified
 * indirectly by REPORT_ID_FIRST (which returns the first report of the given
 * type) or by (REPORT_ID_NEXT | old_id), which returns the next report of the
 * given type which follows old_id.
 */
static struct hid_report *
hiddev_lookup_report(struct hid_device *hid, struct hiddev_report_info *rinfo)
{
	unsigned int flags = rinfo->report_id & ~HID_REPORT_ID_MASK;
	unsigned int rid = rinfo->report_id & HID_REPORT_ID_MASK;
	struct hid_report_enum *report_enum;
	struct hid_report *report;
	struct list_head *list;

	if (rinfo->report_type < HID_REPORT_TYPE_MIN ||
	    rinfo->report_type > HID_REPORT_TYPE_MAX)
		return NULL;

	report_enum = hid->report_enum +
		(rinfo->report_type - HID_REPORT_TYPE_MIN);

	switch (flags) {
	case 0: /* Nothing to do -- report_id is already set correctly */
		break;

	case HID_REPORT_ID_FIRST:
		if (list_empty(&report_enum->report_list))
			return NULL;

		list = report_enum->report_list.next;
		report = list_entry(list, struct hid_report, list);
		rinfo->report_id = report->id;
		break;

	case HID_REPORT_ID_NEXT:
		report = report_enum->report_id_hash[rid];
		if (!report)
			return NULL;

		list = report->list.next;
		if (list == &report_enum->report_list)
			return NULL;

		report = list_entry(list, struct hid_report, list);
		rinfo->report_id = report->id;
		break;

	default:
		return NULL;
	}

	return report_enum->report_id_hash[rinfo->report_id];
}

/*
 * Perform an exhaustive search of the report table for a usage, given its
 * type and usage id.
 */
static struct hid_field *
hiddev_lookup_usage(struct hid_device *hid, struct hiddev_usage_ref *uref)
{
	int i, j;
	struct hid_report *report;
	struct hid_report_enum *report_enum;
	struct hid_field *field;

	if (uref->report_type < HID_REPORT_TYPE_MIN ||
	    uref->report_type > HID_REPORT_TYPE_MAX)
		return NULL;

	report_enum = hid->report_enum +
		(uref->report_type - HID_REPORT_TYPE_MIN);

	list_for_each_entry(report, &report_enum->report_list, list) {
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
			for (j = 0; j < field->maxusage; j++) {
				if (field->usage[j].hid == uref->usage_code) {
					uref->report_id = report->id;
					uref->field_index = i;
					uref->usage_index = j;
					return field;
				}
			}
		}
	}

	return NULL;
}

static void hiddev_send_event(struct hid_device *hid,
			      struct hiddev_usage_ref *uref)
{
	struct hiddev *hiddev = hid->hiddev;
	struct hiddev_list *list;
	unsigned long flags;

	spin_lock_irqsave(&hiddev->list_lock, flags);
	list_for_each_entry(list, &hiddev->list, node) {
		if (uref->field_index != HID_FIELD_INDEX_NONE ||
		    (list->flags & HIDDEV_FLAG_REPORT) != 0) {
			list->buffer[list->head] = *uref;
			list->head = (list->head + 1) &
				(HIDDEV_BUFFER_SIZE - 1);
			kill_fasync(&list->fasync, SIGIO, POLL_IN);
		}
	}
	spin_unlock_irqrestore(&hiddev->list_lock, flags);

	wake_up_interruptible(&hiddev->wait);
}

/*
 * This is where hid.c calls into hiddev to pass an event that occurred over
 * the interrupt pipe
 */
void hiddev_hid_event(struct hid_device *hid, struct hid_field *field,
		      struct hid_usage *usage, __s32 value)
{
	unsigned type = field->report_type;
	struct hiddev_usage_ref uref;

	uref.report_type =
	  (type == HID_INPUT_REPORT) ? HID_REPORT_TYPE_INPUT :
	  ((type == HID_OUTPUT_REPORT) ? HID_REPORT_TYPE_OUTPUT :
	   ((type == HID_FEATURE_REPORT) ? HID_REPORT_TYPE_FEATURE : 0));
	uref.report_id = field->report->id;
	uref.field_index = field->index;
	uref.usage_index = (usage - field->usage);
	uref.usage_code = usage->hid;
	uref.value = value;

	hiddev_send_event(hid, &uref);
}
EXPORT_SYMBOL_GPL(hiddev_hid_event);

void hiddev_report_event(struct hid_device *hid, struct hid_report *report)
{
	unsigned type = report->type;
	struct hiddev_usage_ref uref;

	memset(&uref, 0, sizeof(uref));
	uref.report_type =
	  (type == HID_INPUT_REPORT) ? HID_REPORT_TYPE_INPUT :
	  ((type == HID_OUTPUT_REPORT) ? HID_REPORT_TYPE_OUTPUT :
	   ((type == HID_FEATURE_REPORT) ? HID_REPORT_TYPE_FEATURE : 0));
	uref.report_id = report->id;
	uref.field_index = HID_FIELD_INDEX_NONE;

	hiddev_send_event(hid, &uref);
}

/*
 * fasync file op
 */
static int hiddev_fasync(int fd, struct file *file, int on)
{
	struct hiddev_list *list = file->private_data;

	return fasync_helper(fd, file, on, &list->fasync);
}


/*
 * release file op
 */
static int hiddev_release(struct inode * inode, struct file * file)
{
	struct hiddev_list *list = file->private_data;
	unsigned long flags;

	spin_lock_irqsave(&list->hiddev->list_lock, flags);
	list_del(&list->node);
	spin_unlock_irqrestore(&list->hiddev->list_lock, flags);

	mutex_lock(&list->hiddev->existancelock);
	if (!--list->hiddev->open) {
		if (list->hiddev->exist) {
			hid_hw_close(list->hiddev->hid);
			hid_hw_power(list->hiddev->hid, PM_HINT_NORMAL);
		} else {
			mutex_unlock(&list->hiddev->existancelock);
			kfree(list->hiddev);
			vfree(list);
			return 0;
		}
	}

	mutex_unlock(&list->hiddev->existancelock);
	vfree(list);

	return 0;
}

static int __hiddev_open(struct hiddev *hiddev, struct file *file)
{
	struct hiddev_list *list;
	int error;

	lockdep_assert_held(&hiddev->existancelock);

	list = vzalloc(sizeof(*list));
	if (!list)
		return -ENOMEM;

	mutex_init(&list->thread_lock);
	list->hiddev = hiddev;

	if (!hiddev->open++) {
		error = hid_hw_power(hiddev->hid, PM_HINT_FULLON);
		if (error < 0)
			goto err_drop_count;

		error = hid_hw_open(hiddev->hid);
		if (error < 0)
			goto err_normal_power;
	}

	spin_lock_irq(&hiddev->list_lock);
	list_add_tail(&list->node, &hiddev->list);
	spin_unlock_irq(&hiddev->list_lock);

	file->private_data = list;

	return 0;

err_normal_power:
	hid_hw_power(hiddev->hid, PM_HINT_NORMAL);
err_drop_count:
	hiddev->open--;
	vfree(list);
	return error;
}

/*
 * open file op
 */
static int hiddev_open(struct inode *inode, struct file *file)
{
	struct usb_interface *intf;
	struct hid_device *hid;
	struct hiddev *hiddev;
	int res;

	intf = usbhid_find_interface(iminor(inode));
	if (!intf)
		return -ENODEV;

	hid = usb_get_intfdata(intf);
	hiddev = hid->hiddev;

	mutex_lock(&hiddev->existancelock);
	res = hiddev->exist ? __hiddev_open(hiddev, file) : -ENODEV;
	mutex_unlock(&hiddev->existancelock);

	return res;
}

/*
 * "write" file op
 */
static ssize_t hiddev_write(struct file * file, const char __user * buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

/*
 * "read" file op
 */
static ssize_t hiddev_read(struct file * file, char __user * buffer, size_t count, loff_t *ppos)
{
	DEFINE_WAIT(wait);
	struct hiddev_list *list = file->private_data;
	int event_size;
	int retval;

	event_size = ((list->flags & HIDDEV_FLAG_UREF) != 0) ?
		sizeof(struct hiddev_usage_ref) : sizeof(struct hiddev_event);

	if (count < event_size)
		return 0;

	/* lock against other threads */
	retval = mutex_lock_interruptible(&list->thread_lock);
	if (retval)
		return -ERESTARTSYS;

	while (retval == 0) {
		if (list->head == list->tail) {
			prepare_to_wait(&list->hiddev->wait, &wait, TASK_INTERRUPTIBLE);

			while (list->head == list->tail) {
				if (signal_pending(current)) {
					retval = -ERESTARTSYS;
					break;
				}
				if (!list->hiddev->exist) {
					retval = -EIO;
					break;
				}
				if (file->f_flags & O_NONBLOCK) {
					retval = -EAGAIN;
					break;
				}

				/* let O_NONBLOCK tasks run */
				mutex_unlock(&list->thread_lock);
				schedule();
				if (mutex_lock_interruptible(&list->thread_lock)) {
					finish_wait(&list->hiddev->wait, &wait);
					return -EINTR;
				}
				set_current_state(TASK_INTERRUPTIBLE);
			}
			finish_wait(&list->hiddev->wait, &wait);

		}

		if (retval) {
			mutex_unlock(&list->thread_lock);
			return retval;
		}


		while (list->head != list->tail &&
		       retval + event_size <= count) {
			if ((list->flags & HIDDEV_FLAG_UREF) == 0) {
				if (list->buffer[list->tail].field_index != HID_FIELD_INDEX_NONE) {
					struct hiddev_event event;

					event.hid = list->buffer[list->tail].usage_code;
					event.value = list->buffer[list->tail].value;
					if (copy_to_user(buffer + retval, &event, sizeof(struct hiddev_event))) {
						mutex_unlock(&list->thread_lock);
						return -EFAULT;
					}
					retval += sizeof(struct hiddev_event);
				}
			} else {
				if (list->buffer[list->tail].field_index != HID_FIELD_INDEX_NONE ||
				    (list->flags & HIDDEV_FLAG_REPORT) != 0) {

					if (copy_to_user(buffer + retval, list->buffer + list->tail, sizeof(struct hiddev_usage_ref))) {
						mutex_unlock(&list->thread_lock);
						return -EFAULT;
					}
					retval += sizeof(struct hiddev_usage_ref);
				}
			}
			list->tail = (list->tail + 1) & (HIDDEV_BUFFER_SIZE - 1);
		}

	}
	mutex_unlock(&list->thread_lock);

	return retval;
}

/*
 * "poll" file op
 * No kernel lock - fine
 */
static __poll_t hiddev_poll(struct file *file, poll_table *wait)
{
	struct hiddev_list *list = file->private_data;

	poll_wait(file, &list->hiddev->wait, wait);
	if (list->head != list->tail)
		return EPOLLIN | EPOLLRDNORM | EPOLLOUT;
	if (!list->hiddev->exist)
		return EPOLLERR | EPOLLHUP;
	return 0;
}

/*
 * "ioctl" file op
 */
static noinline int hiddev_ioctl_usage(struct hiddev *hiddev, unsigned int cmd, void __user *user_arg)
{
	struct hid_device *hid = hiddev->hid;
	struct hiddev_report_info rinfo;
	struct hiddev_usage_ref_multi *uref_multi = NULL;
	struct hiddev_usage_ref *uref;
	struct hid_report *report;
	struct hid_field *field;
	int i;

	uref_multi = kmalloc(sizeof(struct hiddev_usage_ref_multi), GFP_KERNEL);
	if (!uref_multi)
		return -ENOMEM;
	uref = &uref_multi->uref;
	if (cmd == HIDIOCGUSAGES || cmd == HIDIOCSUSAGES) {
		if (copy_from_user(uref_multi, user_arg,
				   sizeof(*uref_multi)))
			goto fault;
	} else {
		if (copy_from_user(uref, user_arg, sizeof(*uref)))
			goto fault;
	}

	switch (cmd) {
	case HIDIOCGUCODE:
		rinfo.report_type = uref->report_type;
		rinfo.report_id = uref->report_id;
		if ((report = hiddev_lookup_report(hid, &rinfo)) == NULL)
			goto inval;

		if (uref->field_index >= report->maxfield)
			goto inval;
		uref->field_index = array_index_nospec(uref->field_index,
						       report->maxfield);

		field = report->field[uref->field_index];
		if (uref->usage_index >= field->maxusage)
			goto inval;
		uref->usage_index = array_index_nospec(uref->usage_index,
						       field->maxusage);

		uref->usage_code = field->usage[uref->usage_index].hid;

		if (copy_to_user(user_arg, uref, sizeof(*uref)))
			goto fault;

		goto goodreturn;

	default:
		if (cmd != HIDIOCGUSAGE &&
		    cmd != HIDIOCGUSAGES &&
		    uref->report_type == HID_REPORT_TYPE_INPUT)
			goto inval;

		if (uref->report_id == HID_REPORT_ID_UNKNOWN) {
			field = hiddev_lookup_usage(hid, uref);
			if (field == NULL)
				goto inval;
		} else {
			rinfo.report_type = uref->report_type;
			rinfo.report_id = uref->report_id;
			if ((report = hiddev_lookup_report(hid, &rinfo)) == NULL)
				goto inval;

			if (uref->field_index >= report->maxfield)
				goto inval;
			uref->field_index = array_index_nospec(uref->field_index,
							       report->maxfield);

			field = report->field[uref->field_index];

			if (cmd == HIDIOCGCOLLECTIONINDEX) {
				if (uref->usage_index >= field->maxusage)
					goto inval;
				uref->usage_index =
					array_index_nospec(uref->usage_index,
							   field->maxusage);
			} else if (uref->usage_index >= field->report_count)
				goto inval;
		}

		if (cmd == HIDIOCGUSAGES || cmd == HIDIOCSUSAGES) {
			if (uref_multi->num_values > HID_MAX_MULTI_USAGES ||
			    uref->usage_index + uref_multi->num_values >
			    field->report_count)
				goto inval;

			uref->usage_index =
				array_index_nospec(uref->usage_index,
						   field->report_count -
						   uref_multi->num_values);
		}

		switch (cmd) {
		case HIDIOCGUSAGE:
			uref->value = field->value[uref->usage_index];
			if (copy_to_user(user_arg, uref, sizeof(*uref)))
				goto fault;
			goto goodreturn;

		case HIDIOCSUSAGE:
			field->value[uref->usage_index] = uref->value;
			goto goodreturn;

		case HIDIOCGCOLLECTIONINDEX:
			i = field->usage[uref->usage_index].collection_index;
			kfree(uref_multi);
			return i;
		case HIDIOCGUSAGES:
			for (i = 0; i < uref_multi->num_values; i++)
				uref_multi->values[i] =
				    field->value[uref->usage_index + i];
			if (copy_to_user(user_arg, uref_multi,
					 sizeof(*uref_multi)))
				goto fault;
			goto goodreturn;
		case HIDIOCSUSAGES:
			for (i = 0; i < uref_multi->num_values; i++)
				field->value[uref->usage_index + i] =
				    uref_multi->values[i];
			goto goodreturn;
		}

goodreturn:
		kfree(uref_multi);
		return 0;
fault:
		kfree(uref_multi);
		return -EFAULT;
inval:
		kfree(uref_multi);
		return -EINVAL;
	}
}

static noinline int hiddev_ioctl_string(struct hiddev *hiddev, unsigned int cmd, void __user *user_arg)
{
	struct hid_device *hid = hiddev->hid;
	struct usb_device *dev = hid_to_usb_dev(hid);
	int idx, len;
	char *buf;

	if (get_user(idx, (int __user *)user_arg))
		return -EFAULT;

	if ((buf = kmalloc(HID_STRING_SIZE, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	if ((len = usb_string(dev, idx, buf, HID_STRING_SIZE-1)) < 0) {
		kfree(buf);
		return -EINVAL;
	}

	if (copy_to_user(user_arg+sizeof(int), buf, len+1)) {
		kfree(buf);
		return -EFAULT;
	}

	kfree(buf);

	return len;
}

static long hiddev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct hiddev_list *list = file->private_data;
	struct hiddev *hiddev = list->hiddev;
	struct hid_device *hid;
	struct hiddev_collection_info cinfo;
	struct hiddev_report_info rinfo;
	struct hiddev_field_info finfo;
	struct hiddev_devinfo dinfo;
	struct hid_report *report;
	struct hid_field *field;
	void __user *user_arg = (void __user *)arg;
	int i, r = -EINVAL;

	/* Called without BKL by compat methods so no BKL taken */

	mutex_lock(&hiddev->existancelock);
	if (!hiddev->exist) {
		r = -ENODEV;
		goto ret_unlock;
	}

	hid = hiddev->hid;

	switch (cmd) {

	case HIDIOCGVERSION:
		r = put_user(HID_VERSION, (int __user *)arg) ?
			-EFAULT : 0;
		break;

	case HIDIOCAPPLICATION:
		if (arg >= hid->maxapplication)
			break;

		for (i = 0; i < hid->maxcollection; i++)
			if (hid->collection[i].type ==
			    HID_COLLECTION_APPLICATION && arg-- == 0)
				break;

		if (i < hid->maxcollection)
			r = hid->collection[i].usage;
		break;

	case HIDIOCGDEVINFO:
		{
			struct usb_device *dev = hid_to_usb_dev(hid);
			struct usbhid_device *usbhid = hid->driver_data;

			memset(&dinfo, 0, sizeof(dinfo));

			dinfo.bustype = BUS_USB;
			dinfo.busnum = dev->bus->busnum;
			dinfo.devnum = dev->devnum;
			dinfo.ifnum = usbhid->ifnum;
			dinfo.vendor = le16_to_cpu(dev->descriptor.idVendor);
			dinfo.product = le16_to_cpu(dev->descriptor.idProduct);
			dinfo.version = le16_to_cpu(dev->descriptor.bcdDevice);
			dinfo.num_applications = hid->maxapplication;

			r = copy_to_user(user_arg, &dinfo, sizeof(dinfo)) ?
				-EFAULT : 0;
			break;
		}

	case HIDIOCGFLAG:
		r = put_user(list->flags, (int __user *)arg) ?
			-EFAULT : 0;
		break;

	case HIDIOCSFLAG:
		{
			int newflags;

			if (get_user(newflags, (int __user *)arg)) {
				r = -EFAULT;
				break;
			}

			if ((newflags & ~HIDDEV_FLAGS) != 0 ||
			    ((newflags & HIDDEV_FLAG_REPORT) != 0 &&
			     (newflags & HIDDEV_FLAG_UREF) == 0))
				break;

			list->flags = newflags;

			r = 0;
			break;
		}

	case HIDIOCGSTRING:
		r = hiddev_ioctl_string(hiddev, cmd, user_arg);
		break;

	case HIDIOCINITREPORT:
		usbhid_init_reports(hid);
		hiddev->initialized = true;
		r = 0;
		break;

	case HIDIOCGREPORT:
		if (copy_from_user(&rinfo, user_arg, sizeof(rinfo))) {
			r = -EFAULT;
			break;
		}

		if (rinfo.report_type == HID_REPORT_TYPE_OUTPUT)
			break;

		report = hiddev_lookup_report(hid, &rinfo);
		if (report == NULL)
			break;

		hid_hw_request(hid, report, HID_REQ_GET_REPORT);
		hid_hw_wait(hid);

		r = 0;
		break;

	case HIDIOCSREPORT:
		if (copy_from_user(&rinfo, user_arg, sizeof(rinfo))) {
			r = -EFAULT;
			break;
		}

		if (rinfo.report_type == HID_REPORT_TYPE_INPUT)
			break;

		report = hiddev_lookup_report(hid, &rinfo);
		if (report == NULL)
			break;

		hid_hw_request(hid, report, HID_REQ_SET_REPORT);
		hid_hw_wait(hid);

		r = 0;
		break;

	case HIDIOCGREPORTINFO:
		if (copy_from_user(&rinfo, user_arg, sizeof(rinfo))) {
			r = -EFAULT;
			break;
		}

		report = hiddev_lookup_report(hid, &rinfo);
		if (report == NULL)
			break;

		rinfo.num_fields = report->maxfield;

		r = copy_to_user(user_arg, &rinfo, sizeof(rinfo)) ?
			-EFAULT : 0;
		break;

	case HIDIOCGFIELDINFO:
		if (copy_from_user(&finfo, user_arg, sizeof(finfo))) {
			r = -EFAULT;
			break;
		}

		rinfo.report_type = finfo.report_type;
		rinfo.report_id = finfo.report_id;

		report = hiddev_lookup_report(hid, &rinfo);
		if (report == NULL)
			break;

		if (finfo.field_index >= report->maxfield)
			break;
		finfo.field_index = array_index_nospec(finfo.field_index,
						       report->maxfield);

		field = report->field[finfo.field_index];
		memset(&finfo, 0, sizeof(finfo));
		finfo.report_type = rinfo.report_type;
		finfo.report_id = rinfo.report_id;
		finfo.field_index = field->report_count - 1;
		finfo.maxusage = field->maxusage;
		finfo.flags = field->flags;
		finfo.physical = field->physical;
		finfo.logical = field->logical;
		finfo.application = field->application;
		finfo.logical_minimum = field->logical_minimum;
		finfo.logical_maximum = field->logical_maximum;
		finfo.physical_minimum = field->physical_minimum;
		finfo.physical_maximum = field->physical_maximum;
		finfo.unit_exponent = field->unit_exponent;
		finfo.unit = field->unit;

		r = copy_to_user(user_arg, &finfo, sizeof(finfo)) ?
			-EFAULT : 0;
		break;

	case HIDIOCGUCODE:
		/* fall through */
	case HIDIOCGUSAGE:
	case HIDIOCSUSAGE:
	case HIDIOCGUSAGES:
	case HIDIOCSUSAGES:
	case HIDIOCGCOLLECTIONINDEX:
		if (!hiddev->initialized) {
			usbhid_init_reports(hid);
			hiddev->initialized = true;
		}
		r = hiddev_ioctl_usage(hiddev, cmd, user_arg);
		break;

	case HIDIOCGCOLLECTIONINFO:
		if (copy_from_user(&cinfo, user_arg, sizeof(cinfo))) {
			r = -EFAULT;
			break;
		}

		if (cinfo.index >= hid->maxcollection)
			break;
		cinfo.index = array_index_nospec(cinfo.index,
						 hid->maxcollection);

		cinfo.type = hid->collection[cinfo.index].type;
		cinfo.usage = hid->collection[cinfo.index].usage;
		cinfo.level = hid->collection[cinfo.index].level;

		r = copy_to_user(user_arg, &cinfo, sizeof(cinfo)) ?
			-EFAULT : 0;
		break;

	default:
		if (_IOC_TYPE(cmd) != 'H' || _IOC_DIR(cmd) != _IOC_READ)
			break;

		if (_IOC_NR(cmd) == _IOC_NR(HIDIOCGNAME(0))) {
			int len = strlen(hid->name) + 1;
			if (len > _IOC_SIZE(cmd))
				 len = _IOC_SIZE(cmd);
			r = copy_to_user(user_arg, hid->name, len) ?
				-EFAULT : len;
			break;
		}

		if (_IOC_NR(cmd) == _IOC_NR(HIDIOCGPHYS(0))) {
			int len = strlen(hid->phys) + 1;
			if (len > _IOC_SIZE(cmd))
				len = _IOC_SIZE(cmd);
			r = copy_to_user(user_arg, hid->phys, len) ?
				-EFAULT : len;
			break;
		}
	}

ret_unlock:
	mutex_unlock(&hiddev->existancelock);
	return r;
}

static const struct file_operations hiddev_fops = {
	.owner =	THIS_MODULE,
	.read =		hiddev_read,
	.write =	hiddev_write,
	.poll =		hiddev_poll,
	.open =		hiddev_open,
	.release =	hiddev_release,
	.unlocked_ioctl =	hiddev_ioctl,
	.fasync =	hiddev_fasync,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= noop_llseek,
};

static char *hiddev_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "usb/%s", dev_name(dev));
}

static struct usb_class_driver hiddev_class = {
	.name =		"hiddev%d",
	.devnode =	hiddev_devnode,
	.fops =		&hiddev_fops,
	.minor_base =	HIDDEV_MINOR_BASE,
};

/*
 * This is where hid.c calls us to connect a hid device to the hiddev driver
 */
int hiddev_connect(struct hid_device *hid, unsigned int force)
{
	struct hiddev *hiddev;
	struct usbhid_device *usbhid = hid->driver_data;
	int retval;

	if (!force) {
		unsigned int i;
		for (i = 0; i < hid->maxcollection; i++)
			if (hid->collection[i].type ==
			    HID_COLLECTION_APPLICATION &&
			    !IS_INPUT_APPLICATION(hid->collection[i].usage))
				break;

		if (i == hid->maxcollection)
			return -1;
	}

	if (!(hiddev = kzalloc(sizeof(struct hiddev), GFP_KERNEL)))
		return -1;

	init_waitqueue_head(&hiddev->wait);
	INIT_LIST_HEAD(&hiddev->list);
	spin_lock_init(&hiddev->list_lock);
	mutex_init(&hiddev->existancelock);
	hid->hiddev = hiddev;
	hiddev->hid = hid;
	hiddev->exist = 1;
	retval = usb_register_dev(usbhid->intf, &hiddev_class);
	if (retval) {
		hid_err(hid, "Not able to get a minor for this device\n");
		hid->hiddev = NULL;
		kfree(hiddev);
		return -1;
	}

	/*
	 * If HID_QUIRK_NO_INIT_REPORTS is set, make sure we don't initialize
	 * the reports.
	 */
	hiddev->initialized = hid->quirks & HID_QUIRK_NO_INIT_REPORTS;

	hiddev->minor = usbhid->intf->minor;

	return 0;
}

/*
 * This is where hid.c calls us to disconnect a hiddev device from the
 * corresponding hid device (usually because the usb device has disconnected)
 */
static struct usb_class_driver hiddev_class;
void hiddev_disconnect(struct hid_device *hid)
{
	struct hiddev *hiddev = hid->hiddev;
	struct usbhid_device *usbhid = hid->driver_data;

	usb_deregister_dev(usbhid->intf, &hiddev_class);

	mutex_lock(&hiddev->existancelock);
	hiddev->exist = 0;

	if (hiddev->open) {
		mutex_unlock(&hiddev->existancelock);
		hid_hw_close(hiddev->hid);
		wake_up_interruptible(&hiddev->wait);
	} else {
		mutex_unlock(&hiddev->existancelock);
		kfree(hiddev);
	}
}
