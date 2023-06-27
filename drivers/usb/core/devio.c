// SPDX-License-Identifier: GPL-2.0+
/*****************************************************************************/

/*
 *      devio.c  --  User space communication with USB devices.
 *
 *      Copyright (C) 1999-2000  Thomas Sailer (sailer@ife.ee.ethz.ch)
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
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/signal.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/usbdevice_fs.h>
#include <linux/usb/hcd.h>	/* for usbcore internals */
#include <linux/usb/quirks.h>
#include <linux/cdev.h>
#include <linux/notifier.h>
#include <linux/security.h>
#include <linux/user_namespace.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <asm/byteorder.h>
#include <linux/moduleparam.h>

#include "usb.h"

#ifdef CONFIG_PM
#define MAYBE_CAP_SUSPEND	USBDEVFS_CAP_SUSPEND
#else
#define MAYBE_CAP_SUSPEND	0
#endif

#define USB_MAXBUS			64
#define USB_DEVICE_MAX			(USB_MAXBUS * 128)
#define USB_SG_SIZE			16384 /* split-size for large txs */

/* Mutual exclusion for ps->list in resume vs. release and remove */
static DEFINE_MUTEX(usbfs_mutex);

struct usb_dev_state {
	struct list_head list;      /* state list */
	struct usb_device *dev;
	struct file *file;
	spinlock_t lock;            /* protects the async urb lists */
	struct list_head async_pending;
	struct list_head async_completed;
	struct list_head memory_list;
	wait_queue_head_t wait;     /* wake up if a request completed */
	wait_queue_head_t wait_for_resume;   /* wake up upon runtime resume */
	unsigned int discsignr;
	struct pid *disc_pid;
	const struct cred *cred;
	sigval_t disccontext;
	unsigned long ifclaimed;
	u32 disabled_bulk_eps;
	unsigned long interface_allowed_mask;
	int not_yet_resumed;
	bool suspend_allowed;
	bool privileges_dropped;
};

struct usb_memory {
	struct list_head memlist;
	int vma_use_count;
	int urb_use_count;
	u32 size;
	void *mem;
	dma_addr_t dma_handle;
	unsigned long vm_start;
	struct usb_dev_state *ps;
};

struct async {
	struct list_head asynclist;
	struct usb_dev_state *ps;
	struct pid *pid;
	const struct cred *cred;
	unsigned int signr;
	unsigned int ifnum;
	void __user *userbuffer;
	void __user *userurb;
	sigval_t userurb_sigval;
	struct urb *urb;
	struct usb_memory *usbm;
	unsigned int mem_usage;
	int status;
	u8 bulk_addr;
	u8 bulk_status;
};

static bool usbfs_snoop;
module_param(usbfs_snoop, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(usbfs_snoop, "true to log all usbfs traffic");

static unsigned usbfs_snoop_max = 65536;
module_param(usbfs_snoop_max, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(usbfs_snoop_max,
		"maximum number of bytes to print while snooping");

#define snoop(dev, format, arg...)				\
	do {							\
		if (usbfs_snoop)				\
			dev_info(dev, format, ## arg);		\
	} while (0)

enum snoop_when {
	SUBMIT, COMPLETE
};

#define USB_DEVICE_DEV		MKDEV(USB_DEVICE_MAJOR, 0)

/* Limit on the total amount of memory we can allocate for transfers */
static u32 usbfs_memory_mb = 16;
module_param(usbfs_memory_mb, uint, 0644);
MODULE_PARM_DESC(usbfs_memory_mb,
		"maximum MB allowed for usbfs buffers (0 = no limit)");

/* Hard limit, necessary to avoid arithmetic overflow */
#define USBFS_XFER_MAX         (UINT_MAX / 2 - 1000000)

static DEFINE_SPINLOCK(usbfs_memory_usage_lock);
static u64 usbfs_memory_usage;	/* Total memory currently allocated */

/* Check whether it's okay to allocate more memory for a transfer */
static int usbfs_increase_memory_usage(u64 amount)
{
	u64 lim, total_mem;
	unsigned long flags;
	int ret;

	lim = READ_ONCE(usbfs_memory_mb);
	lim <<= 20;

	ret = 0;
	spin_lock_irqsave(&usbfs_memory_usage_lock, flags);
	total_mem = usbfs_memory_usage + amount;
	if (lim > 0 && total_mem > lim)
		ret = -ENOMEM;
	else
		usbfs_memory_usage = total_mem;
	spin_unlock_irqrestore(&usbfs_memory_usage_lock, flags);

	return ret;
}

/* Memory for a transfer is being deallocated */
static void usbfs_decrease_memory_usage(u64 amount)
{
	unsigned long flags;

	spin_lock_irqsave(&usbfs_memory_usage_lock, flags);
	if (amount > usbfs_memory_usage)
		usbfs_memory_usage = 0;
	else
		usbfs_memory_usage -= amount;
	spin_unlock_irqrestore(&usbfs_memory_usage_lock, flags);
}

static int connected(struct usb_dev_state *ps)
{
	return (!list_empty(&ps->list) &&
			ps->dev->state != USB_STATE_NOTATTACHED);
}

static void dec_usb_memory_use_count(struct usb_memory *usbm, int *count)
{
	struct usb_dev_state *ps = usbm->ps;
	struct usb_hcd *hcd = bus_to_hcd(ps->dev->bus);
	unsigned long flags;

	spin_lock_irqsave(&ps->lock, flags);
	--*count;
	if (usbm->urb_use_count == 0 && usbm->vma_use_count == 0) {
		list_del(&usbm->memlist);
		spin_unlock_irqrestore(&ps->lock, flags);

		hcd_buffer_free_pages(hcd, usbm->size,
				usbm->mem, usbm->dma_handle);
		usbfs_decrease_memory_usage(
			usbm->size + sizeof(struct usb_memory));
		kfree(usbm);
	} else {
		spin_unlock_irqrestore(&ps->lock, flags);
	}
}

static void usbdev_vm_open(struct vm_area_struct *vma)
{
	struct usb_memory *usbm = vma->vm_private_data;
	unsigned long flags;

	spin_lock_irqsave(&usbm->ps->lock, flags);
	++usbm->vma_use_count;
	spin_unlock_irqrestore(&usbm->ps->lock, flags);
}

static void usbdev_vm_close(struct vm_area_struct *vma)
{
	struct usb_memory *usbm = vma->vm_private_data;

	dec_usb_memory_use_count(usbm, &usbm->vma_use_count);
}

static const struct vm_operations_struct usbdev_vm_ops = {
	.open = usbdev_vm_open,
	.close = usbdev_vm_close
};

static int usbdev_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct usb_memory *usbm = NULL;
	struct usb_dev_state *ps = file->private_data;
	struct usb_hcd *hcd = bus_to_hcd(ps->dev->bus);
	size_t size = vma->vm_end - vma->vm_start;
	void *mem;
	unsigned long flags;
	dma_addr_t dma_handle = DMA_MAPPING_ERROR;
	int ret;

	ret = usbfs_increase_memory_usage(size + sizeof(struct usb_memory));
	if (ret)
		goto error;

	usbm = kzalloc(sizeof(struct usb_memory), GFP_KERNEL);
	if (!usbm) {
		ret = -ENOMEM;
		goto error_decrease_mem;
	}

	mem = hcd_buffer_alloc_pages(hcd,
			size, GFP_USER | __GFP_NOWARN, &dma_handle);
	if (!mem) {
		ret = -ENOMEM;
		goto error_free_usbm;
	}

	memset(mem, 0, size);

	usbm->mem = mem;
	usbm->dma_handle = dma_handle;
	usbm->size = size;
	usbm->ps = ps;
	usbm->vm_start = vma->vm_start;
	usbm->vma_use_count = 1;
	INIT_LIST_HEAD(&usbm->memlist);

	/*
	 * In DMA-unavailable cases, hcd_buffer_alloc_pages allocates
	 * normal pages and assigns DMA_MAPPING_ERROR to dma_handle. Check
	 * whether we are in such cases, and then use remap_pfn_range (or
	 * dma_mmap_coherent) to map normal (or DMA) pages into the user
	 * space, respectively.
	 */
	if (dma_handle == DMA_MAPPING_ERROR) {
		if (remap_pfn_range(vma, vma->vm_start,
				    virt_to_phys(usbm->mem) >> PAGE_SHIFT,
				    size, vma->vm_page_prot) < 0) {
			dec_usb_memory_use_count(usbm, &usbm->vma_use_count);
			return -EAGAIN;
		}
	} else {
		if (dma_mmap_coherent(hcd->self.sysdev, vma, mem, dma_handle,
				      size)) {
			dec_usb_memory_use_count(usbm, &usbm->vma_use_count);
			return -EAGAIN;
		}
	}

	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_ops = &usbdev_vm_ops;
	vma->vm_private_data = usbm;

	spin_lock_irqsave(&ps->lock, flags);
	list_add_tail(&usbm->memlist, &ps->memory_list);
	spin_unlock_irqrestore(&ps->lock, flags);

	return 0;

error_free_usbm:
	kfree(usbm);
error_decrease_mem:
	usbfs_decrease_memory_usage(size + sizeof(struct usb_memory));
error:
	return ret;
}

static ssize_t usbdev_read(struct file *file, char __user *buf, size_t nbytes,
			   loff_t *ppos)
{
	struct usb_dev_state *ps = file->private_data;
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
	if (as->usbm == NULL)
		kfree(as->urb->transfer_buffer);
	else
		dec_usb_memory_use_count(as->usbm, &as->usbm->urb_use_count);

	kfree(as->urb->setup_packet);
	usb_free_urb(as->urb);
	usbfs_decrease_memory_usage(as->mem_usage);
	kfree(as);
}

static void async_newpending(struct async *as)
{
	struct usb_dev_state *ps = as->ps;
	unsigned long flags;

	spin_lock_irqsave(&ps->lock, flags);
	list_add_tail(&as->asynclist, &ps->async_pending);
	spin_unlock_irqrestore(&ps->lock, flags);
}

static void async_removepending(struct async *as)
{
	struct usb_dev_state *ps = as->ps;
	unsigned long flags;

	spin_lock_irqsave(&ps->lock, flags);
	list_del_init(&as->asynclist);
	spin_unlock_irqrestore(&ps->lock, flags);
}

static struct async *async_getcompleted(struct usb_dev_state *ps)
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

static struct async *async_getpending(struct usb_dev_state *ps,
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
			dev_info(&udev->dev, "userurb %px, ep%d %s-%s, "
					"length %u\n",
					userurb, ep, t, d, length);
		else
			dev_info(&udev->dev, "userurb %px, ep%d %s-%s, "
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

	data_len = min(data_len, usbfs_snoop_max);
	if (data && data_len > 0) {
		print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_NONE, 32, 1,
			data, data_len, 1);
	}
}

static void snoop_urb_data(struct urb *urb, unsigned len)
{
	int i, size;

	len = min(len, usbfs_snoop_max);
	if (!usbfs_snoop || len == 0)
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

static void cancel_bulk_urbs(struct usb_dev_state *ps, unsigned bulk_addr)
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
	list_for_each_entry_reverse(as, &ps->async_pending, asynclist) {
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
	struct usb_dev_state *ps = as->ps;
	struct pid *pid = NULL;
	const struct cred *cred = NULL;
	unsigned long flags;
	sigval_t addr;
	int signr, errno;

	spin_lock_irqsave(&ps->lock, flags);
	list_move_tail(&as->asynclist, &ps->async_completed);
	as->status = urb->status;
	signr = as->signr;
	if (signr) {
		errno = as->status;
		addr = as->userurb_sigval;
		pid = get_pid(as->pid);
		cred = get_cred(as->cred);
	}
	snoop(&urb->dev->dev, "urb complete\n");
	snoop_urb(urb->dev, as->userurb, urb->pipe, urb->actual_length,
			as->status, COMPLETE, NULL, 0);
	if (usb_urb_dir_in(urb))
		snoop_urb_data(urb, urb->actual_length);

	if (as->status < 0 && as->bulk_addr && as->status != -ECONNRESET &&
			as->status != -ENOENT)
		cancel_bulk_urbs(ps, as->bulk_addr);

	wake_up(&ps->wait);
	spin_unlock_irqrestore(&ps->lock, flags);

	if (signr) {
		kill_pid_usb_asyncio(signr, errno, addr, pid, cred);
		put_pid(pid);
		put_cred(cred);
	}
}

static void destroy_async(struct usb_dev_state *ps, struct list_head *list)
{
	struct urb *urb;
	struct async *as;
	unsigned long flags;

	spin_lock_irqsave(&ps->lock, flags);
	while (!list_empty(list)) {
		as = list_last_entry(list, struct async, asynclist);
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

static void destroy_async_on_interface(struct usb_dev_state *ps,
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

static void destroy_all_async(struct usb_dev_state *ps)
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
	struct usb_dev_state *ps = usb_get_intfdata(intf);
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

/* We don't care about suspend/resume of claimed interfaces */
static int driver_suspend(struct usb_interface *intf, pm_message_t msg)
{
	return 0;
}

static int driver_resume(struct usb_interface *intf)
{
	return 0;
}

/* The following routines apply to the entire device, not interfaces */
void usbfs_notify_suspend(struct usb_device *udev)
{
	/* We don't need to handle this */
}

void usbfs_notify_resume(struct usb_device *udev)
{
	struct usb_dev_state *ps;

	/* Protect against simultaneous remove or release */
	mutex_lock(&usbfs_mutex);
	list_for_each_entry(ps, &udev->filelist, list) {
		WRITE_ONCE(ps->not_yet_resumed, 0);
		wake_up_all(&ps->wait_for_resume);
	}
	mutex_unlock(&usbfs_mutex);
}

struct usb_driver usbfs_driver = {
	.name =		"usbfs",
	.probe =	driver_probe,
	.disconnect =	driver_disconnect,
	.suspend =	driver_suspend,
	.resume =	driver_resume,
	.supports_autosuspend = 1,
};

static int claimintf(struct usb_dev_state *ps, unsigned int ifnum)
{
	struct usb_device *dev = ps->dev;
	struct usb_interface *intf;
	int err;

	if (ifnum >= 8*sizeof(ps->ifclaimed))
		return -EINVAL;
	/* already claimed */
	if (test_bit(ifnum, &ps->ifclaimed))
		return 0;

	if (ps->privileges_dropped &&
			!test_bit(ifnum, &ps->interface_allowed_mask))
		return -EACCES;

	intf = usb_ifnum_to_if(dev, ifnum);
	if (!intf)
		err = -ENOENT;
	else {
		unsigned int old_suppress;

		/* suppress uevents while claiming interface */
		old_suppress = dev_get_uevent_suppress(&intf->dev);
		dev_set_uevent_suppress(&intf->dev, 1);
		err = usb_driver_claim_interface(&usbfs_driver, intf, ps);
		dev_set_uevent_suppress(&intf->dev, old_suppress);
	}
	if (err == 0)
		set_bit(ifnum, &ps->ifclaimed);
	return err;
}

static int releaseintf(struct usb_dev_state *ps, unsigned int ifnum)
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
		unsigned int old_suppress;

		/* suppress uevents while releasing interface */
		old_suppress = dev_get_uevent_suppress(&intf->dev);
		dev_set_uevent_suppress(&intf->dev, 1);
		usb_driver_release_interface(&usbfs_driver, intf);
		dev_set_uevent_suppress(&intf->dev, old_suppress);
		err = 0;
	}
	return err;
}

static int checkintf(struct usb_dev_state *ps, unsigned int ifnum)
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

static int check_ctrlrecip(struct usb_dev_state *ps, unsigned int requesttype,
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
	 * class specification, which we always want to allow as it is used
	 * to query things like ink level, etc.
	 */
	if (requesttype == 0xa1 && request == 0) {
		alt_setting = usb_find_alt_setting(ps->dev->actconfig,
						   index >> 8, index & 0xff);
		if (alt_setting
		 && alt_setting->desc.bInterfaceClass == USB_CLASS_PRINTER)
			return 0;
	}

	index &= 0xff;
	switch (requesttype & USB_RECIP_MASK) {
	case USB_RECIP_ENDPOINT:
		if ((index & ~USB_DIR_IN) == 0)
			return 0;
		ret = findintfep(ps->dev, index);
		if (ret < 0) {
			/*
			 * Some not fully compliant Win apps seem to get
			 * index wrong and have the endpoint number here
			 * rather than the endpoint address (with the
			 * correct direction). Win does let this through,
			 * so we'll not reject it here but leave it to
			 * the device to not break KVM. But we warn.
			 */
			ret = findintfep(ps->dev, index ^ 0x80);
			if (ret >= 0)
				dev_info(&ps->dev->dev,
					"%s: process %i (%s) requesting ep %02x but needs %02x\n",
					__func__, task_pid_nr(current),
					current->comm, index, index ^ 0x80);
		}
		if (ret >= 0)
			ret = checkintf(ps, ret);
		break;

	case USB_RECIP_INTERFACE:
		ret = checkintf(ps, index);
		break;
	}
	return ret;
}

static struct usb_host_endpoint *ep_to_host_endpoint(struct usb_device *dev,
						     unsigned char ep)
{
	if (ep & USB_ENDPOINT_DIR_MASK)
		return dev->ep_in[ep & USB_ENDPOINT_NUMBER_MASK];
	else
		return dev->ep_out[ep & USB_ENDPOINT_NUMBER_MASK];
}

static int parse_usbdevfs_streams(struct usb_dev_state *ps,
				  struct usbdevfs_streams __user *streams,
				  unsigned int *num_streams_ret,
				  unsigned int *num_eps_ret,
				  struct usb_host_endpoint ***eps_ret,
				  struct usb_interface **intf_ret)
{
	unsigned int i, num_streams, num_eps;
	struct usb_host_endpoint **eps;
	struct usb_interface *intf = NULL;
	unsigned char ep;
	int ifnum, ret;

	if (get_user(num_streams, &streams->num_streams) ||
	    get_user(num_eps, &streams->num_eps))
		return -EFAULT;

	if (num_eps < 1 || num_eps > USB_MAXENDPOINTS)
		return -EINVAL;

	/* The XHCI controller allows max 2 ^ 16 streams */
	if (num_streams_ret && (num_streams < 2 || num_streams > 65536))
		return -EINVAL;

	eps = kmalloc_array(num_eps, sizeof(*eps), GFP_KERNEL);
	if (!eps)
		return -ENOMEM;

	for (i = 0; i < num_eps; i++) {
		if (get_user(ep, &streams->eps[i])) {
			ret = -EFAULT;
			goto error;
		}
		eps[i] = ep_to_host_endpoint(ps->dev, ep);
		if (!eps[i]) {
			ret = -EINVAL;
			goto error;
		}

		/* usb_alloc/free_streams operate on an usb_interface */
		ifnum = findintfep(ps->dev, ep);
		if (ifnum < 0) {
			ret = ifnum;
			goto error;
		}

		if (i == 0) {
			ret = checkintf(ps, ifnum);
			if (ret < 0)
				goto error;
			intf = usb_ifnum_to_if(ps->dev, ifnum);
		} else {
			/* Verify all eps belong to the same interface */
			if (ifnum != intf->altsetting->desc.bInterfaceNumber) {
				ret = -EINVAL;
				goto error;
			}
		}
	}

	if (num_streams_ret)
		*num_streams_ret = num_streams;
	*num_eps_ret = num_eps;
	*eps_ret = eps;
	*intf_ret = intf;

	return 0;

error:
	kfree(eps);
	return ret;
}

static struct usb_device *usbdev_lookup_by_devt(dev_t devt)
{
	struct device *dev;

	dev = bus_find_device_by_devt(&usb_bus_type, devt);
	if (!dev)
		return NULL;
	return to_usb_device(dev);
}

/*
 * file operations
 */
static int usbdev_open(struct inode *inode, struct file *file)
{
	struct usb_device *dev = NULL;
	struct usb_dev_state *ps;
	int ret;

	ret = -ENOMEM;
	ps = kzalloc(sizeof(struct usb_dev_state), GFP_KERNEL);
	if (!ps)
		goto out_free_ps;

	ret = -ENODEV;

	/* usbdev device-node */
	if (imajor(inode) == USB_DEVICE_MAJOR)
		dev = usbdev_lookup_by_devt(inode->i_rdev);
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
	ps->interface_allowed_mask = 0xFFFFFFFF; /* 32 bits */
	spin_lock_init(&ps->lock);
	INIT_LIST_HEAD(&ps->list);
	INIT_LIST_HEAD(&ps->async_pending);
	INIT_LIST_HEAD(&ps->async_completed);
	INIT_LIST_HEAD(&ps->memory_list);
	init_waitqueue_head(&ps->wait);
	init_waitqueue_head(&ps->wait_for_resume);
	ps->disc_pid = get_pid(task_pid(current));
	ps->cred = get_current_cred();
	smp_wmb();

	/* Can't race with resume; the device is already active */
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
	struct usb_dev_state *ps = file->private_data;
	struct usb_device *dev = ps->dev;
	unsigned int ifnum;
	struct async *as;

	usb_lock_device(dev);
	usb_hub_release_all_ports(dev, ps);

	/* Protect against simultaneous resume */
	mutex_lock(&usbfs_mutex);
	list_del_init(&ps->list);
	mutex_unlock(&usbfs_mutex);

	for (ifnum = 0; ps->ifclaimed && ifnum < 8*sizeof(ps->ifclaimed);
			ifnum++) {
		if (test_bit(ifnum, &ps->ifclaimed))
			releaseintf(ps, ifnum);
	}
	destroy_all_async(ps);
	if (!ps->suspend_allowed)
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

static void usbfs_blocking_completion(struct urb *urb)
{
	complete((struct completion *) urb->context);
}

/*
 * Much like usb_start_wait_urb, but returns status separately from
 * actual_length and uses a killable wait.
 */
static int usbfs_start_wait_urb(struct urb *urb, int timeout,
		unsigned int *actlen)
{
	DECLARE_COMPLETION_ONSTACK(ctx);
	unsigned long expire;
	int rc;

	urb->context = &ctx;
	urb->complete = usbfs_blocking_completion;
	*actlen = 0;
	rc = usb_submit_urb(urb, GFP_KERNEL);
	if (unlikely(rc))
		return rc;

	expire = (timeout ? msecs_to_jiffies(timeout) : MAX_SCHEDULE_TIMEOUT);
	rc = wait_for_completion_killable_timeout(&ctx, expire);
	if (rc <= 0) {
		usb_kill_urb(urb);
		*actlen = urb->actual_length;
		if (urb->status != -ENOENT)
			;	/* Completed before it was killed */
		else if (rc < 0)
			return -EINTR;
		else
			return -ETIMEDOUT;
	}
	*actlen = urb->actual_length;
	return urb->status;
}

static int do_proc_control(struct usb_dev_state *ps,
		struct usbdevfs_ctrltransfer *ctrl)
{
	struct usb_device *dev = ps->dev;
	unsigned int tmo;
	unsigned char *tbuf;
	unsigned int wLength, actlen;
	int i, pipe, ret;
	struct urb *urb = NULL;
	struct usb_ctrlrequest *dr = NULL;

	ret = check_ctrlrecip(ps, ctrl->bRequestType, ctrl->bRequest,
			      ctrl->wIndex);
	if (ret)
		return ret;
	wLength = ctrl->wLength;	/* To suppress 64k PAGE_SIZE warning */
	if (wLength > PAGE_SIZE)
		return -EINVAL;
	ret = usbfs_increase_memory_usage(PAGE_SIZE + sizeof(struct urb) +
			sizeof(struct usb_ctrlrequest));
	if (ret)
		return ret;

	ret = -ENOMEM;
	tbuf = (unsigned char *)__get_free_page(GFP_KERNEL);
	if (!tbuf)
		goto done;
	urb = usb_alloc_urb(0, GFP_NOIO);
	if (!urb)
		goto done;
	dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_NOIO);
	if (!dr)
		goto done;

	dr->bRequestType = ctrl->bRequestType;
	dr->bRequest = ctrl->bRequest;
	dr->wValue = cpu_to_le16(ctrl->wValue);
	dr->wIndex = cpu_to_le16(ctrl->wIndex);
	dr->wLength = cpu_to_le16(ctrl->wLength);

	tmo = ctrl->timeout;
	snoop(&dev->dev, "control urb: bRequestType=%02x "
		"bRequest=%02x wValue=%04x "
		"wIndex=%04x wLength=%04x\n",
		ctrl->bRequestType, ctrl->bRequest, ctrl->wValue,
		ctrl->wIndex, ctrl->wLength);

	if ((ctrl->bRequestType & USB_DIR_IN) && wLength) {
		pipe = usb_rcvctrlpipe(dev, 0);
		usb_fill_control_urb(urb, dev, pipe, (unsigned char *) dr, tbuf,
				wLength, NULL, NULL);
		snoop_urb(dev, NULL, pipe, wLength, tmo, SUBMIT, NULL, 0);

		usb_unlock_device(dev);
		i = usbfs_start_wait_urb(urb, tmo, &actlen);

		/* Linger a bit, prior to the next control message. */
		if (dev->quirks & USB_QUIRK_DELAY_CTRL_MSG)
			msleep(200);
		usb_lock_device(dev);
		snoop_urb(dev, NULL, pipe, actlen, i, COMPLETE, tbuf, actlen);
		if (!i && actlen) {
			if (copy_to_user(ctrl->data, tbuf, actlen)) {
				ret = -EFAULT;
				goto done;
			}
		}
	} else {
		if (wLength) {
			if (copy_from_user(tbuf, ctrl->data, wLength)) {
				ret = -EFAULT;
				goto done;
			}
		}
		pipe = usb_sndctrlpipe(dev, 0);
		usb_fill_control_urb(urb, dev, pipe, (unsigned char *) dr, tbuf,
				wLength, NULL, NULL);
		snoop_urb(dev, NULL, pipe, wLength, tmo, SUBMIT, tbuf, wLength);

		usb_unlock_device(dev);
		i = usbfs_start_wait_urb(urb, tmo, &actlen);

		/* Linger a bit, prior to the next control message. */
		if (dev->quirks & USB_QUIRK_DELAY_CTRL_MSG)
			msleep(200);
		usb_lock_device(dev);
		snoop_urb(dev, NULL, pipe, actlen, i, COMPLETE, NULL, 0);
	}
	if (i < 0 && i != -EPIPE) {
		dev_printk(KERN_DEBUG, &dev->dev, "usbfs: USBDEVFS_CONTROL "
			   "failed cmd %s rqt %u rq %u len %u ret %d\n",
			   current->comm, ctrl->bRequestType, ctrl->bRequest,
			   ctrl->wLength, i);
	}
	ret = (i < 0 ? i : actlen);

 done:
	kfree(dr);
	usb_free_urb(urb);
	free_page((unsigned long) tbuf);
	usbfs_decrease_memory_usage(PAGE_SIZE + sizeof(struct urb) +
			sizeof(struct usb_ctrlrequest));
	return ret;
}

static int proc_control(struct usb_dev_state *ps, void __user *arg)
{
	struct usbdevfs_ctrltransfer ctrl;

	if (copy_from_user(&ctrl, arg, sizeof(ctrl)))
		return -EFAULT;
	return do_proc_control(ps, &ctrl);
}

static int do_proc_bulk(struct usb_dev_state *ps,
		struct usbdevfs_bulktransfer *bulk)
{
	struct usb_device *dev = ps->dev;
	unsigned int tmo, len1, len2, pipe;
	unsigned char *tbuf;
	int i, ret;
	struct urb *urb = NULL;
	struct usb_host_endpoint *ep;

	ret = findintfep(ps->dev, bulk->ep);
	if (ret < 0)
		return ret;
	ret = checkintf(ps, ret);
	if (ret)
		return ret;

	len1 = bulk->len;
	if (len1 < 0 || len1 >= (INT_MAX - sizeof(struct urb)))
		return -EINVAL;

	if (bulk->ep & USB_DIR_IN)
		pipe = usb_rcvbulkpipe(dev, bulk->ep & 0x7f);
	else
		pipe = usb_sndbulkpipe(dev, bulk->ep & 0x7f);
	ep = usb_pipe_endpoint(dev, pipe);
	if (!ep || !usb_endpoint_maxp(&ep->desc))
		return -EINVAL;
	ret = usbfs_increase_memory_usage(len1 + sizeof(struct urb));
	if (ret)
		return ret;

	/*
	 * len1 can be almost arbitrarily large.  Don't WARN if it's
	 * too big, just fail the request.
	 */
	ret = -ENOMEM;
	tbuf = kmalloc(len1, GFP_KERNEL | __GFP_NOWARN);
	if (!tbuf)
		goto done;
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		goto done;

	if ((ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_INT) {
		pipe = (pipe & ~(3 << 30)) | (PIPE_INTERRUPT << 30);
		usb_fill_int_urb(urb, dev, pipe, tbuf, len1,
				NULL, NULL, ep->desc.bInterval);
	} else {
		usb_fill_bulk_urb(urb, dev, pipe, tbuf, len1, NULL, NULL);
	}

	tmo = bulk->timeout;
	if (bulk->ep & 0x80) {
		snoop_urb(dev, NULL, pipe, len1, tmo, SUBMIT, NULL, 0);

		usb_unlock_device(dev);
		i = usbfs_start_wait_urb(urb, tmo, &len2);
		usb_lock_device(dev);
		snoop_urb(dev, NULL, pipe, len2, i, COMPLETE, tbuf, len2);

		if (!i && len2) {
			if (copy_to_user(bulk->data, tbuf, len2)) {
				ret = -EFAULT;
				goto done;
			}
		}
	} else {
		if (len1) {
			if (copy_from_user(tbuf, bulk->data, len1)) {
				ret = -EFAULT;
				goto done;
			}
		}
		snoop_urb(dev, NULL, pipe, len1, tmo, SUBMIT, tbuf, len1);

		usb_unlock_device(dev);
		i = usbfs_start_wait_urb(urb, tmo, &len2);
		usb_lock_device(dev);
		snoop_urb(dev, NULL, pipe, len2, i, COMPLETE, NULL, 0);
	}
	ret = (i < 0 ? i : len2);
 done:
	usb_free_urb(urb);
	kfree(tbuf);
	usbfs_decrease_memory_usage(len1 + sizeof(struct urb));
	return ret;
}

static int proc_bulk(struct usb_dev_state *ps, void __user *arg)
{
	struct usbdevfs_bulktransfer bulk;

	if (copy_from_user(&bulk, arg, sizeof(bulk)))
		return -EFAULT;
	return do_proc_bulk(ps, &bulk);
}

static void check_reset_of_active_ep(struct usb_device *udev,
		unsigned int epnum, char *ioctl_name)
{
	struct usb_host_endpoint **eps;
	struct usb_host_endpoint *ep;

	eps = (epnum & USB_DIR_IN) ? udev->ep_in : udev->ep_out;
	ep = eps[epnum & 0x0f];
	if (ep && !list_empty(&ep->urb_list))
		dev_warn(&udev->dev, "Process %d (%s) called USBDEVFS_%s for active endpoint 0x%02x\n",
				task_pid_nr(current), current->comm,
				ioctl_name, epnum);
}

static int proc_resetep(struct usb_dev_state *ps, void __user *arg)
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
	check_reset_of_active_ep(ps->dev, ep, "RESETEP");
	usb_reset_endpoint(ps->dev, ep);
	return 0;
}

static int proc_clearhalt(struct usb_dev_state *ps, void __user *arg)
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
	check_reset_of_active_ep(ps->dev, ep, "CLEAR_HALT");
	if (ep & USB_DIR_IN)
		pipe = usb_rcvbulkpipe(ps->dev, ep & 0x7f);
	else
		pipe = usb_sndbulkpipe(ps->dev, ep & 0x7f);

	return usb_clear_halt(ps->dev, pipe);
}

static int proc_getdriver(struct usb_dev_state *ps, void __user *arg)
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
		strscpy(gd.driver, intf->dev.driver->name,
				sizeof(gd.driver));
		ret = (copy_to_user(arg, &gd, sizeof(gd)) ? -EFAULT : 0);
	}
	return ret;
}

static int proc_connectinfo(struct usb_dev_state *ps, void __user *arg)
{
	struct usbdevfs_connectinfo ci;

	memset(&ci, 0, sizeof(ci));
	ci.devnum = ps->dev->devnum;
	ci.slow = ps->dev->speed == USB_SPEED_LOW;

	if (copy_to_user(arg, &ci, sizeof(ci)))
		return -EFAULT;
	return 0;
}

static int proc_conninfo_ex(struct usb_dev_state *ps,
			    void __user *arg, size_t size)
{
	struct usbdevfs_conninfo_ex ci;
	struct usb_device *udev = ps->dev;

	if (size < sizeof(ci.size))
		return -EINVAL;

	memset(&ci, 0, sizeof(ci));
	ci.size = sizeof(ci);
	ci.busnum = udev->bus->busnum;
	ci.devnum = udev->devnum;
	ci.speed = udev->speed;

	while (udev && udev->portnum != 0) {
		if (++ci.num_ports <= ARRAY_SIZE(ci.ports))
			ci.ports[ARRAY_SIZE(ci.ports) - ci.num_ports] =
					udev->portnum;
		udev = udev->parent;
	}

	if (ci.num_ports < ARRAY_SIZE(ci.ports))
		memmove(&ci.ports[0],
			&ci.ports[ARRAY_SIZE(ci.ports) - ci.num_ports],
			ci.num_ports);

	if (copy_to_user(arg, &ci, min(sizeof(ci), size)))
		return -EFAULT;

	return 0;
}

static int proc_resetdevice(struct usb_dev_state *ps)
{
	struct usb_host_config *actconfig = ps->dev->actconfig;
	struct usb_interface *interface;
	int i, number;

	/* Don't allow a device reset if the process has dropped the
	 * privilege to do such things and any of the interfaces are
	 * currently claimed.
	 */
	if (ps->privileges_dropped && actconfig) {
		for (i = 0; i < actconfig->desc.bNumInterfaces; ++i) {
			interface = actconfig->interface[i];
			number = interface->cur_altsetting->desc.bInterfaceNumber;
			if (usb_interface_claimed(interface) &&
					!test_bit(number, &ps->ifclaimed)) {
				dev_warn(&ps->dev->dev,
					"usbfs: interface %d claimed by %s while '%s' resets device\n",
					number,	interface->dev.driver->name, current->comm);
				return -EACCES;
			}
		}
	}

	return usb_reset_device(ps->dev);
}

static int proc_setintf(struct usb_dev_state *ps, void __user *arg)
{
	struct usbdevfs_setinterface setintf;
	int ret;

	if (copy_from_user(&setintf, arg, sizeof(setintf)))
		return -EFAULT;
	ret = checkintf(ps, setintf.interface);
	if (ret)
		return ret;

	destroy_async_on_interface(ps, setintf.interface);

	return usb_set_interface(ps->dev, setintf.interface,
			setintf.altsetting);
}

static int proc_setconfig(struct usb_dev_state *ps, void __user *arg)
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

static struct usb_memory *
find_memory_area(struct usb_dev_state *ps, const struct usbdevfs_urb *uurb)
{
	struct usb_memory *usbm = NULL, *iter;
	unsigned long flags;
	unsigned long uurb_start = (unsigned long)uurb->buffer;

	spin_lock_irqsave(&ps->lock, flags);
	list_for_each_entry(iter, &ps->memory_list, memlist) {
		if (uurb_start >= iter->vm_start &&
				uurb_start < iter->vm_start + iter->size) {
			if (uurb->buffer_length > iter->vm_start + iter->size -
					uurb_start) {
				usbm = ERR_PTR(-EINVAL);
			} else {
				usbm = iter;
				usbm->urb_use_count++;
			}
			break;
		}
	}
	spin_unlock_irqrestore(&ps->lock, flags);
	return usbm;
}

static int proc_do_submiturb(struct usb_dev_state *ps, struct usbdevfs_urb *uurb,
			struct usbdevfs_iso_packet_desc __user *iso_frame_desc,
			void __user *arg, sigval_t userurb_sigval)
{
	struct usbdevfs_iso_packet_desc *isopkt = NULL;
	struct usb_host_endpoint *ep;
	struct async *as = NULL;
	struct usb_ctrlrequest *dr = NULL;
	unsigned int u, totlen, isofrmlen;
	int i, ret, num_sgs = 0, ifnum = -1;
	int number_of_packets = 0;
	unsigned int stream_id = 0;
	void *buf;
	bool is_in;
	bool allow_short = false;
	bool allow_zero = false;
	unsigned long mask =	USBDEVFS_URB_SHORT_NOT_OK |
				USBDEVFS_URB_BULK_CONTINUATION |
				USBDEVFS_URB_NO_FSBR |
				USBDEVFS_URB_ZERO_PACKET |
				USBDEVFS_URB_NO_INTERRUPT;
	/* USBDEVFS_URB_ISO_ASAP is a special case */
	if (uurb->type == USBDEVFS_URB_TYPE_ISO)
		mask |= USBDEVFS_URB_ISO_ASAP;

	if (uurb->flags & ~mask)
			return -EINVAL;

	if ((unsigned int)uurb->buffer_length >= USBFS_XFER_MAX)
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
	ep = ep_to_host_endpoint(ps->dev, uurb->endpoint);
	if (!ep)
		return -ENOENT;
	is_in = (uurb->endpoint & USB_ENDPOINT_DIR_MASK) != 0;

	u = 0;
	switch (uurb->type) {
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
		if (uurb->buffer_length < (le16_to_cpu(dr->wLength) + 8)) {
			ret = -EINVAL;
			goto error;
		}
		ret = check_ctrlrecip(ps, dr->bRequestType, dr->bRequest,
				      le16_to_cpu(dr->wIndex));
		if (ret)
			goto error;
		uurb->buffer_length = le16_to_cpu(dr->wLength);
		uurb->buffer += 8;
		if ((dr->bRequestType & USB_DIR_IN) && uurb->buffer_length) {
			is_in = true;
			uurb->endpoint |= USB_DIR_IN;
		} else {
			is_in = false;
			uurb->endpoint &= ~USB_DIR_IN;
		}
		if (is_in)
			allow_short = true;
		snoop(&ps->dev->dev, "control urb: bRequestType=%02x "
			"bRequest=%02x wValue=%04x "
			"wIndex=%04x wLength=%04x\n",
			dr->bRequestType, dr->bRequest,
			__le16_to_cpu(dr->wValue),
			__le16_to_cpu(dr->wIndex),
			__le16_to_cpu(dr->wLength));
		u = sizeof(struct usb_ctrlrequest);
		break;

	case USBDEVFS_URB_TYPE_BULK:
		if (!is_in)
			allow_zero = true;
		else
			allow_short = true;
		switch (usb_endpoint_type(&ep->desc)) {
		case USB_ENDPOINT_XFER_CONTROL:
		case USB_ENDPOINT_XFER_ISOC:
			return -EINVAL;
		case USB_ENDPOINT_XFER_INT:
			/* allow single-shot interrupt transfers */
			uurb->type = USBDEVFS_URB_TYPE_INTERRUPT;
			goto interrupt_urb;
		}
		num_sgs = DIV_ROUND_UP(uurb->buffer_length, USB_SG_SIZE);
		if (num_sgs == 1 || num_sgs > ps->dev->bus->sg_tablesize)
			num_sgs = 0;
		if (ep->streams)
			stream_id = uurb->stream_id;
		break;

	case USBDEVFS_URB_TYPE_INTERRUPT:
		if (!usb_endpoint_xfer_int(&ep->desc))
			return -EINVAL;
 interrupt_urb:
		if (!is_in)
			allow_zero = true;
		else
			allow_short = true;
		break;

	case USBDEVFS_URB_TYPE_ISO:
		/* arbitrary limit */
		if (uurb->number_of_packets < 1 ||
		    uurb->number_of_packets > 128)
			return -EINVAL;
		if (!usb_endpoint_xfer_isoc(&ep->desc))
			return -EINVAL;
		number_of_packets = uurb->number_of_packets;
		isofrmlen = sizeof(struct usbdevfs_iso_packet_desc) *
				   number_of_packets;
		isopkt = memdup_user(iso_frame_desc, isofrmlen);
		if (IS_ERR(isopkt)) {
			ret = PTR_ERR(isopkt);
			isopkt = NULL;
			goto error;
		}
		for (totlen = u = 0; u < number_of_packets; u++) {
			/*
			 * arbitrary limit need for USB 3.1 Gen2
			 * sizemax: 96 DPs at SSP, 96 * 1024 = 98304
			 */
			if (isopkt[u].length > 98304) {
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

	if (uurb->buffer_length > 0 &&
			!access_ok(uurb->buffer, uurb->buffer_length)) {
		ret = -EFAULT;
		goto error;
	}
	as = alloc_async(number_of_packets);
	if (!as) {
		ret = -ENOMEM;
		goto error;
	}

	as->usbm = find_memory_area(ps, uurb);
	if (IS_ERR(as->usbm)) {
		ret = PTR_ERR(as->usbm);
		as->usbm = NULL;
		goto error;
	}

	/* do not use SG buffers when memory mapped segments
	 * are in use
	 */
	if (as->usbm)
		num_sgs = 0;

	u += sizeof(struct async) + sizeof(struct urb) +
	     (as->usbm ? 0 : uurb->buffer_length) +
	     num_sgs * sizeof(struct scatterlist);
	ret = usbfs_increase_memory_usage(u);
	if (ret)
		goto error;
	as->mem_usage = u;

	if (num_sgs) {
		as->urb->sg = kmalloc_array(num_sgs,
					    sizeof(struct scatterlist),
					    GFP_KERNEL | __GFP_NOWARN);
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
		if (as->usbm) {
			unsigned long uurb_start = (unsigned long)uurb->buffer;

			as->urb->transfer_buffer = as->usbm->mem +
					(uurb_start - as->usbm->vm_start);
		} else {
			as->urb->transfer_buffer = kmalloc(uurb->buffer_length,
					GFP_KERNEL | __GFP_NOWARN);
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
				 * discontiguous if some of the packets are
				 * short. Clear the buffer so that the gaps
				 * don't leak kernel data to userspace.
				 */
				memset(as->urb->transfer_buffer, 0,
						uurb->buffer_length);
			}
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
	if (allow_short && uurb->flags & USBDEVFS_URB_SHORT_NOT_OK)
		u |= URB_SHORT_NOT_OK;
	if (allow_zero && uurb->flags & USBDEVFS_URB_ZERO_PACKET)
		u |= URB_ZERO_PACKET;
	if (uurb->flags & USBDEVFS_URB_NO_INTERRUPT)
		u |= URB_NO_INTERRUPT;
	as->urb->transfer_flags = u;

	if (!allow_short && uurb->flags & USBDEVFS_URB_SHORT_NOT_OK)
		dev_warn(&ps->dev->dev, "Requested nonsensical USBDEVFS_URB_SHORT_NOT_OK.\n");
	if (!allow_zero && uurb->flags & USBDEVFS_URB_ZERO_PACKET)
		dev_warn(&ps->dev->dev, "Requested nonsensical USBDEVFS_URB_ZERO_PACKET.\n");

	as->urb->transfer_buffer_length = uurb->buffer_length;
	as->urb->setup_packet = (unsigned char *)dr;
	dr = NULL;
	as->urb->start_frame = uurb->start_frame;
	as->urb->number_of_packets = number_of_packets;
	as->urb->stream_id = stream_id;

	if (ep->desc.bInterval) {
		if (uurb->type == USBDEVFS_URB_TYPE_ISO ||
				ps->dev->speed == USB_SPEED_HIGH ||
				ps->dev->speed >= USB_SPEED_SUPER)
			as->urb->interval = 1 <<
					min(15, ep->desc.bInterval - 1);
		else
			as->urb->interval = ep->desc.bInterval;
	}

	as->urb->context = as;
	as->urb->complete = async_completed;
	for (totlen = u = 0; u < number_of_packets; u++) {
		as->urb->iso_frame_desc[u].offset = totlen;
		as->urb->iso_frame_desc[u].length = isopkt[u].length;
		totlen += isopkt[u].length;
	}
	kfree(isopkt);
	isopkt = NULL;
	as->ps = ps;
	as->userurb = arg;
	as->userurb_sigval = userurb_sigval;
	if (as->usbm) {
		unsigned long uurb_start = (unsigned long)uurb->buffer;

		as->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		as->urb->transfer_dma = as->usbm->dma_handle +
				(uurb_start - as->usbm->vm_start);
	} else if (is_in && uurb->buffer_length > 0)
		as->userbuffer = uurb->buffer;
	as->signr = uurb->signr;
	as->ifnum = ifnum;
	as->pid = get_pid(task_pid(current));
	as->cred = get_current_cred();
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

static int proc_submiturb(struct usb_dev_state *ps, void __user *arg)
{
	struct usbdevfs_urb uurb;
	sigval_t userurb_sigval;

	if (copy_from_user(&uurb, arg, sizeof(uurb)))
		return -EFAULT;

	memset(&userurb_sigval, 0, sizeof(userurb_sigval));
	userurb_sigval.sival_ptr = arg;

	return proc_do_submiturb(ps, &uurb,
			(((struct usbdevfs_urb __user *)arg)->iso_frame_desc),
			arg, userurb_sigval);
}

static int proc_unlinkurb(struct usb_dev_state *ps, void __user *arg)
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

static void compute_isochronous_actual_length(struct urb *urb)
{
	unsigned int i;

	if (urb->number_of_packets > 0) {
		urb->actual_length = 0;
		for (i = 0; i < urb->number_of_packets; i++)
			urb->actual_length +=
					urb->iso_frame_desc[i].actual_length;
	}
}

static int processcompl(struct async *as, void __user * __user *arg)
{
	struct urb *urb = as->urb;
	struct usbdevfs_urb __user *userurb = as->userurb;
	void __user *addr = as->userurb;
	unsigned int i;

	compute_isochronous_actual_length(urb);
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

static struct async *reap_as(struct usb_dev_state *ps)
{
	DECLARE_WAITQUEUE(wait, current);
	struct async *as = NULL;
	struct usb_device *dev = ps->dev;

	add_wait_queue(&ps->wait, &wait);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		as = async_getcompleted(ps);
		if (as || !connected(ps))
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

static int proc_reapurb(struct usb_dev_state *ps, void __user *arg)
{
	struct async *as = reap_as(ps);

	if (as) {
		int retval;

		snoop(&ps->dev->dev, "reap %px\n", as->userurb);
		retval = processcompl(as, (void __user * __user *)arg);
		free_async(as);
		return retval;
	}
	if (signal_pending(current))
		return -EINTR;
	return -ENODEV;
}

static int proc_reapurbnonblock(struct usb_dev_state *ps, void __user *arg)
{
	int retval;
	struct async *as;

	as = async_getcompleted(ps);
	if (as) {
		snoop(&ps->dev->dev, "reap %px\n", as->userurb);
		retval = processcompl(as, (void __user * __user *)arg);
		free_async(as);
	} else {
		retval = (connected(ps) ? -EAGAIN : -ENODEV);
	}
	return retval;
}

#ifdef CONFIG_COMPAT
static int proc_control_compat(struct usb_dev_state *ps,
				struct usbdevfs_ctrltransfer32 __user *p32)
{
	struct usbdevfs_ctrltransfer ctrl;
	u32 udata;

	if (copy_from_user(&ctrl, p32, sizeof(*p32) - sizeof(compat_caddr_t)) ||
	    get_user(udata, &p32->data))
		return -EFAULT;
	ctrl.data = compat_ptr(udata);
	return do_proc_control(ps, &ctrl);
}

static int proc_bulk_compat(struct usb_dev_state *ps,
			struct usbdevfs_bulktransfer32 __user *p32)
{
	struct usbdevfs_bulktransfer bulk;
	compat_caddr_t addr;

	if (get_user(bulk.ep, &p32->ep) ||
	    get_user(bulk.len, &p32->len) ||
	    get_user(bulk.timeout, &p32->timeout) ||
	    get_user(addr, &p32->data))
		return -EFAULT;
	bulk.data = compat_ptr(addr);
	return do_proc_bulk(ps, &bulk);
}

static int proc_disconnectsignal_compat(struct usb_dev_state *ps, void __user *arg)
{
	struct usbdevfs_disconnectsignal32 ds;

	if (copy_from_user(&ds, arg, sizeof(ds)))
		return -EFAULT;
	ps->discsignr = ds.signr;
	ps->disccontext.sival_int = ds.context;
	return 0;
}

static int get_urb32(struct usbdevfs_urb *kurb,
		     struct usbdevfs_urb32 __user *uurb)
{
	struct usbdevfs_urb32 urb32;
	if (copy_from_user(&urb32, uurb, sizeof(*uurb)))
		return -EFAULT;
	kurb->type = urb32.type;
	kurb->endpoint = urb32.endpoint;
	kurb->status = urb32.status;
	kurb->flags = urb32.flags;
	kurb->buffer = compat_ptr(urb32.buffer);
	kurb->buffer_length = urb32.buffer_length;
	kurb->actual_length = urb32.actual_length;
	kurb->start_frame = urb32.start_frame;
	kurb->number_of_packets = urb32.number_of_packets;
	kurb->error_count = urb32.error_count;
	kurb->signr = urb32.signr;
	kurb->usercontext = compat_ptr(urb32.usercontext);
	return 0;
}

static int proc_submiturb_compat(struct usb_dev_state *ps, void __user *arg)
{
	struct usbdevfs_urb uurb;
	sigval_t userurb_sigval;

	if (get_urb32(&uurb, (struct usbdevfs_urb32 __user *)arg))
		return -EFAULT;

	memset(&userurb_sigval, 0, sizeof(userurb_sigval));
	userurb_sigval.sival_int = ptr_to_compat(arg);

	return proc_do_submiturb(ps, &uurb,
			((struct usbdevfs_urb32 __user *)arg)->iso_frame_desc,
			arg, userurb_sigval);
}

static int processcompl_compat(struct async *as, void __user * __user *arg)
{
	struct urb *urb = as->urb;
	struct usbdevfs_urb32 __user *userurb = as->userurb;
	void __user *addr = as->userurb;
	unsigned int i;

	compute_isochronous_actual_length(urb);
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

static int proc_reapurb_compat(struct usb_dev_state *ps, void __user *arg)
{
	struct async *as = reap_as(ps);

	if (as) {
		int retval;

		snoop(&ps->dev->dev, "reap %px\n", as->userurb);
		retval = processcompl_compat(as, (void __user * __user *)arg);
		free_async(as);
		return retval;
	}
	if (signal_pending(current))
		return -EINTR;
	return -ENODEV;
}

static int proc_reapurbnonblock_compat(struct usb_dev_state *ps, void __user *arg)
{
	int retval;
	struct async *as;

	as = async_getcompleted(ps);
	if (as) {
		snoop(&ps->dev->dev, "reap %px\n", as->userurb);
		retval = processcompl_compat(as, (void __user * __user *)arg);
		free_async(as);
	} else {
		retval = (connected(ps) ? -EAGAIN : -ENODEV);
	}
	return retval;
}


#endif

static int proc_disconnectsignal(struct usb_dev_state *ps, void __user *arg)
{
	struct usbdevfs_disconnectsignal ds;

	if (copy_from_user(&ds, arg, sizeof(ds)))
		return -EFAULT;
	ps->discsignr = ds.signr;
	ps->disccontext.sival_ptr = ds.context;
	return 0;
}

static int proc_claiminterface(struct usb_dev_state *ps, void __user *arg)
{
	unsigned int ifnum;

	if (get_user(ifnum, (unsigned int __user *)arg))
		return -EFAULT;
	return claimintf(ps, ifnum);
}

static int proc_releaseinterface(struct usb_dev_state *ps, void __user *arg)
{
	unsigned int ifnum;
	int ret;

	if (get_user(ifnum, (unsigned int __user *)arg))
		return -EFAULT;
	ret = releaseintf(ps, ifnum);
	if (ret < 0)
		return ret;
	destroy_async_on_interface(ps, ifnum);
	return 0;
}

static int proc_ioctl(struct usb_dev_state *ps, struct usbdevfs_ioctl *ctl)
{
	int			size;
	void			*buf = NULL;
	int			retval = 0;
	struct usb_interface    *intf = NULL;
	struct usb_driver       *driver = NULL;

	if (ps->privileges_dropped)
		return -EACCES;

	if (!connected(ps))
		return -ENODEV;

	/* alloc buffer */
	size = _IOC_SIZE(ctl->ioctl_code);
	if (size > 0) {
		buf = kmalloc(size, GFP_KERNEL);
		if (buf == NULL)
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

static int proc_ioctl_default(struct usb_dev_state *ps, void __user *arg)
{
	struct usbdevfs_ioctl	ctrl;

	if (copy_from_user(&ctrl, arg, sizeof(ctrl)))
		return -EFAULT;
	return proc_ioctl(ps, &ctrl);
}

#ifdef CONFIG_COMPAT
static int proc_ioctl_compat(struct usb_dev_state *ps, compat_uptr_t arg)
{
	struct usbdevfs_ioctl32 ioc32;
	struct usbdevfs_ioctl ctrl;

	if (copy_from_user(&ioc32, compat_ptr(arg), sizeof(ioc32)))
		return -EFAULT;
	ctrl.ifno = ioc32.ifno;
	ctrl.ioctl_code = ioc32.ioctl_code;
	ctrl.data = compat_ptr(ioc32.data);
	return proc_ioctl(ps, &ctrl);
}
#endif

static int proc_claim_port(struct usb_dev_state *ps, void __user *arg)
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

static int proc_release_port(struct usb_dev_state *ps, void __user *arg)
{
	unsigned portnum;

	if (get_user(portnum, (unsigned __user *) arg))
		return -EFAULT;
	return usb_hub_release_port(ps->dev, portnum, ps);
}

static int proc_get_capabilities(struct usb_dev_state *ps, void __user *arg)
{
	__u32 caps;

	caps = USBDEVFS_CAP_ZERO_PACKET | USBDEVFS_CAP_NO_PACKET_SIZE_LIM |
			USBDEVFS_CAP_REAP_AFTER_DISCONNECT | USBDEVFS_CAP_MMAP |
			USBDEVFS_CAP_DROP_PRIVILEGES |
			USBDEVFS_CAP_CONNINFO_EX | MAYBE_CAP_SUSPEND;
	if (!ps->dev->bus->no_stop_on_short)
		caps |= USBDEVFS_CAP_BULK_CONTINUATION;
	if (ps->dev->bus->sg_tablesize)
		caps |= USBDEVFS_CAP_BULK_SCATTER_GATHER;

	if (put_user(caps, (__u32 __user *)arg))
		return -EFAULT;

	return 0;
}

static int proc_disconnect_claim(struct usb_dev_state *ps, void __user *arg)
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

		if (ps->privileges_dropped)
			return -EACCES;

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

static int proc_alloc_streams(struct usb_dev_state *ps, void __user *arg)
{
	unsigned num_streams, num_eps;
	struct usb_host_endpoint **eps;
	struct usb_interface *intf;
	int r;

	r = parse_usbdevfs_streams(ps, arg, &num_streams, &num_eps,
				   &eps, &intf);
	if (r)
		return r;

	destroy_async_on_interface(ps,
				   intf->altsetting[0].desc.bInterfaceNumber);

	r = usb_alloc_streams(intf, eps, num_eps, num_streams, GFP_KERNEL);
	kfree(eps);
	return r;
}

static int proc_free_streams(struct usb_dev_state *ps, void __user *arg)
{
	unsigned num_eps;
	struct usb_host_endpoint **eps;
	struct usb_interface *intf;
	int r;

	r = parse_usbdevfs_streams(ps, arg, NULL, &num_eps, &eps, &intf);
	if (r)
		return r;

	destroy_async_on_interface(ps,
				   intf->altsetting[0].desc.bInterfaceNumber);

	r = usb_free_streams(intf, eps, num_eps, GFP_KERNEL);
	kfree(eps);
	return r;
}

static int proc_drop_privileges(struct usb_dev_state *ps, void __user *arg)
{
	u32 data;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;

	/* This is a one way operation. Once privileges are
	 * dropped, you cannot regain them. You may however reissue
	 * this ioctl to shrink the allowed interfaces mask.
	 */
	ps->interface_allowed_mask &= data;
	ps->privileges_dropped = true;

	return 0;
}

static int proc_forbid_suspend(struct usb_dev_state *ps)
{
	int ret = 0;

	if (ps->suspend_allowed) {
		ret = usb_autoresume_device(ps->dev);
		if (ret == 0)
			ps->suspend_allowed = false;
		else if (ret != -ENODEV)
			ret = -EIO;
	}
	return ret;
}

static int proc_allow_suspend(struct usb_dev_state *ps)
{
	if (!connected(ps))
		return -ENODEV;

	WRITE_ONCE(ps->not_yet_resumed, 1);
	if (!ps->suspend_allowed) {
		usb_autosuspend_device(ps->dev);
		ps->suspend_allowed = true;
	}
	return 0;
}

static int proc_wait_for_resume(struct usb_dev_state *ps)
{
	int ret;

	usb_unlock_device(ps->dev);
	ret = wait_event_interruptible(ps->wait_for_resume,
			READ_ONCE(ps->not_yet_resumed) == 0);
	usb_lock_device(ps->dev);

	if (ret != 0)
		return -EINTR;
	return proc_forbid_suspend(ps);
}

/*
 * NOTE:  All requests here that have interface numbers as parameters
 * are assuming that somehow the configuration has been prevented from
 * changing.  But there's no mechanism to ensure that...
 */
static long usbdev_do_ioctl(struct file *file, unsigned int cmd,
				void __user *p)
{
	struct usb_dev_state *ps = file->private_data;
	struct inode *inode = file_inode(file);
	struct usb_device *dev = ps->dev;
	int ret = -ENOTTY;

	if (!(file->f_mode & FMODE_WRITE))
		return -EPERM;

	usb_lock_device(dev);

	/* Reap operations are allowed even after disconnection */
	switch (cmd) {
	case USBDEVFS_REAPURB:
		snoop(&dev->dev, "%s: REAPURB\n", __func__);
		ret = proc_reapurb(ps, p);
		goto done;

	case USBDEVFS_REAPURBNDELAY:
		snoop(&dev->dev, "%s: REAPURBNDELAY\n", __func__);
		ret = proc_reapurbnonblock(ps, p);
		goto done;

#ifdef CONFIG_COMPAT
	case USBDEVFS_REAPURB32:
		snoop(&dev->dev, "%s: REAPURB32\n", __func__);
		ret = proc_reapurb_compat(ps, p);
		goto done;

	case USBDEVFS_REAPURBNDELAY32:
		snoop(&dev->dev, "%s: REAPURBNDELAY32\n", __func__);
		ret = proc_reapurbnonblock_compat(ps, p);
		goto done;
#endif
	}

	if (!connected(ps)) {
		usb_unlock_device(dev);
		return -ENODEV;
	}

	switch (cmd) {
	case USBDEVFS_CONTROL:
		snoop(&dev->dev, "%s: CONTROL\n", __func__);
		ret = proc_control(ps, p);
		if (ret >= 0)
			inode->i_mtime = current_time(inode);
		break;

	case USBDEVFS_BULK:
		snoop(&dev->dev, "%s: BULK\n", __func__);
		ret = proc_bulk(ps, p);
		if (ret >= 0)
			inode->i_mtime = current_time(inode);
		break;

	case USBDEVFS_RESETEP:
		snoop(&dev->dev, "%s: RESETEP\n", __func__);
		ret = proc_resetep(ps, p);
		if (ret >= 0)
			inode->i_mtime = current_time(inode);
		break;

	case USBDEVFS_RESET:
		snoop(&dev->dev, "%s: RESET\n", __func__);
		ret = proc_resetdevice(ps);
		break;

	case USBDEVFS_CLEAR_HALT:
		snoop(&dev->dev, "%s: CLEAR_HALT\n", __func__);
		ret = proc_clearhalt(ps, p);
		if (ret >= 0)
			inode->i_mtime = current_time(inode);
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
			inode->i_mtime = current_time(inode);
		break;

#ifdef CONFIG_COMPAT
	case USBDEVFS_CONTROL32:
		snoop(&dev->dev, "%s: CONTROL32\n", __func__);
		ret = proc_control_compat(ps, p);
		if (ret >= 0)
			inode->i_mtime = current_time(inode);
		break;

	case USBDEVFS_BULK32:
		snoop(&dev->dev, "%s: BULK32\n", __func__);
		ret = proc_bulk_compat(ps, p);
		if (ret >= 0)
			inode->i_mtime = current_time(inode);
		break;

	case USBDEVFS_DISCSIGNAL32:
		snoop(&dev->dev, "%s: DISCSIGNAL32\n", __func__);
		ret = proc_disconnectsignal_compat(ps, p);
		break;

	case USBDEVFS_SUBMITURB32:
		snoop(&dev->dev, "%s: SUBMITURB32\n", __func__);
		ret = proc_submiturb_compat(ps, p);
		if (ret >= 0)
			inode->i_mtime = current_time(inode);
		break;

	case USBDEVFS_IOCTL32:
		snoop(&dev->dev, "%s: IOCTL32\n", __func__);
		ret = proc_ioctl_compat(ps, ptr_to_compat(p));
		break;
#endif

	case USBDEVFS_DISCARDURB:
		snoop(&dev->dev, "%s: DISCARDURB %px\n", __func__, p);
		ret = proc_unlinkurb(ps, p);
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
	case USBDEVFS_ALLOC_STREAMS:
		ret = proc_alloc_streams(ps, p);
		break;
	case USBDEVFS_FREE_STREAMS:
		ret = proc_free_streams(ps, p);
		break;
	case USBDEVFS_DROP_PRIVILEGES:
		ret = proc_drop_privileges(ps, p);
		break;
	case USBDEVFS_GET_SPEED:
		ret = ps->dev->speed;
		break;
	case USBDEVFS_FORBID_SUSPEND:
		ret = proc_forbid_suspend(ps);
		break;
	case USBDEVFS_ALLOW_SUSPEND:
		ret = proc_allow_suspend(ps);
		break;
	case USBDEVFS_WAIT_FOR_RESUME:
		ret = proc_wait_for_resume(ps);
		break;
	}

	/* Handle variable-length commands */
	switch (cmd & ~IOCSIZE_MASK) {
	case USBDEVFS_CONNINFO_EX(0):
		ret = proc_conninfo_ex(ps, p, _IOC_SIZE(cmd));
		break;
	}

 done:
	usb_unlock_device(dev);
	if (ret >= 0)
		inode->i_atime = current_time(inode);
	return ret;
}

static long usbdev_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	int ret;

	ret = usbdev_do_ioctl(file, cmd, (void __user *)arg);

	return ret;
}

/* No kernel lock - fine */
static __poll_t usbdev_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct usb_dev_state *ps = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &ps->wait, wait);
	if (file->f_mode & FMODE_WRITE && !list_empty(&ps->async_completed))
		mask |= EPOLLOUT | EPOLLWRNORM;
	if (!connected(ps))
		mask |= EPOLLHUP;
	if (list_empty(&ps->list))
		mask |= EPOLLERR;
	return mask;
}

const struct file_operations usbdev_file_operations = {
	.owner =	  THIS_MODULE,
	.llseek =	  no_seek_end_llseek,
	.read =		  usbdev_read,
	.poll =		  usbdev_poll,
	.unlocked_ioctl = usbdev_ioctl,
	.compat_ioctl =   compat_ptr_ioctl,
	.mmap =           usbdev_mmap,
	.open =		  usbdev_open,
	.release =	  usbdev_release,
};

static void usbdev_remove(struct usb_device *udev)
{
	struct usb_dev_state *ps;

	/* Protect against simultaneous resume */
	mutex_lock(&usbfs_mutex);
	while (!list_empty(&udev->filelist)) {
		ps = list_entry(udev->filelist.next, struct usb_dev_state, list);
		destroy_all_async(ps);
		wake_up_all(&ps->wait);
		WRITE_ONCE(ps->not_yet_resumed, 0);
		wake_up_all(&ps->wait_for_resume);
		list_del_init(&ps->list);
		if (ps->discsignr)
			kill_pid_usb_asyncio(ps->discsignr, EPIPE, ps->disccontext,
					     ps->disc_pid, ps->cred);
	}
	mutex_unlock(&usbfs_mutex);
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
	.notifier_call =	usbdev_notify,
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
