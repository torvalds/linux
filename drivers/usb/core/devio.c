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
 *  $Id: devio.c,v 1.7 2000/02/01 17:28:48 fliegl Exp $
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
#include <linux/smp_lock.h>
#include <linux/signal.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usbdevice_fs.h>
#include <linux/cdev.h>
#include <linux/notifier.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <linux/moduleparam.h>

#include "hcd.h"	/* for usbcore internals */
#include "usb.h"

#define USB_MAXBUS			64
#define USB_DEVICE_MAX			USB_MAXBUS * 128
static struct class *usb_device_class;

struct async {
	struct list_head asynclist;
	struct dev_state *ps;
	pid_t pid;
	uid_t uid, euid;
	unsigned int signr;
	unsigned int ifnum;
	void __user *userbuffer;
	void __user *userurb;
	struct urb *urb;
};

static int usbfs_snoop = 0;
module_param (usbfs_snoop, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC (usbfs_snoop, "true to log all usbfs traffic");

#define snoop(dev, format, arg...)				\
	do {							\
		if (usbfs_snoop)				\
			dev_info( dev , format , ## arg);	\
	} while (0)

#define USB_DEVICE_DEV		MKDEV(USB_DEVICE_MAJOR, 0)


#define	MAX_USBFS_BUFFER_SIZE	16384

static inline int connected (struct usb_device *dev)
{
	return dev->state != USB_STATE_NOTATTACHED;
}

static loff_t usbdev_lseek(struct file *file, loff_t offset, int orig)
{
	loff_t ret;

	lock_kernel();

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

	unlock_kernel();
	return ret;
}

static ssize_t usbdev_read(struct file *file, char __user *buf, size_t nbytes, loff_t *ppos)
{
	struct dev_state *ps = (struct dev_state *)file->private_data;
	struct usb_device *dev = ps->dev;
	ssize_t ret = 0;
	unsigned len;
	loff_t pos;
	int i;

	pos = *ppos;
	usb_lock_device(dev);
	if (!connected(dev)) {
		ret = -ENODEV;
		goto err;
	} else if (pos < 0) {
		ret = -EINVAL;
		goto err;
	}

	if (pos < sizeof(struct usb_device_descriptor)) {
		struct usb_device_descriptor temp_desc ; /* 18 bytes - fits on the stack */

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
        unsigned int assize = sizeof(struct async) + numisoframes * sizeof(struct usb_iso_packet_descriptor);
        struct async *as = kzalloc(assize, GFP_KERNEL);

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
	kfree(as->urb->transfer_buffer);
	kfree(as->urb->setup_packet);
	usb_free_urb(as->urb);
	kfree(as);
}

static inline void async_newpending(struct async *as)
{
        struct dev_state *ps = as->ps;
        unsigned long flags;
        
        spin_lock_irqsave(&ps->lock, flags);
        list_add_tail(&as->asynclist, &ps->async_pending);
        spin_unlock_irqrestore(&ps->lock, flags);
}

static inline void async_removepending(struct async *as)
{
        struct dev_state *ps = as->ps;
        unsigned long flags;
        
        spin_lock_irqsave(&ps->lock, flags);
        list_del_init(&as->asynclist);
        spin_unlock_irqrestore(&ps->lock, flags);
}

static inline struct async *async_getcompleted(struct dev_state *ps)
{
        unsigned long flags;
        struct async *as = NULL;

        spin_lock_irqsave(&ps->lock, flags);
        if (!list_empty(&ps->async_completed)) {
                as = list_entry(ps->async_completed.next, struct async, asynclist);
                list_del_init(&as->asynclist);
        }
        spin_unlock_irqrestore(&ps->lock, flags);
        return as;
}

static inline struct async *async_getpending(struct dev_state *ps, void __user *userurb)
{
        unsigned long flags;
        struct async *as;

        spin_lock_irqsave(&ps->lock, flags);
	list_for_each_entry(as, &ps->async_pending, asynclist)
		if (as->userurb == userurb) {
			list_del_init(&as->asynclist);
			spin_unlock_irqrestore(&ps->lock, flags);
			return as;
		}
        spin_unlock_irqrestore(&ps->lock, flags);
        return NULL;
}

static void snoop_urb(struct urb *urb, void __user *userurb)
{
	int j;
	unsigned char *data = urb->transfer_buffer;

	if (!usbfs_snoop)
		return;

	if (urb->pipe & USB_DIR_IN)
		dev_info(&urb->dev->dev, "direction=IN\n");
	else
		dev_info(&urb->dev->dev, "direction=OUT\n");
	dev_info(&urb->dev->dev, "userurb=%p\n", userurb);
	dev_info(&urb->dev->dev, "transfer_buffer_length=%d\n",
		 urb->transfer_buffer_length);
	dev_info(&urb->dev->dev, "actual_length=%d\n", urb->actual_length);
	dev_info(&urb->dev->dev, "data: ");
	for (j = 0; j < urb->transfer_buffer_length; ++j)
		printk ("%02x ", data[j]);
	printk("\n");
}

static void async_completed(struct urb *urb, struct pt_regs *regs)
{
        struct async *as = (struct async *)urb->context;
        struct dev_state *ps = as->ps;
	struct siginfo sinfo;

        spin_lock(&ps->lock);
        list_move_tail(&as->asynclist, &ps->async_completed);
        spin_unlock(&ps->lock);
	if (as->signr) {
		sinfo.si_signo = as->signr;
		sinfo.si_errno = as->urb->status;
		sinfo.si_code = SI_ASYNCIO;
		sinfo.si_addr = as->userurb;
		kill_proc_info_as_uid(as->signr, &sinfo, as->pid, as->uid, 
				      as->euid);
	}
	snoop(&urb->dev->dev, "urb complete\n");
	snoop_urb(urb, as->userurb);
	wake_up(&ps->wait);
}

static void destroy_async (struct dev_state *ps, struct list_head *list)
{
	struct async *as;
	unsigned long flags;

	spin_lock_irqsave(&ps->lock, flags);
	while (!list_empty(list)) {
		as = list_entry(list->next, struct async, asynclist);
		list_del_init(&as->asynclist);

		/* drop the spinlock so the completion handler can run */
		spin_unlock_irqrestore(&ps->lock, flags);
		usb_kill_urb(as->urb);
		spin_lock_irqsave(&ps->lock, flags);
	}
	spin_unlock_irqrestore(&ps->lock, flags);
	as = async_getcompleted(ps);
	while (as) {
		free_async(as);
		as = async_getcompleted(ps);
	}
}

static void destroy_async_on_interface (struct dev_state *ps, unsigned int ifnum)
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

static inline void destroy_all_async(struct dev_state *ps)
{
	        destroy_async(ps, &ps->async_pending);
}

/*
 * interface claims are made only at the request of user level code,
 * which can also release them (explicitly or by closing files).
 * they're also undone when devices disconnect.
 */

static int driver_probe (struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	return -ENODEV;
}

static void driver_disconnect(struct usb_interface *intf)
{
	struct dev_state *ps = usb_get_intfdata (intf);
	unsigned int ifnum = intf->altsetting->desc.bInterfaceNumber;

	if (!ps)
		return;

	/* NOTE:  this relies on usbcore having canceled and completed
	 * all pending I/O requests; 2.6 does that.
	 */

	if (likely(ifnum < 8*sizeof(ps->ifclaimed)))
		clear_bit(ifnum, &ps->ifclaimed);
	else
		warn("interface number %u out of range", ifnum);

	usb_set_intfdata (intf, NULL);

	/* force async requests to complete */
	destroy_async_on_interface(ps, ifnum);
}

struct usb_driver usbfs_driver = {
	.name =		"usbfs",
	.probe =	driver_probe,
	.disconnect =	driver_disconnect,
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

	/* lock against other changes to driver bindings */
	down_write(&usb_bus_type.subsys.rwsem);
	intf = usb_ifnum_to_if(dev, ifnum);
	if (!intf)
		err = -ENOENT;
	else
		err = usb_driver_claim_interface(&usbfs_driver, intf, ps);
	up_write(&usb_bus_type.subsys.rwsem);
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
	/* lock against other changes to driver bindings */
	down_write(&usb_bus_type.subsys.rwsem);
	intf = usb_ifnum_to_if(dev, ifnum);
	if (!intf)
		err = -ENOENT;
	else if (test_and_clear_bit(ifnum, &ps->ifclaimed)) {
		usb_driver_release_interface(&usbfs_driver, intf);
		err = 0;
	}
	up_write(&usb_bus_type.subsys.rwsem);
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
	dev_warn(&ps->dev->dev, "usbfs: process %d (%s) did not claim interface %u before use\n",
	       current->pid, current->comm, ifnum);
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

static int check_ctrlrecip(struct dev_state *ps, unsigned int requesttype, unsigned int index)
{
	int ret = 0;

	if (ps->dev->state != USB_STATE_ADDRESS
	 && ps->dev->state != USB_STATE_CONFIGURED)
		return -EHOSTUNREACH;
	if (USB_TYPE_VENDOR == (USB_TYPE_MASK & requesttype))
		return 0;

	index &= 0xff;
	switch (requesttype & USB_RECIP_MASK) {
	case USB_RECIP_ENDPOINT:
		if ((ret = findintfep(ps->dev, index)) >= 0)
			ret = checkintf(ps, ret);
		break;

	case USB_RECIP_INTERFACE:
		ret = checkintf(ps, index);
		break;
	}
	return ret;
}

static struct usb_device *usbdev_lookup_minor(int minor)
{
	struct device *device;
	struct usb_device *udev = NULL;

	down(&usb_device_class->sem);
	list_for_each_entry(device, &usb_device_class->devices, node) {
		if (device->devt == MKDEV(USB_DEVICE_MAJOR, minor)) {
			udev = device->platform_data;
			break;
		}
	}
	up(&usb_device_class->sem);

	return udev;
};

/*
 * file operations
 */
static int usbdev_open(struct inode *inode, struct file *file)
{
	struct usb_device *dev = NULL;
	struct dev_state *ps;
	int ret;

	/* 
	 * no locking necessary here, as chrdev_open has the kernel lock
	 * (still acquire the kernel lock for safety)
	 */
	ret = -ENOMEM;
	if (!(ps = kmalloc(sizeof(struct dev_state), GFP_KERNEL)))
		goto out_nolock;

	lock_kernel();
	ret = -ENOENT;
	/* check if we are called from a real node or usbfs */
	if (imajor(inode) == USB_DEVICE_MAJOR)
		dev = usbdev_lookup_minor(iminor(inode));
	if (!dev)
		dev = inode->u.generic_ip;
	if (!dev) {
		kfree(ps);
		goto out;
	}
	usb_get_dev(dev);
	ret = 0;
	ps->dev = dev;
	ps->file = file;
	spin_lock_init(&ps->lock);
	INIT_LIST_HEAD(&ps->async_pending);
	INIT_LIST_HEAD(&ps->async_completed);
	init_waitqueue_head(&ps->wait);
	ps->discsignr = 0;
	ps->disc_pid = current->pid;
	ps->disc_uid = current->uid;
	ps->disc_euid = current->euid;
	ps->disccontext = NULL;
	ps->ifclaimed = 0;
	wmb();
	list_add_tail(&ps->list, &dev->filelist);
	file->private_data = ps;
 out:
	unlock_kernel();
 out_nolock:
        return ret;
}

static int usbdev_release(struct inode *inode, struct file *file)
{
	struct dev_state *ps = (struct dev_state *)file->private_data;
	struct usb_device *dev = ps->dev;
	unsigned int ifnum;

	usb_lock_device(dev);
	list_del_init(&ps->list);
	for (ifnum = 0; ps->ifclaimed && ifnum < 8*sizeof(ps->ifclaimed);
			ifnum++) {
		if (test_bit(ifnum, &ps->ifclaimed))
			releaseintf(ps, ifnum);
	}
	destroy_all_async(ps);
	usb_unlock_device(dev);
	usb_put_dev(dev);
	ps->dev = NULL;
	kfree(ps);
        return 0;
}

static int proc_control(struct dev_state *ps, void __user *arg)
{
	struct usb_device *dev = ps->dev;
	struct usbdevfs_ctrltransfer ctrl;
	unsigned int tmo;
	unsigned char *tbuf;
	int i, j, ret;

	if (copy_from_user(&ctrl, arg, sizeof(ctrl)))
		return -EFAULT;
	if ((ret = check_ctrlrecip(ps, ctrl.bRequestType, ctrl.wIndex)))
		return ret;
	if (ctrl.wLength > PAGE_SIZE)
		return -EINVAL;
	if (!(tbuf = (unsigned char *)__get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	tmo = ctrl.timeout;
	if (ctrl.bRequestType & 0x80) {
		if (ctrl.wLength && !access_ok(VERIFY_WRITE, ctrl.data, ctrl.wLength)) {
			free_page((unsigned long)tbuf);
			return -EINVAL;
		}
		snoop(&dev->dev, "control read: bRequest=%02x "
				"bRrequestType=%02x wValue=%04x "
				"wIndex=%04x wLength=%04x\n",
			ctrl.bRequest, ctrl.bRequestType, ctrl.wValue,
				ctrl.wIndex, ctrl.wLength);

		usb_unlock_device(dev);
		i = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), ctrl.bRequest, ctrl.bRequestType,
				       ctrl.wValue, ctrl.wIndex, tbuf, ctrl.wLength, tmo);
		usb_lock_device(dev);
		if ((i > 0) && ctrl.wLength) {
			if (usbfs_snoop) {
				dev_info(&dev->dev, "control read: data ");
				for (j = 0; j < i; ++j)
					printk("%02x ", (unsigned char)(tbuf)[j]);
				printk("\n");
			}
			if (copy_to_user(ctrl.data, tbuf, i)) {
				free_page((unsigned long)tbuf);
				return -EFAULT;
			}
		}
	} else {
		if (ctrl.wLength) {
			if (copy_from_user(tbuf, ctrl.data, ctrl.wLength)) {
				free_page((unsigned long)tbuf);
				return -EFAULT;
			}
		}
		snoop(&dev->dev, "control write: bRequest=%02x "
				"bRrequestType=%02x wValue=%04x "
				"wIndex=%04x wLength=%04x\n",
			ctrl.bRequest, ctrl.bRequestType, ctrl.wValue,
				ctrl.wIndex, ctrl.wLength);
		if (usbfs_snoop) {
			dev_info(&dev->dev, "control write: data: ");
			for (j = 0; j < ctrl.wLength; ++j)
				printk("%02x ", (unsigned char)(tbuf)[j]);
			printk("\n");
		}
		usb_unlock_device(dev);
		i = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), ctrl.bRequest, ctrl.bRequestType,
				       ctrl.wValue, ctrl.wIndex, tbuf, ctrl.wLength, tmo);
		usb_lock_device(dev);
	}
	free_page((unsigned long)tbuf);
	if (i<0 && i != -EPIPE) {
		dev_printk(KERN_DEBUG, &dev->dev, "usbfs: USBDEVFS_CONTROL "
			   "failed cmd %s rqt %u rq %u len %u ret %d\n",
			   current->comm, ctrl.bRequestType, ctrl.bRequest,
			   ctrl.wLength, i);
	}
	return i;
}

static int proc_bulk(struct dev_state *ps, void __user *arg)
{
	struct usb_device *dev = ps->dev;
	struct usbdevfs_bulktransfer bulk;
	unsigned int tmo, len1, pipe;
	int len2;
	unsigned char *tbuf;
	int i, j, ret;

	if (copy_from_user(&bulk, arg, sizeof(bulk)))
		return -EFAULT;
	if ((ret = findintfep(ps->dev, bulk.ep)) < 0)
		return ret;
	if ((ret = checkintf(ps, ret)))
		return ret;
	if (bulk.ep & USB_DIR_IN)
		pipe = usb_rcvbulkpipe(dev, bulk.ep & 0x7f);
	else
		pipe = usb_sndbulkpipe(dev, bulk.ep & 0x7f);
	if (!usb_maxpacket(dev, pipe, !(bulk.ep & USB_DIR_IN)))
		return -EINVAL;
	len1 = bulk.len;
	if (len1 > MAX_USBFS_BUFFER_SIZE)
		return -EINVAL;
	if (!(tbuf = kmalloc(len1, GFP_KERNEL)))
		return -ENOMEM;
	tmo = bulk.timeout;
	if (bulk.ep & 0x80) {
		if (len1 && !access_ok(VERIFY_WRITE, bulk.data, len1)) {
			kfree(tbuf);
			return -EINVAL;
		}
		snoop(&dev->dev, "bulk read: len=0x%02x timeout=%04d\n",
			bulk.len, bulk.timeout);
		usb_unlock_device(dev);
		i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, tmo);
		usb_lock_device(dev);
		if (!i && len2) {
			if (usbfs_snoop) {
				dev_info(&dev->dev, "bulk read: data ");
				for (j = 0; j < len2; ++j)
					printk("%02x ", (unsigned char)(tbuf)[j]);
				printk("\n");
			}
			if (copy_to_user(bulk.data, tbuf, len2)) {
				kfree(tbuf);
				return -EFAULT;
			}
		}
	} else {
		if (len1) {
			if (copy_from_user(tbuf, bulk.data, len1)) {
				kfree(tbuf);
				return -EFAULT;
			}
		}
		snoop(&dev->dev, "bulk write: len=0x%02x timeout=%04d\n",
			bulk.len, bulk.timeout);
		if (usbfs_snoop) {
			dev_info(&dev->dev, "bulk write: data: ");
			for (j = 0; j < len1; ++j)
				printk("%02x ", (unsigned char)(tbuf)[j]);
			printk("\n");
		}
		usb_unlock_device(dev);
		i = usb_bulk_msg(dev, pipe, tbuf, len1, &len2, tmo);
		usb_lock_device(dev);
	}
	kfree(tbuf);
	if (i < 0)
		return i;
	return len2;
}

static int proc_resetep(struct dev_state *ps, void __user *arg)
{
	unsigned int ep;
	int ret;

	if (get_user(ep, (unsigned int __user *)arg))
		return -EFAULT;
	if ((ret = findintfep(ps->dev, ep)) < 0)
		return ret;
	if ((ret = checkintf(ps, ret)))
		return ret;
	usb_settoggle(ps->dev, ep & 0xf, !(ep & USB_DIR_IN), 0);
	return 0;
}

static int proc_clearhalt(struct dev_state *ps, void __user *arg)
{
	unsigned int ep;
	int pipe;
	int ret;

	if (get_user(ep, (unsigned int __user *)arg))
		return -EFAULT;
	if ((ret = findintfep(ps->dev, ep)) < 0)
		return ret;
	if ((ret = checkintf(ps, ret)))
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
	down_read(&usb_bus_type.subsys.rwsem);
	intf = usb_ifnum_to_if(ps->dev, gd.interface);
	if (!intf || !intf->dev.driver)
		ret = -ENODATA;
	else {
		strncpy(gd.driver, intf->dev.driver->name,
				sizeof(gd.driver));
		ret = (copy_to_user(arg, &gd, sizeof(gd)) ? -EFAULT : 0);
	}
	up_read(&usb_bus_type.subsys.rwsem);
	return ret;
}

static int proc_connectinfo(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_connectinfo ci;

	ci.devnum = ps->dev->devnum;
	ci.slow = ps->dev->speed == USB_SPEED_LOW;
	if (copy_to_user(arg, &ci, sizeof(ci)))
		return -EFAULT;
	return 0;
}

static int proc_resetdevice(struct dev_state *ps)
{
	return usb_reset_composite_device(ps->dev, NULL);
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
	unsigned int u;
	int status = 0;
 	struct usb_host_config *actconfig;

	if (get_user(u, (unsigned int __user *)arg))
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
				dev_warn (&ps->dev->dev,
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
	struct async *as;
	struct usb_ctrlrequest *dr = NULL;
	unsigned int u, totlen, isofrmlen;
	int ret, interval = 0, ifnum = -1;

	if (uurb->flags & ~(USBDEVFS_URB_ISO_ASAP|USBDEVFS_URB_SHORT_NOT_OK|
			   URB_NO_FSBR|URB_ZERO_PACKET))
		return -EINVAL;
	if (!uurb->buffer)
		return -EINVAL;
	if (uurb->signr != 0 && (uurb->signr < SIGRTMIN || uurb->signr > SIGRTMAX))
		return -EINVAL;
	if (!(uurb->type == USBDEVFS_URB_TYPE_CONTROL && (uurb->endpoint & ~USB_ENDPOINT_DIR_MASK) == 0)) {
		if ((ifnum = findintfep(ps->dev, uurb->endpoint)) < 0)
			return ifnum;
		if ((ret = checkintf(ps, ifnum)))
			return ret;
	}
	if ((uurb->endpoint & USB_ENDPOINT_DIR_MASK) != 0)
		ep = ps->dev->ep_in [uurb->endpoint & USB_ENDPOINT_NUMBER_MASK];
	else
		ep = ps->dev->ep_out [uurb->endpoint & USB_ENDPOINT_NUMBER_MASK];
	if (!ep)
		return -ENOENT;
	switch(uurb->type) {
	case USBDEVFS_URB_TYPE_CONTROL:
		if ((ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				!= USB_ENDPOINT_XFER_CONTROL)
			return -EINVAL;
		/* min 8 byte setup packet, max 8 byte setup plus an arbitrary data stage */
		if (uurb->buffer_length < 8 || uurb->buffer_length > (8 + MAX_USBFS_BUFFER_SIZE))
			return -EINVAL;
		if (!(dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL)))
			return -ENOMEM;
		if (copy_from_user(dr, uurb->buffer, 8)) {
			kfree(dr);
			return -EFAULT;
		}
		if (uurb->buffer_length < (le16_to_cpup(&dr->wLength) + 8)) {
			kfree(dr);
			return -EINVAL;
		}
		if ((ret = check_ctrlrecip(ps, dr->bRequestType, le16_to_cpup(&dr->wIndex)))) {
			kfree(dr);
			return ret;
		}
		uurb->endpoint = (uurb->endpoint & ~USB_ENDPOINT_DIR_MASK) | (dr->bRequestType & USB_ENDPOINT_DIR_MASK);
		uurb->number_of_packets = 0;
		uurb->buffer_length = le16_to_cpup(&dr->wLength);
		uurb->buffer += 8;
		if (!access_ok((uurb->endpoint & USB_DIR_IN) ?  VERIFY_WRITE : VERIFY_READ, uurb->buffer, uurb->buffer_length)) {
			kfree(dr);
			return -EFAULT;
		}
		snoop(&ps->dev->dev, "control urb\n");
		break;

	case USBDEVFS_URB_TYPE_BULK:
		switch (ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
		case USB_ENDPOINT_XFER_CONTROL:
		case USB_ENDPOINT_XFER_ISOC:
			return -EINVAL;
		/* allow single-shot interrupt transfers, at bogus rates */
		}
		uurb->number_of_packets = 0;
		if (uurb->buffer_length > MAX_USBFS_BUFFER_SIZE)
			return -EINVAL;
		if (!access_ok((uurb->endpoint & USB_DIR_IN) ? VERIFY_WRITE : VERIFY_READ, uurb->buffer, uurb->buffer_length))
			return -EFAULT;
		snoop(&ps->dev->dev, "bulk urb\n");
		break;

	case USBDEVFS_URB_TYPE_ISO:
		/* arbitrary limit */
		if (uurb->number_of_packets < 1 || uurb->number_of_packets > 128)
			return -EINVAL;
		if ((ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				!= USB_ENDPOINT_XFER_ISOC)
			return -EINVAL;
		interval = 1 << min (15, ep->desc.bInterval - 1);
		isofrmlen = sizeof(struct usbdevfs_iso_packet_desc) * uurb->number_of_packets;
		if (!(isopkt = kmalloc(isofrmlen, GFP_KERNEL)))
			return -ENOMEM;
		if (copy_from_user(isopkt, iso_frame_desc, isofrmlen)) {
			kfree(isopkt);
			return -EFAULT;
		}
		for (totlen = u = 0; u < uurb->number_of_packets; u++) {
			/* arbitrary limit, sufficient for USB 2.0 high-bandwidth iso */
			if (isopkt[u].length > 8192) {
				kfree(isopkt);
				return -EINVAL;
			}
			totlen += isopkt[u].length;
		}
		if (totlen > 32768) {
			kfree(isopkt);
			return -EINVAL;
		}
		uurb->buffer_length = totlen;
		snoop(&ps->dev->dev, "iso urb\n");
		break;

	case USBDEVFS_URB_TYPE_INTERRUPT:
		uurb->number_of_packets = 0;
		if ((ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				!= USB_ENDPOINT_XFER_INT)
			return -EINVAL;
		if (ps->dev->speed == USB_SPEED_HIGH)
			interval = 1 << min (15, ep->desc.bInterval - 1);
		else
			interval = ep->desc.bInterval;
		if (uurb->buffer_length > MAX_USBFS_BUFFER_SIZE)
			return -EINVAL;
		if (!access_ok((uurb->endpoint & USB_DIR_IN) ? VERIFY_WRITE : VERIFY_READ, uurb->buffer, uurb->buffer_length))
			return -EFAULT;
		snoop(&ps->dev->dev, "interrupt urb\n");
		break;

	default:
		return -EINVAL;
	}
	if (!(as = alloc_async(uurb->number_of_packets))) {
		kfree(isopkt);
		kfree(dr);
		return -ENOMEM;
	}
	if (!(as->urb->transfer_buffer = kmalloc(uurb->buffer_length, GFP_KERNEL))) {
		kfree(isopkt);
		kfree(dr);
		free_async(as);
		return -ENOMEM;
	}
        as->urb->dev = ps->dev;
        as->urb->pipe = (uurb->type << 30) | __create_pipe(ps->dev, uurb->endpoint & 0xf) | (uurb->endpoint & USB_DIR_IN);
        as->urb->transfer_flags = uurb->flags;
	as->urb->transfer_buffer_length = uurb->buffer_length;
	as->urb->setup_packet = (unsigned char*)dr;
	as->urb->start_frame = uurb->start_frame;
	as->urb->number_of_packets = uurb->number_of_packets;
	as->urb->interval = interval;
        as->urb->context = as;
        as->urb->complete = async_completed;
	for (totlen = u = 0; u < uurb->number_of_packets; u++) {
		as->urb->iso_frame_desc[u].offset = totlen;
		as->urb->iso_frame_desc[u].length = isopkt[u].length;
		totlen += isopkt[u].length;
	}
	kfree(isopkt);
	as->ps = ps;
        as->userurb = arg;
	if (uurb->endpoint & USB_DIR_IN)
		as->userbuffer = uurb->buffer;
	else
		as->userbuffer = NULL;
	as->signr = uurb->signr;
	as->ifnum = ifnum;
	as->pid = current->pid;
	as->uid = current->uid;
	as->euid = current->euid;
	if (!(uurb->endpoint & USB_DIR_IN)) {
		if (copy_from_user(as->urb->transfer_buffer, uurb->buffer, as->urb->transfer_buffer_length)) {
			free_async(as);
			return -EFAULT;
		}
	}
	snoop(&as->urb->dev->dev, "submit urb\n");
	snoop_urb(as->urb, as->userurb);
        async_newpending(as);
        if ((ret = usb_submit_urb(as->urb, GFP_KERNEL))) {
		dev_printk(KERN_DEBUG, &ps->dev->dev, "usbfs: usb_submit_urb returned %d\n", ret);
                async_removepending(as);
                free_async(as);
                return ret;
        }
        return 0;
}

static int proc_submiturb(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_urb uurb;

	if (copy_from_user(&uurb, arg, sizeof(uurb)))
		return -EFAULT;

	return proc_do_submiturb(ps, &uurb, (((struct usbdevfs_urb __user *)arg)->iso_frame_desc), arg);
}

static int proc_unlinkurb(struct dev_state *ps, void __user *arg)
{
	struct async *as;

	as = async_getpending(ps, arg);
	if (!as)
		return -EINVAL;
	usb_kill_urb(as->urb);
	return 0;
}

static int processcompl(struct async *as, void __user * __user *arg)
{
	struct urb *urb = as->urb;
	struct usbdevfs_urb __user *userurb = as->userurb;
	void __user *addr = as->userurb;
	unsigned int i;

	if (as->userbuffer)
		if (copy_to_user(as->userbuffer, urb->transfer_buffer, urb->transfer_buffer_length))
			return -EFAULT;
	if (put_user(urb->status, &userurb->status))
		return -EFAULT;
	if (put_user(urb->actual_length, &userurb->actual_length))
		return -EFAULT;
	if (put_user(urb->error_count, &userurb->error_count))
		return -EFAULT;

	if (usb_pipeisoc(urb->pipe)) {
		for (i = 0; i < urb->number_of_packets; i++) {
			if (put_user(urb->iso_frame_desc[i].actual_length,
				     &userurb->iso_frame_desc[i].actual_length))
				return -EFAULT;
			if (put_user(urb->iso_frame_desc[i].status,
				     &userurb->iso_frame_desc[i].status))
				return -EFAULT;
		}
	}

	free_async(as);

	if (put_user(addr, (void __user * __user *)arg))
		return -EFAULT;
	return 0;
}

static struct async* reap_as(struct dev_state *ps)
{
        DECLARE_WAITQUEUE(wait, current);
	struct async *as = NULL;
	struct usb_device *dev = ps->dev;

	add_wait_queue(&ps->wait, &wait);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		if ((as = async_getcompleted(ps)))
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
	if (as)
		return processcompl(as, (void __user * __user *)arg);
	if (signal_pending(current))
		return -EINTR;
	return -EIO;
}

static int proc_reapurbnonblock(struct dev_state *ps, void __user *arg)
{
	struct async *as;

	if (!(as = async_getcompleted(ps)))
		return -EAGAIN;
	return processcompl(as, (void __user * __user *)arg);
}

#ifdef CONFIG_COMPAT

static int get_urb32(struct usbdevfs_urb *kurb,
		     struct usbdevfs_urb32 __user *uurb)
{
	__u32  uptr;
	if (get_user(kurb->type, &uurb->type) ||
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
	if (__get_user(uptr, &uurb->buffer))
		return -EFAULT;
	kurb->usercontext = compat_ptr(uptr);

	return 0;
}

static int proc_submiturb_compat(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_urb uurb;

	if (get_urb32(&uurb,(struct usbdevfs_urb32 *)arg))
		return -EFAULT;

	return proc_do_submiturb(ps, &uurb, ((struct usbdevfs_urb32 __user *)arg)->iso_frame_desc, arg);
}

static int processcompl_compat(struct async *as, void __user * __user *arg)
{
	struct urb *urb = as->urb;
	struct usbdevfs_urb32 __user *userurb = as->userurb;
	void __user *addr = as->userurb;
	unsigned int i;

	if (as->userbuffer)
		if (copy_to_user(as->userbuffer, urb->transfer_buffer, urb->transfer_buffer_length))
			return -EFAULT;
	if (put_user(urb->status, &userurb->status))
		return -EFAULT;
	if (put_user(urb->actual_length, &userurb->actual_length))
		return -EFAULT;
	if (put_user(urb->error_count, &userurb->error_count))
		return -EFAULT;

	if (usb_pipeisoc(urb->pipe)) {
		for (i = 0; i < urb->number_of_packets; i++) {
			if (put_user(urb->iso_frame_desc[i].actual_length,
				     &userurb->iso_frame_desc[i].actual_length))
				return -EFAULT;
			if (put_user(urb->iso_frame_desc[i].status,
				     &userurb->iso_frame_desc[i].status))
				return -EFAULT;
		}
	}

	free_async(as);
	if (put_user((u32)(u64)addr, (u32 __user *)arg))
		return -EFAULT;
	return 0;
}

static int proc_reapurb_compat(struct dev_state *ps, void __user *arg)
{
	struct async *as = reap_as(ps);
	if (as)
		return processcompl_compat(as, (void __user * __user *)arg);
	if (signal_pending(current))
		return -EINTR;
	return -EIO;
}

static int proc_reapurbnonblock_compat(struct dev_state *ps, void __user *arg)
{
	struct async *as;

	if (!(as = async_getcompleted(ps)))
		return -EAGAIN;
	return processcompl_compat(as, (void __user * __user *)arg);
}

#endif

static int proc_disconnectsignal(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_disconnectsignal ds;

	if (copy_from_user(&ds, arg, sizeof(ds)))
		return -EFAULT;
	if (ds.signr != 0 && (ds.signr < SIGRTMIN || ds.signr > SIGRTMAX))
		return -EINVAL;
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
	if ((size = _IOC_SIZE (ctl->ioctl_code)) > 0) {
		if ((buf = kmalloc (size, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		if ((_IOC_DIR(ctl->ioctl_code) & _IOC_WRITE)) {
			if (copy_from_user (buf, ctl->data, size)) {
				kfree(buf);
				return -EFAULT;
			}
		} else {
			memset (buf, 0, size);
		}
	}

	if (!connected(ps->dev)) {
		kfree(buf);
		return -ENODEV;
	}

	if (ps->dev->state != USB_STATE_CONFIGURED)
		retval = -EHOSTUNREACH;
	else if (!(intf = usb_ifnum_to_if (ps->dev, ctl->ifno)))
               retval = -EINVAL;
	else switch (ctl->ioctl_code) {

	/* disconnect kernel driver from interface */
	case USBDEVFS_DISCONNECT:

		down_write(&usb_bus_type.subsys.rwsem);
		if (intf->dev.driver) {
			driver = to_usb_driver(intf->dev.driver);
			dev_dbg (&intf->dev, "disconnect by usbfs\n");
			usb_driver_release_interface(driver, intf);
		} else
			retval = -ENODATA;
		up_write(&usb_bus_type.subsys.rwsem);
		break;

	/* let kernel drivers try to (re)bind to the interface */
	case USBDEVFS_CONNECT:
		usb_unlock_device(ps->dev);
		bus_rescan_devices(intf->dev.bus);
		usb_lock_device(ps->dev);
		break;

	/* talk directly to the interface's driver */
	default:
		down_read(&usb_bus_type.subsys.rwsem);
		if (intf->dev.driver)
			driver = to_usb_driver(intf->dev.driver);
		if (driver == NULL || driver->ioctl == NULL) {
			retval = -ENOTTY;
		} else {
			retval = driver->ioctl (intf, ctl->ioctl_code, buf);
			if (retval == -ENOIOCTLCMD)
				retval = -ENOTTY;
		}
		up_read(&usb_bus_type.subsys.rwsem);
	}

	/* cleanup and return */
	if (retval >= 0
			&& (_IOC_DIR (ctl->ioctl_code) & _IOC_READ) != 0
			&& size > 0
			&& copy_to_user (ctl->data, buf, size) != 0)
		retval = -EFAULT;

	kfree(buf);
	return retval;
}

static int proc_ioctl_default(struct dev_state *ps, void __user *arg)
{
	struct usbdevfs_ioctl	ctrl;

	if (copy_from_user(&ctrl, arg, sizeof (ctrl)))
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
	if (get_user(ctrl.ifno, &uioc->ifno) ||
	    get_user(ctrl.ioctl_code, &uioc->ioctl_code) ||
	    __get_user(udata, &uioc->data))
		return -EFAULT;
	ctrl.data = compat_ptr(udata);

	return proc_ioctl(ps, &ctrl);
}
#endif

/*
 * NOTE:  All requests here that have interface numbers as parameters
 * are assuming that somehow the configuration has been prevented from
 * changing.  But there's no mechanism to ensure that...
 */
static int usbdev_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct dev_state *ps = (struct dev_state *)file->private_data;
	struct usb_device *dev = ps->dev;
	void __user *p = (void __user *)arg;
	int ret = -ENOTTY;

	if (!(file->f_mode & FMODE_WRITE))
		return -EPERM;
	usb_lock_device(dev);
	if (!connected(dev)) {
		usb_unlock_device(dev);
		return -ENODEV;
	}

	switch (cmd) {
	case USBDEVFS_CONTROL:
		snoop(&dev->dev, "%s: CONTROL\n", __FUNCTION__);
		ret = proc_control(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_BULK:
		snoop(&dev->dev, "%s: BULK\n", __FUNCTION__);
		ret = proc_bulk(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_RESETEP:
		snoop(&dev->dev, "%s: RESETEP\n", __FUNCTION__);
		ret = proc_resetep(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_RESET:
		snoop(&dev->dev, "%s: RESET\n", __FUNCTION__);
		ret = proc_resetdevice(ps);
		break;

	case USBDEVFS_CLEAR_HALT:
		snoop(&dev->dev, "%s: CLEAR_HALT\n", __FUNCTION__);
		ret = proc_clearhalt(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_GETDRIVER:
		snoop(&dev->dev, "%s: GETDRIVER\n", __FUNCTION__);
		ret = proc_getdriver(ps, p);
		break;

	case USBDEVFS_CONNECTINFO:
		snoop(&dev->dev, "%s: CONNECTINFO\n", __FUNCTION__);
		ret = proc_connectinfo(ps, p);
		break;

	case USBDEVFS_SETINTERFACE:
		snoop(&dev->dev, "%s: SETINTERFACE\n", __FUNCTION__);
		ret = proc_setintf(ps, p);
		break;

	case USBDEVFS_SETCONFIGURATION:
		snoop(&dev->dev, "%s: SETCONFIGURATION\n", __FUNCTION__);
		ret = proc_setconfig(ps, p);
		break;

	case USBDEVFS_SUBMITURB:
		snoop(&dev->dev, "%s: SUBMITURB\n", __FUNCTION__);
		ret = proc_submiturb(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

#ifdef CONFIG_COMPAT

	case USBDEVFS_SUBMITURB32:
		snoop(&dev->dev, "%s: SUBMITURB32\n", __FUNCTION__);
		ret = proc_submiturb_compat(ps, p);
		if (ret >= 0)
			inode->i_mtime = CURRENT_TIME;
		break;

	case USBDEVFS_REAPURB32:
		snoop(&dev->dev, "%s: REAPURB32\n", __FUNCTION__);
		ret = proc_reapurb_compat(ps, p);
		break;

	case USBDEVFS_REAPURBNDELAY32:
		snoop(&dev->dev, "%s: REAPURBDELAY32\n", __FUNCTION__);
		ret = proc_reapurbnonblock_compat(ps, p);
		break;

	case USBDEVFS_IOCTL32:
		snoop(&dev->dev, "%s: IOCTL\n", __FUNCTION__);
		ret = proc_ioctl_compat(ps, (compat_uptr_t)(long)p);
		break;
#endif

	case USBDEVFS_DISCARDURB:
		snoop(&dev->dev, "%s: DISCARDURB\n", __FUNCTION__);
		ret = proc_unlinkurb(ps, p);
		break;

	case USBDEVFS_REAPURB:
		snoop(&dev->dev, "%s: REAPURB\n", __FUNCTION__);
		ret = proc_reapurb(ps, p);
		break;

	case USBDEVFS_REAPURBNDELAY:
		snoop(&dev->dev, "%s: REAPURBDELAY\n", __FUNCTION__);
		ret = proc_reapurbnonblock(ps, p);
		break;

	case USBDEVFS_DISCSIGNAL:
		snoop(&dev->dev, "%s: DISCSIGNAL\n", __FUNCTION__);
		ret = proc_disconnectsignal(ps, p);
		break;

	case USBDEVFS_CLAIMINTERFACE:
		snoop(&dev->dev, "%s: CLAIMINTERFACE\n", __FUNCTION__);
		ret = proc_claiminterface(ps, p);
		break;

	case USBDEVFS_RELEASEINTERFACE:
		snoop(&dev->dev, "%s: RELEASEINTERFACE\n", __FUNCTION__);
		ret = proc_releaseinterface(ps, p);
		break;

	case USBDEVFS_IOCTL:
		snoop(&dev->dev, "%s: IOCTL\n", __FUNCTION__);
		ret = proc_ioctl_default(ps, p);
		break;
	}
	usb_unlock_device(dev);
	if (ret >= 0)
		inode->i_atime = CURRENT_TIME;
	return ret;
}

/* No kernel lock - fine */
static unsigned int usbdev_poll(struct file *file, struct poll_table_struct *wait)
{
	struct dev_state *ps = (struct dev_state *)file->private_data;
        unsigned int mask = 0;

	poll_wait(file, &ps->wait, wait);
	if (file->f_mode & FMODE_WRITE && !list_empty(&ps->async_completed))
		mask |= POLLOUT | POLLWRNORM;
	if (!connected(ps->dev))
		mask |= POLLERR | POLLHUP;
	return mask;
}

struct file_operations usbfs_device_file_operations = {
	.llseek =	usbdev_lseek,
	.read =		usbdev_read,
	.poll =		usbdev_poll,
	.ioctl =	usbdev_ioctl,
	.open =		usbdev_open,
	.release =	usbdev_release,
};

static void usbdev_add(struct usb_device *dev)
{
	int minor = ((dev->bus->busnum-1) * 128) + (dev->devnum-1);

	dev->usbfs_dev = device_create(usb_device_class, &dev->dev,
				MKDEV(USB_DEVICE_MAJOR, minor),
				"usbdev%d.%d", dev->bus->busnum, dev->devnum);

	dev->usbfs_dev->platform_data = dev;
}

static void usbdev_remove(struct usb_device *dev)
{
	device_unregister(dev->usbfs_dev);
}

static int usbdev_notify(struct notifier_block *self, unsigned long action,
			 void *dev)
{
	switch (action) {
	case USB_DEVICE_ADD:
		usbdev_add(dev);
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

static struct cdev usb_device_cdev = {
	.kobj   = {.name = "usb_device", },
	.owner  = THIS_MODULE,
};

int __init usbdev_init(void)
{
	int retval;

	retval = register_chrdev_region(USB_DEVICE_DEV, USB_DEVICE_MAX,
			"usb_device");
	if (retval) {
		err("unable to register minors for usb_device");
		goto out;
	}
	cdev_init(&usb_device_cdev, &usbfs_device_file_operations);
	retval = cdev_add(&usb_device_cdev, USB_DEVICE_DEV, USB_DEVICE_MAX);
	if (retval) {
		err("unable to get usb_device major %d", USB_DEVICE_MAJOR);
		goto error_cdev;
	}
	usb_device_class = class_create(THIS_MODULE, "usb_device");
	if (IS_ERR(usb_device_class)) {
		err("unable to register usb_device class");
		retval = PTR_ERR(usb_device_class);
		goto error_class;
	}

	usb_register_notify(&usbdev_nb);

out:
	return retval;

error_class:
	usb_device_class = NULL;
	cdev_del(&usb_device_cdev);

error_cdev:
	unregister_chrdev_region(USB_DEVICE_DEV, USB_DEVICE_MAX);
	goto out;
}

void usbdev_cleanup(void)
{
	usb_unregister_notify(&usbdev_nb);
	class_destroy(usb_device_class);
	cdev_del(&usb_device_cdev);
	unregister_chrdev_region(USB_DEVICE_DEV, USB_DEVICE_MAX);
}

