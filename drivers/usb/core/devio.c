/*****************************************************************************/

/*
 *      devio.c  --  User space communication with USB devices.
 *
 *      Copyright (C) 1999-2000  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  This file implements the usbfs/x/y files, where
 *  x is the bus number and y the device number.
 *
 *  It allows user space programs/"drivers" to communicate directly
 *  with USB devices without intervening kernel driver.
 *
 *  Revision history
 *    22.12.1999   0.1   Initial release (split from proc_usb.c)
 *    04.01.2000   0.2   Turned into its own filesystem
 *    30.09.2005   0.3   Fix user-triggerable oops in async URB delivery
 *    			 (CAN-2005-3055)
 */

/*****************************************************************************/

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/signal.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/usbdevice_fs.h>
#include <linux/usb/hcd.h>	/* for usbcore internals */
#include <linux/cdev.h>
#include <linux/notifier.h>
#include <linux/security.h>
#include <linux/user_namespace.h>
#include <linux/scatterlist.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <linux/moduleparam.h>

#include "usb.h"

#define USB_MAXBUS			64
#define USB_DEVICE_MAX			USB_MAXBUS * 128
#define USB_SG_SIZE			16384 /* split-size for large txs */

/* Mutual exclusion for removal, open, and release */
DEFINE_MUTEX(usbfs_mutex);

struct dev_state {
	struct list_head list;      /* state list */
	struct usb_device *dev;
	struct file *file;
	spinlock_t lock;            /* protects the async urb lists */
	struct list_head async_pending;
	struct list_head async_completed;
	wait_queue_head_t wait;     /* wake up if a request completed */
	unsigned int discsignr;
	struct pid *disc_pid;
	const struct cred *cred;
	void __user *disccontext;
	unsigned long ifclaimed;
	u32 secid;
	u32 disabled_bulk_eps;
};

struct async {
	struct list_head asynclist;
	struct dev_state *ps;
	struct pid *pid;
	const struct cred *cred;
	unsigned int signr;
	unsigned int ifnum;
	void __user *userbuffer;
	void __user *userurb;
	struct urb *urb;
	unsigned int mem_usage;
	int status;
	u32 secid;
	u8 bulk_addr;
	u8 bulk_status;
};

static bool usbfs_snoop;
module_param(usbfs_snoop, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(usbfs_snoop, "true to log all usbfs traffic");

#define snoop(dev, format, arg...)				\
	do {							\
		if (usbfs_snoop)				\
			dev_info(dev , format , ## arg);	\
	} while (0)

enum snoop_when {
	SUBMIT, COMPLETE
};

#define USB_DEVICE_DEV		MKDEV(USB_DEVICE_MAJOR, 0)

/* Limit on the total amount of memory we can allocate for transfers */
static unsigned usbfs_memory_mb = 16;
module_param(usbfs_memory_mb, uint, 0644);
MODULE_PARM_DESC(usbfs_memory_mb,
		"maximum MB allowed for usbfs buffers (0 = no limit)");

/* Hard limit, necessary to avoid aithmetic overflow */
#define USBFS_XFER_MAX		(UINT_MAX / 2 - 1000000)

static atomic_t usbfs_memory_usage;	/* Total memory currently allocated */

/* Check whether it's okay to allocate more memory for a transfer */
static int usbfs_increase_memory_usage(unsigned amount)
{
	unsigned lim;

	/*
	 * Convert usbfs_memory_mb to bytes, avoiding overflows.
	 * 0 means use the hard limit (effectively unlimited).
	 */
	lim = ACCESS_ONCE(usbfs_memory_mb);
	if (lim == 0 || lim > (USBFS_XFER_MAX >> 20))
		lim = USBFS_XFER_MAX;
	else
		lim <<= 20;

	atomic_add(amount, &usbfs_memory_usage);
	if (atomic_read(&usbfs_memory_usage) <= lim)
		return 0;
	atomic_sub(amount, &usbfs_memory_usage);
	return -ENOMEM;
}

/* Memory for a transfer is being deallocated */
static void usbfs_decrease_memory_usage(unsigned amount)
{
	atomic_sub(amount, &usbfs_memory_usage);
}

static int connected(struct dev_state *ps)
{
	return (!list_empty(&ps->list) &&
			ps->dev->state != USB_STATE_NOTATTACHED);
}

static loff_t usbdev_lseek(struct file *file, loff_t offset, int orig)
{
	loff_t ret;

	mutex_lock(&file_inode(file)->i_mutex);

	switch (orig) {
	case 0:
		file->f_pos = offset;
		ret = file->f_pos;
		break;
	case 1:
		file->f_pos += offset;
		ret = file->f_pos;
		break;
	case 2:
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&file_inode(file)->i_mutex);
	return ret;
}

static ssize_t usbdev_read(struct file *file, char __user *buf, size_t nbytes,
			   loff_t *ppos)
{
	struct dev_state *ps = file->private_data;
	struct usb_device *dev = ps->dev;
	ssize_t ret = 0;
	unsigned len;
	loff_t pos;
	int i;

	pos = *ppos;
	usb_lock_device(dev);
	if (!connected(ps)) {
		ret = -ENODEV;
		goto err;
	} else if (pos < 0) {
		ret = -EINVAL;
		goto err;
	}

	if (pos < sizeof(struct usb_device_descriptor)) {
		/* 18 bytes - fits on the stack */
		struct usb_device_descriptor temp_desc;

		memcpy(&temp_desc, &dev->descriptor, sizeof(dev->descriptor));
		le16_to_cpus(&temp_desc.bcdUSB);
		le16_to_cpus(&temp_desc.idVendor);
		le16_to_cpus(&temp_desc.idProduct);
		le16_to_cpus(&temp_desc.bcdDevice);

		len = sizeof(struct usb_device_descriptor) - pos;
		if (len > nbytes)
			len = nbytes;
		if (copy_to_user(buf, ((char *)&temp_desc) + pos, len)) {
			ret = -EFAULT;
			goto err;
		}

		*ppos += len;
		buf += len;
		nbytes -= len;
		ret += len;
	}

	pos = sizeof(struct usb_device_descriptor);
	for (i = 0; nbytes && i < dev->descriptor.bNumConfigurations; i++) {
		struct usb_config_descriptor *config =
			(struct usb_config_descriptor *)dev->rawdescriptors[i];
		unsigned int length = le16_to_cpu(config->wTotalLength);

		if (*ppos < pos + length) {

			/* The descriptor may claim to be longer than it
			 * really is.  Here is the actual allocated length. */
			unsigned alloclen =
				le16_to_cpu(dev->config[i].desc.wTotalLength);

			len = length - (*ppos - pos);
			if (len > nbytes)
				len = nbytes;

			/* Simply don't write (skip over) unallocated parts */
			if (alloclen > (*ppos - pos)) {
				alloclen -= (*ppos - pos);
				if (copy_to_user(buf,
				    dev->rawdescriptors[i] + (*ppos - pos),
				    min(len, alloclen))) {
					ret = -EFAULT;
					goto err;
				}
			}

			*ppos += len;
			buf += len;
			nbytes -= len;
			ret += len;
		}

		pos += length;
	}

err:
	usb_unlock_device(dev);
	return ret;
}

/*
 * async list handling
 */

static struct async *alloc_async(unsigned int numisoframes)
{
	struct async *as;

	as = kzalloc(sizeof(struct async), GFP_KERNEL);
	if (!as)
		return NULL;
	as->urb = usb_alloc_urb(numisoframes, GFP_KERNEL);
	if (!as->urb) {
		kfree(as);
		return NULL;
	}
	return as;
}

static void free_async(struct async *as)
{
	int i;

	put_pid(as->pid);
	if (as->cred)
		put_cred(as->cred);
	for (i = 0; i < as->urb->num_sgs; i++) {
		if (sg_page(&as->urb->sg[i]))
			kfree(sg_virt(&as->urb->sg[i]));
	}
	kfree(as->urb->sg);
	kfree(as->urb->transfer_buffer);
	kfree(as->urb->setup_packet);
	usb_free_urb(as->urb);
	usbfs_decrease_memory_usage(as->mem_usage);
	kfree(as);
}

static void async_newpending(struct async *as)
{
	struct dev_state *ps = as->ps;
	unsigned long flags;

	spin_lock_irqsave(&ps->lock, flags);
	list_add_tail(&as->asynclist, &ps->async_pending);
	spin_unlock_irqrestore(&ps->lock, flags);
}

static void async_removepending(struct async *as)
{
	struct dev_state *ps = as->ps;
	unsigned long flags;

	spin_lock_irqsave(&ps->lock, flags);
	list_del_init(&as->asynclist);
	spin_unlock_irqrestore(&ps->lock, flags);
}

static struct async *async_getcompleted(struct dev_state *ps)
{
	unsigned long flags;
	struct async *as = NULL;

	spin_lock_irqsave(&ps->lock, flags);
	if (!list_empty(&ps->async_completed)) {
		as = list_entry(ps->async_completed.next, struct async,
				asynclist);
		list_del_init(&as->asynclist);
	}
	spin_unlock_irqrestore(&ps->lock, flags);
	return as;
}

static struct async *async_getpending(struct dev_state *ps,
					     void __user *userurb)
{
	struct async *as;

	list_for_each_entry(as, &ps->async_pending, asynclist)
		if (as->userurb == userurb) {
			list_del_init(&as->asynclist);
			return as;
		}

	return NULL;
}

static void snoop_urb(struct usb_device *udev,
		void __user *userurb, int pipe, unsigned length,
		int timeout_or_status, enum snoop_when when,
		unsigned char *data, unsigned data_len)
{
	static const char *types[] = {"isoc", "int", "ctrl", "bulk"};
	static const char *dirs[] = {"out", "in"};
	int ep;
	const char *t, *d;

	if (!usbfs_snoop)
		return;

	ep = usb_pipeendpoint(pipe);
	t = types[usb_pipetype(pipe)];
	d = dirs[!!usb_pipein(pipe)];

	if (userurb) {		/* Async */
		if (when == SUBMIT)
			dev_info(&udev->dev, "userurb %p, ep%d %s-%s, "
					"length %u\n",
					userurb, ep, t, d, length);
		else
			dev_info(&udev->dev, "userurb %p, ep%d %s-%s, "
					"actual_length %u status %d\n",
					userurb, ep, t, d, length,
					timeout_or_status);
	} else {
		if (when == SUBMIT)
			dev_info(&udev->dev, "ep%d %s-%s, length %u, "
					"timeout %d\n",
					ep, t, d, length, timeout_or_status);
		else
			dev_info(&udev->dev, "ep%d %s-%s, actual_length %u, "
					"status %d\n",
					ep, t, d, length, timeout_or_status);
	}

	if (data && data_len > 0) {
		print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_NONE, 32, 1,
			data, data_len, 1);
	}
}

static void snoop_urb_data(struct urb *urb, unsigned len)
{
	int i, size;

	if (!usbfs_snoop)
		return;

	if (urb->num_sgs == 0) {
		print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_NONE, 32, 1,
			urb->transfer_buffer, len, 1);
		return;
	}

	for (i = 0; i < urb->num_sgs && len; i++) {
		size = (len > USB_SG_SIZE) ? USB_SG_SIZE : len;
		print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_NONE, 32, 1,
			sg_virt(&urb->sg[i]), size, 1);
		len -= size;
	}
}

static int copy_urb_data_to_user(u8 __user *userbuffer, struct urb *urb)
{
	unsigned i, len, size;

	if (urb->number_of_packets > 0)		/* Isochronous */
		len = urb->transfer_buffer_length;
	else					/* Non-Isoc */
		len = urb->actual_length;

	if (urb->num_sgs == 0) {
		if (copy_to_user(userbuffer, urb->transfer_buffer, len))
			return -EFAULT;
		return 0;
	}

	for (i = 0; i < urb->num_sgs && len; i++) {
		size = (len > USB_SG_SIZE) ? USB_SG_SIZE : len;
		if (copy_to_user(userbuffer, sg_virt(&urb->sg[i]), size))
			return -EFAULT;
		userbuffer += size;
		len -= size;
	}

	return 0;
}

#define AS_CONTINUATION	1
#define AS_UNLINK	2

static void cancel_bulk_urbs(struct dev_state *ps, unsigned bulk_addr)
__releases(ps->lock)
__acquires(ps->lock)
{
	struct urb *urb;
	struct async *as;

	/* Mark all the pending URBs that match bulk_addr, up to but not
	 * including the first one without AS_CONTINUATION.  If such an
	 * URB is encountered then a new transfer has already started so
	 * the endpoint doesn't need to be disabled; otherwise it does.
	 */
	list_for_each_entry(as, &ps->async_pending, asynclist) {
		if (as->bulk_addr == bulk_addr) {
			if (as->bulk_status != AS_CONTINUATION)
				goto rescan;
			as->bulk_status = AS_UNLINK;
			as->bulk_addr = 0;
		}
	}
	ps->disabled_bulk_eps |= (1 << bulk_addr);

	/* Now carefully unlink all the marked pending URBs */
 rescan:
	list_for_each_entry(as, &ps->async_pending, asynclist) {
		if (as->bulk_status == AS_UNLINK) {
			as->bulk_status = 0;		/* Only once */
			urb = as->urb;
			usb_get_urb(urb);
			spin_unlock(&ps->lock);		/* Allow completions */
			usb_unlink_urb(urb);
			usb_put_urb(urb);
			spin_lock(&ps->lock);
			goto rescan;
		}
	}
}

static void async_completed(struct urb *urb)
{
	struct async *as = urb->context;
	struct dev_state *ps = as->ps;
	struct siginfo sinfo;
	struct pid *pid = NULL;
	u32 secid = 0;
	const struct cred *cred = NULL;
	int signr;

	spin_lock(&ps->lock);
	list_move_tail(&as->asynclist, &ps->async_completed);
	as->status = urb->status;
	signr = as->signr;
	if (signr) {
		sinfo.si_signo = as->signr;
		sinfo.si_errno = as->status;
		sinfo.si_code = SI_ASYNCIO;
		sinfo.si_addr = as->userurb;
		pid = get_pid(as->pid);
		cred = get_cred(as->cred);
		secid = as->secid;
	}
	snoop(&urb->dev->dev, "urb complete\n");
	snoop_urb(urb->dev, as->userurb, urb->pipe, urb->actual_length,
			as->status, COMPLETE, NULL, 0);
	if ((urb->transfer_flags & URB_DIR_MASK) == USB_DIR_IN)
		snoop_urb_data(urb, urb->actual_length);

	if (as->status < 0 && as->bulk_addr && as->status != -ECONNRESET &&
			as->status != -ENOENT)
		cancel_bulk_urbs(ps, as->bulk_addr);
	spin_unlock(&ps->lock);

	if (signr) {
		kill_pid_info_as_cred(sinfo.si_signo, &sinfo, pid, cred, secid);
		put_pid(pid);
		put_cred(cred);
	}

	wake_up(&ps->wait);
}

static void destroy_async(struct dev_state *ps, struct list_head *list)
{
	struct urb *urb;
	struct async *as;
	unsigned long flags;

	spin_lock_irqsave(&ps->lock, flags);
	while (!list_empty(list)) {
		as = list_entry(list->next, struct async, asynclist);
		list_del_init(&as->asynclist);
		urb = as->urb;
		usb_get_urb(urb);

		/* drop the spinlock so the completion handler can run */
		spin_unlock_irqrestore(&ps->lock, flags);
		usb_kill_urb(urb);
		usb_put_urb(urb);
		spin_lock_irqsave(&ps->lock, flags);
	}
	spin_unlock_irqrestore(&ps->lock, flags);
}

static void destroy_async_on_interface(struct dev_state *ps,
				       unsigned int ifnum)
{
	struct list_head *p, *q, hitlist;
	unsigned long flags;

	INIT_LIST_HEAD(&hitlist);
	spin_lock_irqsave(&ps->lock, flags);
	list_for_each_safe(p, q, &ps->async_pending)
		if (ifnum == list_entry(p, struct async, asynclist)->ifnum)
			list_move_tail(p, &hitlist);
	spin_unlock_irqrestore(&ps->lock, flags);
	destroy_async(ps, &hitlist);
}

static void destroy_all_async(struct dev_state *ps)
{
	destroy_async(ps, &ps->async_pending);
}

/*
 * interface claims are made only at the request of user level code,
 * which can also release them (explicitly or by closing files).
 * they're also undone when devices disconnect.
 */

static int driver_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	return -ENODEV;
}

static void driver_disconnect(struct usb_interface *intf)
{
	struct dev_state *ps = usb_get_intfdata(intf);
	unsigned int ifnum = intf->altsetting->desc.bInterfaceNumber;

	if (!ps)
		return;

	/* NOTE:  this relies on usbcore having canceled and completed
	 * all pending I/O requests; 2.6 does that.
	 */

	if (likely(ifnum < 8*sizeof(ps->ifclaimed)))
		clear_bit(ifnum, &ps->ifclaimed);
	else
		dev_warn(&intf->dev, "interface number %u out of range\n",
			 ifnum);

	usb_set_intfdata(intf, NULL);

	/* force async requests to complete */
	destroy_async_on_interface(ps, ifnum);
}

/* The following routines are merely placeholders.  There is no way
 * to inform a user task about suspend or resumes.
 */
static int driver_suspend(struct usb_interface *intf, pm_message_t msg)
{
	return 0;
}

static int driver_resume(struct usb_interface *intf)
{
	return 0;
}

struct usb_driver usbfs_driver = {
	.name =		"usbfs",
	.probe =	driver_probe,
	.disconnect =	driver_disconnect,
	.suspend =	driver_suspend,
	.resume =	driver_resume,
};

static int claimintf(struct dev_state *ps, unsigned int ifnum)
{
	struct usb_device *dev = ps->dev;
	struct usb_interface *intf;
	int err;

	if (ifnum >= 8*sizeof(ps->ifclaimed))
		return -EINVAL;
	/* already claimed */
	if (test_bit(ifnum, &ps->ifclaimed))
		return 0;

	intf = usb_ifnum_to_if(dev, ifnum);
	if (!intf)
		err = -ENOENT;
	else
		err = usb_driver_claim_interface(&usbfs_driver, intf, ps);
	if (err == 0)
		set_bit(ifnum, &ps->ifclaimed);
	return err;
}

static int releaseintf(struct dev_state *ps, unsigned int ifnum)
{
	struct usb_device *dev;
	struct usb_interface *intf;
	int err;

	err = -EINVAL;
	if (ifnum >= 8*sizeof(ps->ifclaimed))
		return err;
	dev = ps->dev;
	intf = usb_ifnum_to_if(dev, ifnum);
	if (!intf)
		err = -ENOENT;
	else if (test_and_clear_bit(ifnum, &ps->ifclaimed)) {
		usb_driver_release_interface(&usbfs_driver, intf);
		err = 0;
	}
	return err;
}

static int checkintf(struct dev_state *ps, unsigned int ifnum)
{
	if (ps->dev->state != USB_STATE_CONFIGURED)
		return -EHOSTUNREACH;
	if (ifnum >= 8*sizeof(ps->ifclaimed))
		return -EINVAL;
	if (test_bit(ifnum, &ps->ifclaimed))
		return 0;
	/* if not yet claimed, claim it for the driver */
	dev_warn(&ps->dev->dev, "usbfs: process %d (%s) did not claim "
		 "interface %u before use\n", task_pid_nr(current),
		 current->comm, ifnum);
	return claimintf(ps, ifnum);
}

static int findintfep(struct usb_device *dev, unsigned int ep)
{
	unsigned int i, j, e;
	struct usb_interface *intf;
	struct usb_host_interface *alts;
	struct usb_endpoint_descriptor *endpt;

	if (ep & ~(USB_DIR_IN|0xf))
		return -EINVAL;
	if (!dev->actconfig)
		return -ESRCH;
	for (i = 0; i < dev->actconfig->desc.bNumInterfaces; i++) {
		intf = dev->actconfig->interface[i];
		for (j = 0; j < intf->num_altsetting; j++) {
			alts = &intf->altsetting[j];
			for (e = 0; e < alts->desc.bNumEndpoints; e++) {
				endpt = &alts->endpoint[e].desc;
				if (endpt->bEndpointAddress == ep)
					return alts->desc.bInterfaceNumber;
			}
		}
	}
	return -ENOENT;
}

static int check_ctrlrecip(struct dev_state *ps, unsigned int requesttype,
			   unsigned int request, unsigned int index)
{
	int ret = 0;
	struct usb_host_interface *alt_setting;

	if (ps->dev->state != USB_STATE_UNAUTHENTICATED
	 && ps->dev->state != USB_STATE_ADDRESS
	 && ps->dev->state != USB_STATE_CONFIGURED)
		return -EHOSTUNREACH;
	if (USB_TYPE_VENDOR == (USB_TYPE_MASK & requesttype))
		return 0;

	/*
	 * check for the special corner case 'get_device_id' in the printer
	 * class specification, where wIndex is (interface << 8 | altsetting)
	 * instead of just interface
	 */
	if (requesttype == 0xa1 && request == 0) {
		alt_setting = usb_find_alt_setting(ps->dev->actconfig,
						   index >> 8, index & 0xff);
		if (alt_setting
		 && alt_setting->desc.bInterfaceClass == USB_CLASS_PRINTER)
			index >>= 8;
	}

	index &= 0xff;
	switch (requesttype & USB_RECIP_MASK) {
	case USB_RECIP_ENDPOINT:
		ret = findintfep(ps->dev, index);
		if (ret >= 0)
			ret = checkintf(ps, ret);
		break;

	case USB_RECIP_INTERFACE:
		ret = checkintf(ps, index);
		break;
	}
	return ret;
}

static int match_devt(struct device *dev, void *data)
{
	return dev->devt == (dev_t) (unsigned long) data;
}

static struct usb_device *usbdev_lookup_by_devt(dev_t devt)
{
	struct device *dev;

	dev = bus_find_device(&usb_bus_type, NULL,
			      (void *) (unsigned long) devt, match_devt);
	if (!dev)
		return NULL;
	return container_of(dev, struct usb_device, dev);
}

/*
 * file operations
 */
static int usbdev_open(struct inode *inode, struct file *file)
{
	struct usb_device *dev = NULL;
	struct dev_state *ps;
	int ret;

	ret = -ENOMEM;
	ps = kmalloc(sizeof(struct dev_state), GFP_KERNEL);
	if (!ps)
		goto out_free_ps;

	ret = -ENODEV;

	/* Protect against simultaneous removal or release */
	mutex_lock(&usbfs_mutex);

	/* usbdev device-node */
	if (imajor(inode) == USB_DEVICE_MAJOR)
		dev = usbdev_lookup_by_devt(inode->i_rdev);

	mutex_unlock(&usbfs_mutex);

	if (!dev)
		goto out_free_ps;

	usb_lock_device(dev);
	if (dev->state == USB_STATE_NOTATTACHED)
		goto out_unlock_device;

	ret = usb_autoresume_device(dev);
	if (ret)
		goto out_unlock_device;

	ps->dev = dev;
	ps->file = file;
	spin_lock_init(&ps->lock);
	INIT_LIST_HEAD(&ps->list);
	INIT_LIST_HEAD(&ps->async_pending);
	INIT_LIST_HEAD(&ps->async_completed);
	init_waitqueue_head(&ps->wait);
	ps->discsignr = 0;
	ps->disc_pid = get_pid(task_pid(current));
	ps->cred = get_current_cred();
	ps->disccontext = NULL;
	ps->ifclaimed = 0;
	security_task_getsecid(current, &ps->secid);
	smp_wmb();
	list_add_tail(&ps->list, &dev->filelist);
	file->private_data = ps;
	usb_unlock_device(dev);
	snoop(&dev->dev, "opened by process %d: %s\n", task_pid_nr(current),
			current->comm);
	return ret;

 out_unlock_device:
	usb_unlock_device(dev);
	usb_put_dev(dev);
 out_free_ps:
	kfree(ps);
	return ret;
}

static int usbdev_release(struct inode *inode, struct file *file)
{
	struct dev_state *ps = file->private_data;
	struct usb_device *dev = ps->dev;
	unsigned int ifnum;
	struct async *as;

	usb_lock_device(dev);
	usb_hub_release_all_ports(dev, ps);

	list_del_init(&ps->list);

	for (ifnum = 0; ps->ifclaimed && ifnum < 8*sizeof(ps->ifclaimed);
			ifnum++) {
		if (test_bit(ifnum, &ps->ifclaimed))
			releaseintf(ps, ifnum);
	}
	destroy_all_async(ps);
	usb_autosuspend_device(dev);
	usb_unlock_device(dev);
	usb_put_dev(dev);
	put_pid(ps->disc_pid);
	put_cred(ps->cred);

	as = async_getcompleted(ps);
	while (as) {
		free_async(as);
		as = async_getcompleted(ps);
	}
	kfree(ps);
	return 0;
}

static int proc_control(struct dev_state *ps, void __user *arg)
{
	struct usb_device *dev = ps->dev;
	struct usbdevfs_ctrltransfer ctrl;
	unsigned int tmo;
	unsigned char *tbuf;
	unsigned wLength;
	int i, pipe, ret;

	if (copy_from_user(&ctrl, arg, sizeof(ctrl)))
		return -EFAULT;
	ret = check_ctrlrecip(ps, ctrl.bRequestType, ctrl.bRequest,
			      ctrl.wIndex);
	if (ret)
		return ret;
	wLength = ctrl.wLength;		/* To suppress 64k PAGE_SIZE warning */
	if (wLength > PAGE_SIZE)
		return -EINVAL;
	ret = usbfs_increase_memory_usage(PAGE_SIZE + sizeof(struct urb) +
			sizeof(struct usb_ctrlrequest));
	if (ret)
		return ret;
	tbuf = (unsigned char *)__get_free_page(GFP_KERNEL);
	if (!tbuf) {
		ret = -ENOMEM;
		goto done;
	}
	tmo = ctrl.timeout;
	snoop(&dev->dev, "control urb: bRequestType=%02x "
		"bRequest=%02x wValue=%04x "
		"wIndex=%04x wLength=%04x\n",
		ctrl.bRequestType, ctrl.bRequest,
		__le16_to_cpup(&ctrl.wValue),
		__le16_to_cpup(&ctrl.wIndex),
		__le16_to_cpup(&ctrl.wLength));
	if (ctrl.bRequestType & 0x80) {
		if (ctrl.wLength && !access_ok(VERIFY_WRITE, ctrl.data,
					       ctrl.wLength)) {
			ret = -EINVAL;
			goto done;
		}
		pipe = usb_rcvctrlpipe(dev, 0);
		snoop_urb(dev, NULL, pipe, ctrl.wLength, tmo, SUBMIT, NULL, 0);

		usb_unlock_device(dev);
		i = usb_control_msg(dev, pipe, ctrl.bRequest,
				    ctrl.bRequestType, ctrl.wValue, ctrl.wIndex,
				    tbuf, ctrl.wLength, tmo);
		usb_lock_device(dev);
		snoop_urb(dev, NULL, pipe, max(i, 0), min(i, 0), COMPLETE,
			  tbuf, max(i, 0));
		if ((i > 0) && ctrl.wLength) {
			if (copy_to_user(ctrl.data, tbuf, i)) {
				ret = -EFAULT;
				goto done;
			}
		}
	} else {
		if (ctrl.wLength) {
			if (copy_from_user(tbuf, ctrl.data, ctrl.wLength)) {
				ret = -EFAULT;
				goto done;
			}
		}
		pipe = usb_sndctrlpipe(dev, 0);
		snoop_urb(dev, NULL, pipe, ctrl.wLength, tmo, SUBMIT,
			tbuf, ctrl.wLength);

		usb_unlock_device(dev);
		i = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), ctrl.bRequest,
				    ctrl.bRequestType, ctrl.wValue, ctrl.wIndex,
				    tbuf, ctrl.wLength, tmo);
		usb_lock_device(dev);
		snoop_urb(dev, NULL, pipe, max(i, 0), min(i, 0), COMPLETE, NULL, 0);
	}
	if (i < 0 && i != -EPIPE) {
		dev_printk(KERN_DEBUG, &dev->dev, "usbfs: USBDEVFS_CONTROL "
			   "failed cmd %s rqt %u rq %u len %u ret %d\n",
			   current->comm, ctrl.bRequestType, ctrl.bRequest,
			   ctrl.wLength, i);
	}
	ret = i;
 done:
	free_page((unsigned long) tbuf);
	usbfs_decrease_memory_usage(PAGE_SIZE + sizeof(struct urb) +
			sizeof(struct usb_ctrlrequest));
	return ret;
}

static int proc_bulk(struct dev_state *ps, void __user *arg)
{
	struct usb_device *dev = ps->dev;
	struct usbdevfs_bulktransfer bulk;
	unsigned int tmo, len1, pipe;
	int len2;
	unsigned char *tbuf;
	int i, ret;

	if (copy_from_user(&bulk, arg, sizeof(bulk)))
		return -EFAULT;
	ret = findintfep(ps->dev, bulk.ep);
	if (ret < 0)
		return ret;
	ret = checkintf(ps, ret);
	if (ret)
		return ret;
	if (bulk.ep & USB_DIR_IN)
		pipe = usb_rcvbulkpipe(dev, bulk.ep & 0x7f);
	else
		pipe = usb_sndbulkpipe(dev, bulk.ep & 0x7f);
	if (!usb_maxpacket(dev, pipe, !(bulk.ep & USB_DIR_IN)))
		return -EINVAL;
	len1 = bulk.len;
	if (len1 >= USBFS_XFER_MAX)
		return -EINVAL;
	ret = usbfs_increase_memory_usage(len1 + sizeof(struct urb));
	if (ret)
		return ret;
	if (!(tbuf = kmalloc(len1, GFP_KERNEL))) {
		ret = -ENOMEM;
		goto done;
	}
	tmo = bulk.timeout;
	if (bulk.ep & 0x80) {
		if (len1 && !access_ok(VERIFY_WRITE, bulk.data, len1)) {
			ret = -EINVAL;
			goto done;
		}
		snoop_urb(dev, NULL, pipe, len1, tmo, SUBMIT, NULL, 0);

		usb_unlock_device(dev);
		i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, tmo);
		usb_lock_device(dev);
		snoop_urb(dev, NULL, pipe, len2, i, COMPLETE, tbuf, len2);

		if (!i && len2) {
			if (copy_to_user(bulk.data, tbuf, len2)) {
				ret = -EFAULT;
				goto done;
			}
		}
	} else {
		if (len1) {
			if (copy_from_user(tbuf, bulk.data, len1)) {
				ret = -EFAULT;
				goto done;
			}
		}
		snoop_urb(dev, NULL, pipe, len1, tmo, SUBMIT, tbuf, len1);

		usb_unlock_device(dev);
		i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, tmo);
		usb_lock_device(dev);
		snoop_urb(dev, NULL, pipe, len2, i, COMPLETE, NULL, 0);
	}
	ret = (i < 0 ? i : len2);
 done:
	kfree(tbuf);
	usbfs_decrease_memory_usage(len1 + sizeof(struct urb));
	return ret;
}

static int proc_resetep(struct dev_state *ps, void __user *arg)
{
	unsigned int ep;
	int ret;

	if (get_user(ep, (unsigned int __user *)arg))
		return -EFAULT;
	ret = findintfep(ps->dev, ep);
	if (ret < 0)
		return ret;
	ret = checkintf(ps, ret);
	if (ret)
		return ret;
	usb_reset_endpoint(ps->dev, ep);
	return 0;
}

static int proc_clearhalt(struct dev_state *ps, void __user *arg)
{
	unsigned int ep;
	int pipe;
	int ret;

	if (get_user(ep, (unsigned int __user *)arg))
		return -EFAULT;
	ret = findintfep(ps->dev, ep);
	if (ret < 0)
		return ret;
	ret = checkintf(ps, ret);
	if (ret)
		return ret;
	if (ep & USB_DIR_IN)
		pipe = usb_rcvbulkpipe(ps->dev, ep & 0x7f);
	else
		pipe = usb_sndbulkpipe(ps->dev, ep & 0x7f);

	return usb_clear_halt(ps->dev, pipe);
}

static int proc_getdriver(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_getdriver gd;
	struct usb_interface *intf;
	int ret;

	if (copy_from_user(&gd, arg, sizeof(gd)))
		return -EFAULT;
	intf = usb_ifnum_to_if(ps->dev, gd.interface);
	if (!intf || !intf->dev.driver)
		ret = -ENODATA;
	else {
		strlcpy(gd.driver, intf->dev.driver->name,
				sizeof(gd.driver));
		ret = (copy_to_user(arg, &gd, sizeof(gd)) ? -EFAULT : 0);
	}
	return ret;
}

static int proc_connectinfo(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_connectinfo ci = {
		.devnum = ps->dev->devnum,
		.slow = ps->dev->speed == USB_SPEED_LOW
	};

	if (copy_to_user(arg, &ci, sizeof(ci)))
		return -EFAULT;
	return 0;
}

static int proc_resetdevice(struct dev_state *ps)
{
	return usb_reset_device(ps->dev);
}

static int proc_setintf(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_setinterface setintf;
	int ret;

	if (copy_from_user(&setintf, arg, sizeof(setintf)))
		return -EFAULT;
	if ((ret = checkintf(ps, setintf.interface)))
		return ret;
	return usb_set_interface(ps->dev, setintf.interface,
			setintf.altsetting);
}

static int proc_setconfig(struct dev_state *ps, void __user *arg)
{
	int u;
	int status = 0;
	struct usb_host_config *actconfig;

	if (get_user(u, (int __user *)arg))
		return -EFAULT;

	actconfig = ps->dev->actconfig;

	/* Don't touch the device if any interfaces are claimed.
	 * It could interfere with other drivers' operations, and if
	 * an interface is claimed by usbfs it could easily deadlock.
	 */
	if (actconfig) {
		int i;

		for (i = 0; i < actconfig->desc.bNumInterfaces; ++i) {
			if (usb_interface_claimed(actconfig->interface[i])) {
				dev_warn(&ps->dev->dev,
					"usbfs: interface %d claimed by %s "
					"while '%s' sets config #%d\n",
					actconfig->interface[i]
						->cur_altsetting
						->desc.bInterfaceNumber,
					actconfig->interface[i]
						->dev.driver->name,
					current->comm, u);
				status = -EBUSY;
				break;
			}
		}
	}

	/* SET_CONFIGURATION is often abused as a "cheap" driver reset,
	 * so avoid usb_set_configuration()'s kick to sysfs
	 */
	if (status == 0) {
		if (actconfig && actconfig->desc.bConfigurationValue == u)
			status = usb_reset_configuration(ps->dev);
		else
			status = usb_set_configuration(ps->dev, u);
	}

	return status;
}

static int proc_do_submiturb(struct dev_state *ps, struct usbdevfs_urb *uurb,
			struct usbdevfs_iso_packet_desc __user *iso_frame_desc,
			void __user *arg)
{
	struct usbdevfs_iso_packet_desc *isopkt = NULL;
	struct usb_host_endpoint *ep;
	struct async *as = NULL;
	struct usb_ctrlrequest *dr = NULL;
	unsigned int u, totlen, isofrmlen;
	int i, ret, is_in, num_sgs = 0, ifnum = -1;
	void *buf;

	if (uurb->flags & ~(USBDEVFS_URB_ISO_ASAP |
				USBDEVFS_URB_SHORT_NOT_OK |
				USBDEVFS_URB_BULK_CONTINUATION |
				USBDEVFS_URB_NO_FSBR |
				USBDEVFS_URB_ZERO_PACKET |
				USBDEVFS_URB_NO_INTERRUPT))
		return -EINVAL;
	if (uurb->buffer_length > 0 && !uurb->buffer)
		return -EINVAL;
	if (!(uurb->type == USBDEVFS_URB_TYPE_CONTROL &&
	    (uurb->endpoint & ~USB_ENDPOINT_DIR_MASK) == 0)) {
		ifnum = findintfep(ps->dev, uurb->endpoint);
		if (ifnum < 0)
			return ifnum;
		ret = checkintf(ps, ifnum);
		if (ret)
			return ret;
	}
	if ((uurb->endpoint & USB_ENDPOINT_DIR_MASK) != 0) {
		is_in = 1;
		ep = ps->dev->ep_in[uurb->endpoint & USB_ENDPOINT_NUMBER_MASK];
	} else {
		is_in = 0;
		ep = ps->dev->ep_out[uurb->endpoint & USB_ENDPOINT_NUMBER_MASK];
	}
	if (!ep)
		return -ENOENT;

	u = 0;
	switch(uurb->type) {
	case USBDEVFS_URB_TYPE_CONTROL:
		if (!usb_endpoint_xfer_control(&ep->desc))
			return -EINVAL;
		/* min 8 byte setup packet */
		if (uurb->buffer_length < 8)
			return -EINVAL;
		dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
		if (!dr)
			return -ENOMEM;
		if (copy_from_user(dr, uurb->buffer, 8)) {
			ret = -EFAULT;
			goto error;
		}
		if (uurb->buffer_length < (le16_to_cpup(&dr->wLength) + 8)) {
			ret = -EINVAL;
			goto error;
		}
		ret = check_ctrlrecip(ps, dr->bRequestType, dr->bRequest,
				      le16_to_cpup(&dr->wIndex));
		if (ret)
			goto error;
		uurb->number_of_packets = 0;
		uurb->buffer_length = le16_to_cpup(&dr->wLength);
		uurb->buffer += 8;
		if ((dr->bRequestType & USB_DIR_IN) && uurb->buffer_length) {
			is_in = 1;
			uurb->endpoint |= USB_DIR_IN;
		} else {
			is_in = 0;
			uurb->endpoint &= ~USB_DIR_IN;
		}
		snoop(&ps->dev->dev, "control urb: bRequestType=%02x "
			"bRequest=%02x wValue=%04x "
			"wIndex=%04x wLength=%04x\n",
			dr->bRequestType, dr->bRequest,
			__le16_to_cpup(&dr->wValue),
			__le16_to_cpup(&dr->wIndex),
			__le16_to_cpup(&dr->wLength));
		u = sizeof(struct usb_ctrlrequest);
		break;

	case USBDEVFS_URB_TYPE_BULK:
		switch (usb_endpoint_type(&ep->desc)) {
		case USB_ENDPOINT_XFER_CONTROL:
		case USB_ENDPOINT_XFER_ISOC:
			return -EINVAL;
		case USB_ENDPOINT_XFER_INT:
			/* allow single-shot interrupt transfers */
			uurb->type = USBDEVFS_URB_TYPE_INTERRUPT;
			goto interrupt_urb;
		}
		uurb->number_of_packets = 0;
		num_sgs = DIV_ROUND_UP(uurb->buffer_length, USB_SG_SIZE);
		if (num_sgs == 1 || num_sgs > ps->dev->bus->sg_tablesize)
			num_sgs = 0;
		break;

	case USBDEVFS_URB_TYPE_INTERRUPT:
		if (!usb_endpoint_xfer_int(&ep->desc))
			return -EINVAL;
 interrupt_urb:
		uurb->number_of_packets = 0;
		break;

	case USBDEVFS_URB_TYPE_ISO:
		/* arbitrary limit */
		if (uurb->number_of_packets < 1 ||
		    uurb->number_of_packets > 128)
			return -EINVAL;
		if (!usb_endpoint_xfer_isoc(&ep->desc))
			return -EINVAL;
		isofrmlen = sizeof(struct usbdevfs_iso_packet_desc) *
				   uurb->number_of_packets;
		if (!(isopkt = kmalloc(isofrmlen, GFP_KERNEL)))
			return -ENOMEM;
		if (copy_from_user(isopkt, iso_frame_desc, isofrmlen)) {
			ret = -EFAULT;
			goto error;
		}
		for (totlen = u = 0; u < uurb->number_of_packets; u++) {
			/* arbitrary limit,
			 * sufficient for USB 2.0 high-bandwidth iso */
			if (isopkt[u].length > 8192) {
				ret = -EINVAL;
				goto error;
			}
			totlen += isopkt[u].length;
		}
		u *= sizeof(struct usb_iso_packet_descriptor);
		uurb->buffer_length = totlen;
		break;

	default:
		return -EINVAL;
	}

	if (uurb->buffer_length >= USBFS_XFER_MAX) {
		ret = -EINVAL;
		goto error;
	}
	if (uurb->buffer_length > 0 &&
			!access_ok(is_in ? VERIFY_WRITE : VERIFY_READ,
				uurb->buffer, uurb->buffer_length)) {
		ret = -EFAULT;
		goto error;
	}
	as = alloc_async(uurb->number_of_packets);
	if (!as) {
		ret = -ENOMEM;
		goto error;
	}

	u += sizeof(struct async) + sizeof(struct urb) + uurb->buffer_length +
	     num_sgs * sizeof(struct scatterlist);
	ret = usbfs_increase_memory_usage(u);
	if (ret)
		goto error;
	as->mem_usage = u;

	if (num_sgs) {
		as->urb->sg = kmalloc(num_sgs * sizeof(struct scatterlist),
				      GFP_KERNEL);
		if (!as->urb->sg) {
			ret = -ENOMEM;
			goto error;
		}
		as->urb->num_sgs = num_sgs;
		sg_init_table(as->urb->sg, as->urb->num_sgs);

		totlen = uurb->buffer_length;
		for (i = 0; i < as->urb->num_sgs; i++) {
			u = (totlen > USB_SG_SIZE) ? USB_SG_SIZE : totlen;
			buf = kmalloc(u, GFP_KERNEL);
			if (!buf) {
				ret = -ENOMEM;
				goto error;
			}
			sg_set_buf(&as->urb->sg[i], buf, u);

			if (!is_in) {
				if (copy_from_user(buf, uurb->buffer, u)) {
					ret = -EFAULT;
					goto error;
				}
				uurb->buffer += u;
			}
			totlen -= u;
		}
	} else if (uurb->buffer_length > 0) {
		as->urb->transfer_buffer = kmalloc(uurb->buffer_length,
				GFP_KERNEL);
		if (!as->urb->transfer_buffer) {
			ret = -ENOMEM;
			goto error;
		}

		if (!is_in) {
			if (copy_from_user(as->urb->transfer_buffer,
					   uurb->buffer,
					   uurb->buffer_length)) {
				ret = -EFAULT;
				goto error;
			}
		} else if (uurb->type == USBDEVFS_URB_TYPE_ISO) {
			/*
			 * Isochronous input data may end up being
			 * discontiguous if some of the packets are short.
			 * Clear the buffer so that the gaps don't leak
			 * kernel data to userspace.
			 */
			memset(as->urb->transfer_buffer, 0,
					uurb->buffer_length);
		}
	}
	as->urb->dev = ps->dev;
	as->urb->pipe = (uurb->type << 30) |
			__create_pipe(ps->dev, uurb->endpoint & 0xf) |
			(uurb->endpoint & USB_DIR_IN);

	/* This tedious sequence is necessary because the URB_* flags
	 * are internal to the kernel and subject to change, whereas
	 * the USBDEVFS_URB_* flags are a user API and must not be changed.
	 */
	u = (is_in ? URB_DIR_IN : URB_DIR_OUT);
	if (uurb->flags & USBDEVFS_URB_ISO_ASAP)
		u |= URB_ISO_ASAP;
	if (uurb->flags & USBDEVFS_URB_SHORT_NOT_OK)
		u |= URB_SHORT_NOT_OK;
	if (uurb->flags & USBDEVFS_URB_NO_FSBR)
		u |= URB_NO_FSBR;
	if (uurb->flags & USBDEVFS_URB_ZERO_PACKET)
		u |= URB_ZERO_PACKET;
	if (uurb->flags & USBDEVFS_URB_NO_INTERRUPT)
		u |= URB_NO_INTERRUPT;
	as->urb->transfer_flags = u;

	as->urb->transfer_buffer_length = uurb->buffer_length;
	as->urb->setup_packet = (unsigned char *)dr;
	dr = NULL;
	as->urb->start_frame = uurb->start_frame;
	as->urb->number_of_packets = uurb->number_of_packets;
	if (uurb->type == USBDEVFS_URB_TYPE_ISO ||
			ps->dev->speed == USB_SPEED_HIGH)
		as->urb->interval = 1 << min(15, ep->desc.bInterval - 1);
	else
		as->urb->interval = ep->desc.bInterval;
	as->urb->context = as;
	as->urb->complete = async_completed;
	for (totlen = u = 0; u < uurb->number_of_packets; u++) {
		as->urb->iso_frame_desc[u].offset = totlen;
		as->urb->iso_frame_desc[u].length = isopkt[u].length;
		totlen += isopkt[u].length;
	}
	kfree(isopkt);
	isopkt = NULL;
	as->ps = ps;
	as->userurb = arg;
	if (is_in && uurb->buffer_length > 0)
		as->userbuffer = uurb->buffer;
	else
		as->userbuffer = NULL;
	as->signr = uurb->signr;
	as->ifnum = ifnum;
	as->pid = get_pid(task_pid(current));
	as->cred = get_current_cred();
	security_task_getsecid(current, &as->secid);
	snoop_urb(ps->dev, as->userurb, as->urb->pipe,
			as->urb->transfer_buffer_length, 0, SUBMIT,
			NULL, 0);
	if (!is_in)
		snoop_urb_data(as->urb, as->urb->transfer_buffer_length);

	async_newpending(as);

	if (usb_endpoint_xfer_bulk(&ep->desc)) {
		spin_lock_irq(&ps->lock);

		/* Not exactly the endpoint address; the direction bit is
		 * shifted to the 0x10 position so that the value will be
		 * between 0 and 31.
		 */
		as->bulk_addr = usb_endpoint_num(&ep->desc) |
			((ep->desc.bEndpointAddress & USB_ENDPOINT_DIR_MASK)
				>> 3);

		/* If this bulk URB is the start of a new transfer, re-enable
		 * the endpoint.  Otherwise mark it as a continuation URB.
		 */
		if (uurb->flags & USBDEVFS_URB_BULK_CONTINUATION)
			as->bulk_status = AS_CONTINUATION;
		else
			ps->disabled_bulk_eps &= ~(1 << as->bulk_addr);

		/* Don't accept continuation URBs if the endpoint is
		 * disabled because of an earlier error.
		 */
		if (ps->disabled_bulk_eps & (1 << as->bulk_addr))
			ret = -EREMOTEIO;
		else
			ret = usb_submit_urb(as->urb, GFP_ATOMIC);
		spin_unlock_irq(&ps->lock);
	} else {
		ret = usb_submit_urb(as->urb, GFP_KERNEL);
	}

	if (ret) {
		dev_printk(KERN_DEBUG, &ps->dev->dev,
			   "usbfs: usb_submit_urb returned %d\n", ret);
		snoop_urb(ps->dev, as->userurb, as->urb->pipe,
				0, ret, COMPLETE, NULL, 0);
		async_removepending(as);
		goto error;
	}
	return 0;

 error:
	kfree(isopkt);
	kfree(dr);
	if (as)
		free_async(as);
	return ret;
}

static int proc_submiturb(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_urb uurb;

	if (copy_from_user(&uurb, arg, sizeof(uurb)))
		return -EFAULT;

	return proc_do_submiturb(ps, &uurb,
			(((struct usbdevfs_urb __user *)arg)->iso_frame_desc),
			arg);
}

static int proc_unlinkurb(struct dev_state *ps, void __user *arg)
{
	struct urb *urb;
	struct async *as;
	unsigned long flags;

	spin_lock_irqsave(&ps->lock, flags);
	as = async_getpending(ps, arg);
	if (!as) {
		spin_unlock_irqrestore(&ps->lock, flags);
		return -EINVAL;
	}

	urb = as->urb;
	usb_get_urb(urb);
	spin_unlock_irqrestore(&ps->lock, flags);

	usb_kill_urb(urb);
	usb_put_urb(urb);

	return 0;
}

static int processcompl(struct async *as, void __user * __user *arg)
{
	struct urb *urb = as->urb;
	struct usbdevfs_urb __user *userurb = as->userurb;
	void __user *addr = as->userurb;
	unsigned int i;

	if (as->userbuffer && urb->actual_length) {
		if (copy_urb_data_to_user(as->userbuffer, urb))
			goto err_out;
	}
	if (put_user(as->status, &userurb->status))
		goto err_out;
	if (put_user(urb->actual_length, &userurb->actual_length))
		goto err_out;
	if (put_user(urb->error_count, &userurb->error_count))
		goto err_out;

	if (usb_endpoint_xfer_isoc(&urb->ep->desc)) {
		for (i = 0; i < urb->number_of_packets; i++) {
			if (put_user(urb->iso_frame_desc[i].actual_length,
				     &userurb->iso_frame_desc[i].actual_length))
				goto err_out;
			if (put_user(urb->iso_frame_desc[i].status,
				     &userurb->iso_frame_desc[i].status))
				goto err_out;
		}
	}

	if (put_user(addr, (void __user * __user *)arg))
		return -EFAULT;
	return 0;

err_out:
	return -EFAULT;
}

static struct async *reap_as(struct dev_state *ps)
{
	DECLARE_WAITQUEUE(wait, current);
	struct async *as = NULL;
	struct usb_device *dev = ps->dev;

	add_wait_queue(&ps->wait, &wait);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		as = async_getcompleted(ps);
		if (as)
			break;
		if (signal_pending(current))
			break;
		usb_unlock_device(dev);
		schedule();
		usb_lock_device(dev);
	}
	remove_wait_queue(&ps->wait, &wait);
	set_current_state(TASK_RUNNING);
	return as;
}

static int proc_reapurb(struct dev_state *ps, void __user *arg)
{
	struct async *as = reap_as(ps);
	if (as) {
		int retval = processcompl(as, (void __user * __user *)arg);
		free_async(as);
		return retval;
	}
	if (signal_pending(current))
		return -EINTR;
	return -EIO;
}

static int proc_reapurbnonblock(struct dev_state *ps, void __user *arg)
{
	int retval;
	struct async *as;

	as = async_getcompleted(ps);
	retval = -EAGAIN;
	if (as) {
		retval = processcompl(as, (void __user * __user *)arg);
		free_async(as);
	}
	return retval;
}

#ifdef CONFIG_COMPAT
static int proc_control_compat(struct dev_state *ps,
				struct usbdevfs_ctrltransfer32 __user *p32)
{
        struct usbdevfs_ctrltransfer __user *p;
        __u32 udata;
        p = compat_alloc_user_space(sizeof(*p));
        if (copy_in_user(p, p32, (sizeof(*p32) - sizeof(compat_caddr_t))) ||
            get_user(udata, &p32->data) ||
	    put_user(compat_ptr(udata), &p->data))
		return -EFAULT;
        return proc_control(ps, p);
}

static int proc_bulk_compat(struct dev_state *ps,
			struct usbdevfs_bulktransfer32 __user *p32)
{
        struct usbdevfs_bulktransfer __user *p;
        compat_uint_t n;
        compat_caddr_t addr;

        p = compat_alloc_user_space(sizeof(*p));

        if (get_user(n, &p32->ep) || put_user(n, &p->ep) ||
            get_user(n, &p32->len) || put_user(n, &p->len) ||
            get_user(n, &p32->timeout) || put_user(n, &p->timeout) ||
            get_user(addr, &p32->data) || put_user(compat_ptr(addr), &p->data))
                return -EFAULT;

        return proc_bulk(ps, p);
}
static int proc_disconnectsignal_compat(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_disconnectsignal32 ds;

	if (copy_from_user(&ds, arg, sizeof(ds)))
		return -EFAULT;
	ps->discsignr = ds.signr;
	ps->disccontext = compat_ptr(ds.context);
	return 0;
}

static int get_urb32(struct usbdevfs_urb *kurb,
		     struct usbdevfs_urb32 __user *uurb)
{
	__u32  uptr;
	if (!access_ok(VERIFY_READ, uurb, sizeof(*uurb)) ||
	    __get_user(kurb->type, &uurb->type) ||
	    __get_user(kurb->endpoint, &uurb->endpoint) ||
	    __get_user(kurb->status, &uurb->status) ||
	    __get_user(kurb->flags, &uurb->flags) ||
	    __get_user(kurb->buffer_length, &uurb->buffer_length) ||
	    __get_user(kurb->actual_length, &uurb->actual_length) ||
	    __get_user(kurb->start_frame, &uurb->start_frame) ||
	    __get_user(kurb->number_of_packets, &uurb->number_of_packets) ||
	    __get_user(kurb->error_count, &uurb->error_count) ||
	    __get_user(kurb->signr, &uurb->signr))
		return -EFAULT;

	if (__get_user(uptr, &uurb->buffer))
		return -EFAULT;
	kurb->buffer = compat_ptr(uptr);
	if (__get_user(uptr, &uurb->usercontext))
		return -EFAULT;
	kurb->usercontext = compat_ptr(uptr);

	return 0;
}

static int proc_submiturb_compat(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_urb uurb;

	if (get_urb32(&uurb, (struct usbdevfs_urb32 __user *)arg))
		return -EFAULT;

	return proc_do_submiturb(ps, &uurb,
			((struct usbdevfs_urb32 __user *)arg)->iso_frame_desc,
			arg);
}

static int processcompl_compat(struct async *as, void __user * __user *arg)
{
	struct urb *urb = as->urb;
	struct usbdevfs_urb32 __user *userurb = as->userurb;
	void __user *addr = as->userurb;
	unsigned int i;

	if (as->userbuffer && urb->actual_length) {
		if (copy_urb_data_to_user(as->userbuffer, urb))
			return -EFAULT;
	}
	if (put_user(as->status, &userurb->status))
		return -EFAULT;
	if (put_user(urb->actual_length, &userurb->actual_length))
		return -EFAULT;
	if (put_user(urb->error_count, &userurb->error_count))
		return -EFAULT;

	if (usb_endpoint_xfer_isoc(&urb->ep->desc)) {
		for (i = 0; i < urb->number_of_packets; i++) {
			if (put_user(urb->iso_frame_desc[i].actual_length,
				     &userurb->iso_frame_desc[i].actual_length))
				return -EFAULT;
			if (put_user(urb->iso_frame_desc[i].status,
				     &userurb->iso_frame_desc[i].status))
				return -EFAULT;
		}
	}

	if (put_user(ptr_to_compat(addr), (u32 __user *)arg))
		return -EFAULT;
	return 0;
}

static int proc_reapurb_compat(struct dev_state *ps, void __user *arg)
{
	struct async *as = reap_as(ps);
	if (as) {
		int retval = processcompl_compat(as, (void __user * __user *)arg);
		free_async(as);
		return retval;
	}
	if (signal_pending(current))
		return -EINTR;
	return -EIO;
}

static int proc_reapurbnonblock_compat(struct dev_state *ps, void __user *arg)
{
	int retval;
	struct async *as;

	retval = -EAGAIN;
	as = async_getcompleted(ps);
	if (as) {
		retval = processcompl_compat(as, (void __user * __user *)arg);
		free_async(as);
	}
	return retval;
}


#endif

static int proc_disconnectsignal(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_disconnectsignal ds;

	if (copy_from_user(&ds, arg, sizeof(ds)))
		return -EFAULT;
	ps->discsignr = ds.signr;
	ps->disccontext = ds.context;
	return 0;
}

static int proc_claiminterface(struct dev_state *ps, void __user *arg)
{
	unsigned int ifnum;

	if (get_user(ifnum, (unsigned int __user *)arg))
		return -EFAULT;
	return claimintf(ps, ifnum);
}

static int proc_releaseinterface(struct dev_state *ps, void __user *arg)
{
	unsigned int ifnum;
	int ret;

	if (get_user(ifnum, (unsigned int __user *)arg))
		return -EFAULT;
	if ((ret = releaseintf(ps, ifnum)) < 0)
		return ret;
	destroy_async_on_interface (ps, ifnum);
	return 0;
}

static int proc_ioctl(struct dev_state *ps, struct usbdevfs_ioctl *ctl)
{
	int			size;
	void			*buf = NULL;
	int			retval = 0;
	struct usb_interface    *intf = NULL;
	struct usb_driver       *driver = NULL;

	/* alloc buffer */
	if ((size = _IOC_SIZE(ctl->ioctl_code)) > 0) {
		if ((buf = kmalloc(size, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		if ((_IOC_DIR(ctl->ioctl_code) & _IOC_WRITE)) {
			if (copy_from_user(buf, ctl->data, size)) {
				kfree(buf);
				return -EFAULT;
			}
		} else {
			memset(buf, 0, size);
		}
	}

	if (!connected(ps)) {
		kfree(buf);
		return -ENODEV;
	}

	if (ps->dev->state != USB_STATE_CONFIGURED)
		retval = -EHOSTUNREACH;
	else if (!(intf = usb_ifnum_to_if(ps->dev, ctl->ifno)))
		retval = -EINVAL;
	else switch (ctl->ioctl_code) {

	/* disconnect kernel driver from interface */
	case USBDEVFS_DISCONNECT:
		if (intf->dev.driver) {
			driver = to_usb_driver(intf->dev.driver);
			dev_dbg(&intf->dev, "disconnect by usbfs\n");
			usb_driver_release_interface(driver, intf);
		} else
			retval = -ENODATA;
		break;

	/* let kernel drivers try to (re)bind to the interface */
	case USBDEVFS_CONNECT:
		if (!intf->dev.driver)
			retval = device_attach(&intf->dev);
		else
			retval = -EBUSY;
		break;

	/* talk directly to the interface's driver */
	default:
		if (intf->dev.driver)
			driver = to_usb_driver(intf->dev.driver);
		if (driver == NULL || driver->unlocked_ioctl == NULL) {
			retval = -ENOTTY;
		} else {
			retval = driver->unlocked_ioctl(intf, ctl->ioctl_code, buf);
			if (retval == -ENOIOCTLCMD)
				retval = -ENOTTY;
		}
	}

	/* cleanup and return */
	if (retval >= 0
			&& (_IOC_DIR(ctl->ioctl_code) & _IOC_READ) != 0
			&& size > 0
			&& copy_to_user(ctl->data, buf, size) != 0)
		retval = -EFAULT;

	kfree(buf);
	return retval;
}

static int proc_ioctl_default(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_ioctl	ctrl;

	if (copy_from_user(&ctrl, arg, sizeof(ctrl)))
		return -EFAULT;
	return proc_ioctl(ps, &ctrl);
}

#ifdef CONFIG_COMPAT
static int proc_ioctl_compat(struct dev_state *ps, compat_uptr_t arg)
{
	struct usbdevfs_ioctl32 __user *uioc;
	struct usbdevfs_ioctl ctrl;
	u32 udata;

	uioc = compat_ptr((long)arg);
	if (!access_ok(VERIFY_READ, uioc, sizeof(*uioc)) ||
	    __get_user(ctrl.ifno, &uioc->ifno) ||
	    __get_user(ctrl.ioctl_code, &uioc->ioctl_code) ||
	    __get_user(udata, &uioc->data))
		return -EFAULT;
	ctrl.data = compat_ptr(udata);

	return proc_ioctl(ps, &ctrl);
}
#endif

static int proc_claim_port(struct dev_state *ps, void __user *arg)
{
	unsigned portnum;
	int rc;

	if (get_user(portnum, (unsigned __user *) arg))
		return -EFAULT;
	rc = usb_hub_claim_port(ps->dev, portnum, ps);
	if (rc == 0)
		snoop(&ps->dev->dev, "port %d claimed by process %d: %s\n",
			portnum, task_pid_nr(current), current->comm);
	return rc;
}

static int proc_release_port(struct dev_state *ps, void __user *arg)
{
	unsigned portnum;

	if (get_user(portnum, (unsigned __user *) arg))
		return -EFAULT;
	return usb_hub_release_port(ps->dev, portnum, ps);
}

static int proc_get_capabilities(struct dev_state *ps, void __user *arg)
{
	__u32 caps;

	caps = USBDEVFS_CAP_ZERO_PACKET | USBDEVFS_CAP_NO_PACKET_SIZE_LIM;
	if (!ps->dev->bus->no_stop_on_short)
		caps |= USBDEVFS_CAP_BULK_CONTINUATION;
	if (ps->dev->bus->sg_tablesize)
		caps |= USBDEVFS_CAP_BULK_SCATTER_GATHER;

	if (put_user(caps, (__u32 __user *)arg))
		return -EFAULT;

	return 0;
}

static int proc_disconnect_claim(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_disconnect_claim dc;
	struct usb_interface *intf;

	if (copy_from_user(&dc, arg, sizeof(dc)))
		return -EFAULT;

	intf = usb_ifnum_to_if(ps->dev, dc.interface);
	if (!intf)
		return -EINVAL;

	if (intf->dev.driver) {
		struct usb_driver *driver = to_usb_driver(intf->dev.driver);

		if ((dc.flags & USBDEVFS_DISCONNECT_CLAIM_IF_DRIVER) &&
				strncmp(dc.driver, intf->dev.driver->name,
					sizeof(dc.driver)) != 0)
			return -EBUSY;

		if ((dc.flags & USBDEVFS_DISCONNECT_CLAIM_EXCEPT_DRIVER) &&
				strncmp(dc.driver, intf->dev.driver->name,
					sizeof(dc.driver)) == 0)
			return -EBUSY;

		dev_dbg(&intf->dev, "disconnect by usbfs\n");
		usb_driver_release_interface(driver, intf);
	}

	return claimintf(ps, dc.interface);
}

/*
 * NOTE:  All requests here that have interface numbers as parameters
 * are assuming that somehow the configuration has been prevented from
 * changing.  But there's no mechanism to ensure that...
 */
static long usbdev_do_ioctl(struct file *file, unsigned int cmd,
				void __user *p)
{
	struct dev_state *ps = file->private_data;
	struct inode *inode = file_inode(file);
	struct usb_device *dev = ps->dev;
	int ret = -ENOTTY;

	if (!(file->f_mode & FMODE_WRITE))
		return -EPERM;

	usb_lock_device(dev);
	if (!connected(ps)) {
		usb_unlock_device(dev);
		return -ENODEV;
	}

	switch (cmd) {
	case USBDEVFS_CONTROL:
		snoop(&dev->dev, "%s: CONTROL\n", __func__);
		ret = proc_control(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_BULK:
		snoop(&dev->dev, "%s: BULK\n", __func__);
		ret = proc_bulk(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_RESETEP:
		snoop(&dev->dev, "%s: RESETEP\n", __func__);
		ret = proc_resetep(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_RESET:
		snoop(&dev->dev, "%s: RESET\n", __func__);
		ret = proc_resetdevice(ps);
		break;

	case USBDEVFS_CLEAR_HALT:
		snoop(&dev->dev, "%s: CLEAR_HALT\n", __func__);
		ret = proc_clearhalt(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_GETDRIVER:
		snoop(&dev->dev, "%s: GETDRIVER\n", __func__);
		ret = proc_getdriver(ps, p);
		break;

	case USBDEVFS_CONNECTINFO:
		snoop(&dev->dev, "%s: CONNECTINFO\n", __func__);
		ret = proc_connectinfo(ps, p);
		break;

	case USBDEVFS_SETINTERFACE:
		snoop(&dev->dev, "%s: SETINTERFACE\n", __func__);
		ret = proc_setintf(ps, p);
		break;

	case USBDEVFS_SETCONFIGURATION:
		snoop(&dev->dev, "%s: SETCONFIGURATION\n", __func__);
		ret = proc_setconfig(ps, p);
		break;

	case USBDEVFS_SUBMITURB:
		snoop(&dev->dev, "%s: SUBMITURB\n", __func__);
		ret = proc_submiturb(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

#ifdef CONFIG_COMPAT
	case USBDEVFS_CONTROL32:
		snoop(&dev->dev, "%s: CONTROL32\n", __func__);
		ret = proc_control_compat(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_BULK32:
		snoop(&dev->dev, "%s: BULK32\n", __func__);
		ret = proc_bulk_compat(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_DISCSIGNAL32:
		snoop(&dev->dev, "%s: DISCSIGNAL32\n", __func__);
		ret = proc_disconnectsignal_compat(ps, p);
		break;

	case USBDEVFS_SUBMITURB32:
		snoop(&dev->dev, "%s: SUBMITURB32\n", __func__);
		ret = proc_submiturb_compat(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_REAPURB32:
		snoop(&dev->dev, "%s: REAPURB32\n", __func__);
		ret = proc_reapurb_compat(ps, p);
		break;

	case USBDEVFS_REAPURBNDELAY32:
		snoop(&dev->dev, "%s: REAPURBNDELAY32\n", __func__);
		ret = proc_reapurbnonblock_compat(ps, p);
		break;

	case USBDEVFS_IOCTL32:
		snoop(&dev->dev, "%s: IOCTL32\n", __func__);
		ret = proc_ioctl_compat(ps, ptr_to_compat(p));
		break;
#endif

	case USBDEVFS_DISCARDURB:
		snoop(&dev->dev, "%s: DISCARDURB\n", __func__);
		ret = proc_unlinkurb(ps, p);
		break;

	case USBDEVFS_REAPURB:
		snoop(&dev->dev, "%s: REAPURB\n", __func__);
		ret = proc_reapurb(ps, p);
		break;

	case USBDEVFS_REAPURBNDELAY:
		snoop(&dev->dev, "%s: REAPURBNDELAY\n", __func__);
		ret = proc_reapurbnonblock(ps, p);
		break;

	case USBDEVFS_DISCSIGNAL:
		snoop(&dev->dev, "%s: DISCSIGNAL\n", __func__);
		ret = proc_disconnectsignal(ps, p);
		break;

	case USBDEVFS_CLAIMINTERFACE:
		snoop(&dev->dev, "%s: CLAIMINTERFACE\n", __func__);
		ret = proc_claiminterface(ps, p);
		break;

	case USBDEVFS_RELEASEINTERFACE:
		snoop(&dev->dev, "%s: RELEASEINTERFACE\n", __func__);
		ret = proc_releaseinterface(ps, p);
		break;

	case USBDEVFS_IOCTL:
		snoop(&dev->dev, "%s: IOCTL\n", __func__);
		ret = proc_ioctl_default(ps, p);
		break;

	case USBDEVFS_CLAIM_PORT:
		snoop(&dev->dev, "%s: CLAIM_PORT\n", __func__);
		ret = proc_claim_port(ps, p);
		break;

	case USBDEVFS_RELEASE_PORT:
		snoop(&dev->dev, "%s: RELEASE_PORT\n", __func__);
		ret = proc_release_port(ps, p);
		break;
	case USBDEVFS_GET_CAPABILITIES:
		ret = proc_get_capabilities(ps, p);
		break;
	case USBDEVFS_DISCONNECT_CLAIM:
		ret = proc_disconnect_claim(ps, p);
		break;
	}
	usb_unlock_device(dev);
	if (ret >= 0)
		inode->i_atime = CURRENT_TIME;
	return ret;
}

static long usbdev_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	int ret;

	ret = usbdev_do_ioctl(file, cmd, (void __user *)arg);

	return ret;
}

#ifdef CONFIG_COMPAT
static long usbdev_compat_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	int ret;

	ret = usbdev_do_ioctl(file, cmd, compat_ptr(arg));

	return ret;
}
#endif

/* No kernel lock - fine */
static unsigned int usbdev_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct dev_state *ps = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &ps->wait, wait);
	if (file->f_mode & FMODE_WRITE && !list_empty(&ps->async_completed))
		mask |= POLLOUT | POLLWRNORM;
	if (!connected(ps))
		mask |= POLLERR | POLLHUP;
	return mask;
}

const struct file_operations usbdev_file_operations = {
	.owner =	  THIS_MODULE,
	.llseek =	  usbdev_lseek,
	.read =		  usbdev_read,
	.poll =		  usbdev_poll,
	.unlocked_ioctl = usbdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =   usbdev_compat_ioctl,
#endif
	.open =		  usbdev_open,
	.release =	  usbdev_release,
};

static void usbdev_remove(struct usb_device *udev)
{
	struct dev_state *ps;
	struct siginfo sinfo;

	while (!list_empty(&udev->filelist)) {
		ps = list_entry(udev->filelist.next, struct dev_state, list);
		destroy_all_async(ps);
		wake_up_all(&ps->wait);
		list_del_init(&ps->list);
		if (ps->discsignr) {
			sinfo.si_signo = ps->discsignr;
			sinfo.si_errno = EPIPE;
			sinfo.si_code = SI_ASYNCIO;
			sinfo.si_addr = ps->disccontext;
			kill_pid_info_as_cred(ps->discsignr, &sinfo,
					ps->disc_pid, ps->cred, ps->secid);
		}
	}
}

static int usbdev_notify(struct notifier_block *self,
			       unsigned long action, void *dev)
{
	switch (action) {
	case USB_DEVICE_ADD:
		break;
	case USB_DEVICE_REMOVE:
		usbdev_remove(dev);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block usbdev_nb = {
	.notifier_call = 	usbdev_notify,
};

static struct cdev usb_device_cdev;

int __init usb_devio_init(void)
{
	int retval;

	retval = register_chrdev_region(USB_DEVICE_DEV, USB_DEVICE_MAX,
					"usb_device");
	if (retval) {
		printk(KERN_ERR "Unable to register minors for usb_device\n");
		goto out;
	}
	cdev_init(&usb_device_cdev, &usbdev_file_operations);
	retval = cdev_add(&usb_device_cdev, USB_DEVICE_DEV, USB_DEVICE_MAX);
	if (retval) {
		printk(KERN_ERR "Unable to get usb_device major %d\n",
		       USB_DEVICE_MAJOR);
		goto error_cdev;
	}
	usb_register_notify(&usbdev_nb);
out:
	return retval;

error_cdev:
	unregister_chrdev_region(USB_DEVICE_DEV, USB_DEVICE_MAX);
	goto out;
}

void usb_devio_cleanup(void)
{
	usb_unregister_notify(&usbdev_nb);
	cdev_del(&usb_device_cdev);
	unregister_chrdev_region(USB_DEVICE_DEV, USB_DEVICE_MAX);
}
