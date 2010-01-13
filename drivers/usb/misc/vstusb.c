/*****************************************************************************
 *  File: drivers/usb/misc/vstusb.c
 *
 *  Purpose: Support for the bulk USB Vernier Spectrophotometers
 *
 *  Author:     Johnnie Peters
 *              Axian Consulting
 *              Beaverton, OR, USA 97005
 *
 *  Modified by:     EQware Engineering, Inc.
 *                   Oregon City, OR, USA 97045
 *
 *  Copyright:  2007, 2008
 *              Vernier Software & Technology
 *              Beaverton, OR, USA 97005
 *
 *  Web:        www.vernier.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *****************************************************************************/
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/smp_lock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#include <linux/usb/vstusb.h>

#define DRIVER_VERSION "VST USB Driver Version 1.5"
#define DRIVER_DESC "Vernier Software Technology Bulk USB Driver"

#ifdef CONFIG_USB_DYNAMIC_MINORS
	#define VSTUSB_MINOR_BASE	0
#else
	#define VSTUSB_MINOR_BASE	199
#endif

#define USB_VENDOR_OCEANOPTICS	0x2457
#define USB_VENDOR_VERNIER	0x08F7	/* Vernier Software & Technology */

#define USB_PRODUCT_USB2000	0x1002
#define USB_PRODUCT_ADC1000_FW	0x1003	/* firmware download (renumerates) */
#define USB_PRODUCT_ADC1000	0x1004
#define USB_PRODUCT_HR2000_FW	0x1009	/* firmware download (renumerates) */
#define USB_PRODUCT_HR2000	0x100A
#define USB_PRODUCT_HR4000_FW	0x1011	/* firmware download (renumerates) */
#define USB_PRODUCT_HR4000	0x1012
#define USB_PRODUCT_USB650	0x1014	/* "Red Tide" */
#define USB_PRODUCT_QE65000	0x1018
#define USB_PRODUCT_USB4000	0x1022
#define USB_PRODUCT_USB325	0x1024	/* "Vernier Spectrometer" */

#define USB_PRODUCT_LABPRO	0x0001
#define USB_PRODUCT_LABQUEST	0x0005

#define VST_MAXBUFFER		(64*1024)

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(USB_VENDOR_OCEANOPTICS, USB_PRODUCT_USB2000)},
	{ USB_DEVICE(USB_VENDOR_OCEANOPTICS, USB_PRODUCT_HR4000)},
	{ USB_DEVICE(USB_VENDOR_OCEANOPTICS, USB_PRODUCT_USB650)},
	{ USB_DEVICE(USB_VENDOR_OCEANOPTICS, USB_PRODUCT_USB4000)},
	{ USB_DEVICE(USB_VENDOR_OCEANOPTICS, USB_PRODUCT_USB325)},
	{ USB_DEVICE(USB_VENDOR_VERNIER, USB_PRODUCT_LABQUEST)},
	{ USB_DEVICE(USB_VENDOR_VERNIER, USB_PRODUCT_LABPRO)},
	{},
};

MODULE_DEVICE_TABLE(usb, id_table);

struct vstusb_device {
	struct kref				kref;
	struct mutex            lock;
	struct usb_device       *usb_dev;
	char                    present;
	char                    isopen;
	struct usb_anchor       submitted;
	int                     rd_pipe;
	int                     rd_timeout_ms;
	int                     wr_pipe;
	int                     wr_timeout_ms;
};
#define to_vst_dev(d) container_of(d, struct vstusb_device, kref)

static struct usb_driver vstusb_driver;

static void vstusb_delete(struct kref *kref)
{
	struct vstusb_device *vstdev = to_vst_dev(kref);

	usb_put_dev(vstdev->usb_dev);
	kfree(vstdev);
}

static int vstusb_open(struct inode *inode, struct file *file)
{
	struct vstusb_device *vstdev;
	struct usb_interface *interface;

	lock_kernel();
	interface = usb_find_interface(&vstusb_driver, iminor(inode));

	if (!interface) {
		printk(KERN_ERR KBUILD_MODNAME
		       ": %s - error, can't find device for minor %d\n",
		       __func__, iminor(inode));
		unlock_kernel();
		return -ENODEV;
	}

	vstdev = usb_get_intfdata(interface);

	if (!vstdev) {
		unlock_kernel();
		return -ENODEV;
	}

	/* lock this device */
	mutex_lock(&vstdev->lock);

	/* can only open one time */
	if ((!vstdev->present) || (vstdev->isopen)) {
		mutex_unlock(&vstdev->lock);
		unlock_kernel();
		return -EBUSY;
	}

	/* increment our usage count */
	kref_get(&vstdev->kref);

	vstdev->isopen = 1;

	/* save device in the file's private structure */
	file->private_data = vstdev;

	dev_dbg(&vstdev->usb_dev->dev, "%s: opened\n", __func__);

	mutex_unlock(&vstdev->lock);
	unlock_kernel();

	return 0;
}

static int vstusb_release(struct inode *inode, struct file *file)
{
	struct vstusb_device *vstdev;

	vstdev = file->private_data;

	if (vstdev == NULL)
		return -ENODEV;

	mutex_lock(&vstdev->lock);

	vstdev->isopen = 0;

	dev_dbg(&vstdev->usb_dev->dev, "%s: released\n", __func__);

	mutex_unlock(&vstdev->lock);

	kref_put(&vstdev->kref, vstusb_delete);

	return 0;
}

static void usb_api_blocking_completion(struct urb *urb)
{
	struct completion *completeit = urb->context;

	complete(completeit);
}

static int vstusb_fill_and_send_urb(struct urb *urb,
				    struct usb_device *usb_dev,
				    unsigned int pipe, void *data,
				    unsigned int len, struct completion *done)
{
	struct usb_host_endpoint *ep;
	struct usb_host_endpoint **hostep;
	unsigned int pipend;

	int status;

	hostep = usb_pipein(pipe) ? usb_dev->ep_in : usb_dev->ep_out;
	pipend = usb_pipeendpoint(pipe);
	ep = hostep[pipend];

	if (!ep || (len == 0))
		return -EINVAL;

	if ((ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
	    == USB_ENDPOINT_XFER_INT) {
		pipe = (pipe & ~(3 << 30)) | (PIPE_INTERRUPT << 30);
		usb_fill_int_urb(urb, usb_dev, pipe, data, len,
				 (usb_complete_t)usb_api_blocking_completion,
				 NULL, ep->desc.bInterval);
	} else
		usb_fill_bulk_urb(urb, usb_dev, pipe, data, len,
				  (usb_complete_t)usb_api_blocking_completion,
				  NULL);

	init_completion(done);
	urb->context = done;
	urb->actual_length = 0;
	status = usb_submit_urb(urb, GFP_KERNEL);

	return status;
}

static int vstusb_complete_urb(struct urb *urb, struct completion *done,
			       int timeout, int *actual_length)
{
	unsigned long expire;
	int status;

	expire = timeout ? msecs_to_jiffies(timeout) : MAX_SCHEDULE_TIMEOUT;
	if (!wait_for_completion_interruptible_timeout(done, expire)) {
		usb_kill_urb(urb);
		status = urb->status == -ENOENT ? -ETIMEDOUT : urb->status;

		dev_dbg(&urb->dev->dev,
			"%s timed out on ep%d%s len=%d/%d, urb status = %d\n",
			current->comm,
			usb_pipeendpoint(urb->pipe),
			usb_pipein(urb->pipe) ? "in" : "out",
			urb->actual_length,
			urb->transfer_buffer_length,
			urb->status);

	} else {
		if (signal_pending(current)) {
			/* if really an error */
			if (urb->status && !((urb->status == -ENOENT)     ||
					     (urb->status == -ECONNRESET) ||
					     (urb->status == -ESHUTDOWN))) {
				status = -EINTR;
				usb_kill_urb(urb);
			} else {
				status = 0;
			}

			dev_dbg(&urb->dev->dev,
				"%s: signal pending on ep%d%s len=%d/%d,"
				"urb status = %d\n",
				current->comm,
				usb_pipeendpoint(urb->pipe),
				usb_pipein(urb->pipe) ? "in" : "out",
				urb->actual_length,
				urb->transfer_buffer_length,
				urb->status);

		} else {
			status = urb->status;
		}
	}

	if (actual_length)
		*actual_length = urb->actual_length;

	return status;
}

static ssize_t vstusb_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *ppos)
{
	struct vstusb_device *vstdev;
	int cnt = -1;
	void *buf;
	int retval = 0;

	struct urb              *urb;
	struct usb_device       *dev;
	unsigned int            pipe;
	int                     timeout;

	DECLARE_COMPLETION_ONSTACK(done);

	vstdev = file->private_data;

	if (vstdev == NULL)
		return -ENODEV;

	/* verify that we actually want to read some data */
	if ((count == 0) || (count > VST_MAXBUFFER))
		return -EINVAL;

	/* lock this object */
	if (mutex_lock_interruptible(&vstdev->lock))
		return -ERESTARTSYS;

	/* anyone home */
	if (!vstdev->present) {
		mutex_unlock(&vstdev->lock);
		printk(KERN_ERR KBUILD_MODNAME
		       ": %s: device not present\n", __func__);
		return -ENODEV;
	}

	/* pull out the necessary data */
	dev =     vstdev->usb_dev;
	pipe =    usb_rcvbulkpipe(dev, vstdev->rd_pipe);
	timeout = vstdev->rd_timeout_ms;

	buf = kmalloc(count, GFP_KERNEL);
	if (buf == NULL) {
		mutex_unlock(&vstdev->lock);
		return -ENOMEM;
	}

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		kfree(buf);
		mutex_unlock(&vstdev->lock);
		return -ENOMEM;
	}

	usb_anchor_urb(urb, &vstdev->submitted);
	retval = vstusb_fill_and_send_urb(urb, dev, pipe, buf, count, &done);
	mutex_unlock(&vstdev->lock);
	if (retval) {
		usb_unanchor_urb(urb);
		dev_err(&dev->dev, "%s: error %d filling and sending urb %d\n",
			__func__, retval, pipe);
		goto exit;
	}

	retval = vstusb_complete_urb(urb, &done, timeout, &cnt);
	if (retval) {
		dev_err(&dev->dev, "%s: error %d completing urb %d\n",
			__func__, retval, pipe);
		goto exit;
	}

	if (copy_to_user(buffer, buf, cnt)) {
		dev_err(&dev->dev, "%s: can't copy_to_user\n", __func__);
		retval = -EFAULT;
	} else {
		retval = cnt;
		dev_dbg(&dev->dev, "%s: read %d bytes from pipe %d\n",
			__func__, cnt, pipe);
	}

exit:
	usb_free_urb(urb);
	kfree(buf);
	return retval;
}

static ssize_t vstusb_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	struct vstusb_device *vstdev;
	int cnt = -1;
	void *buf;
	int retval = 0;

	struct urb              *urb;
	struct usb_device       *dev;
	unsigned int            pipe;
	int                     timeout;

	DECLARE_COMPLETION_ONSTACK(done);

	vstdev = file->private_data;

	if (vstdev == NULL)
		return -ENODEV;

	/* verify that we actually have some data to write */
	if ((count == 0) || (count > VST_MAXBUFFER))
		return retval;

	/* lock this object */
	if (mutex_lock_interruptible(&vstdev->lock))
		return -ERESTARTSYS;

	/* anyone home */
	if (!vstdev->present) {
		mutex_unlock(&vstdev->lock);
		printk(KERN_ERR KBUILD_MODNAME
		       ": %s: device not present\n", __func__);
		return -ENODEV;
	}

	/* pull out the necessary data */
	dev =     vstdev->usb_dev;
	pipe =    usb_sndbulkpipe(dev, vstdev->wr_pipe);
	timeout = vstdev->wr_timeout_ms;

	buf = kmalloc(count, GFP_KERNEL);
	if (buf == NULL) {
		mutex_unlock(&vstdev->lock);
		return -ENOMEM;
	}

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		kfree(buf);
		mutex_unlock(&vstdev->lock);
		return -ENOMEM;
	}

	if (copy_from_user(buf, buffer, count)) {
		mutex_unlock(&vstdev->lock);
		dev_err(&dev->dev, "%s: can't copy_from_user\n", __func__);
		retval = -EFAULT;
		goto exit;
	}

	usb_anchor_urb(urb, &vstdev->submitted);
	retval = vstusb_fill_and_send_urb(urb, dev, pipe, buf, count, &done);
	mutex_unlock(&vstdev->lock);
	if (retval) {
		usb_unanchor_urb(urb);
		dev_err(&dev->dev, "%s: error %d filling and sending urb %d\n",
			__func__, retval, pipe);
		goto exit;
	}

	retval = vstusb_complete_urb(urb, &done, timeout, &cnt);
	if (retval) {
		dev_err(&dev->dev, "%s: error %d completing urb %d\n",
			__func__, retval, pipe);
		goto exit;
	} else {
		retval = cnt;
		dev_dbg(&dev->dev, "%s: sent %d bytes to pipe %d\n",
			__func__, cnt, pipe);
	}

exit:
	usb_free_urb(urb);
	kfree(buf);
	return retval;
}

static long vstusb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	int cnt = -1;
	void __user *data = (void __user *)arg;
	struct vstusb_args usb_data;

	struct vstusb_device *vstdev;
	void *buffer = NULL; /* must be initialized. buffer is
			      *	referenced on exit but not all
			      * ioctls allocate it */

	struct urb              *urb = NULL; /* must be initialized. urb is
					      *	referenced on exit but not all
					      * ioctls allocate it */
	struct usb_device       *dev;
	unsigned int            pipe;
	int                     timeout;

	DECLARE_COMPLETION_ONSTACK(done);

	vstdev = file->private_data;

	if (_IOC_TYPE(cmd) != VST_IOC_MAGIC) {
		dev_warn(&vstdev->usb_dev->dev,
			 "%s: ioctl command %x, bad ioctl magic %x, "
			 "expected %x\n", __func__, cmd,
			 _IOC_TYPE(cmd), VST_IOC_MAGIC);
		return -EINVAL;
	}

	if (vstdev == NULL)
		return -ENODEV;

	if (copy_from_user(&usb_data, data, sizeof(struct vstusb_args))) {
		dev_err(&vstdev->usb_dev->dev, "%s: can't copy_from_user\n",
			__func__);
		return -EFAULT;
	}

	/* lock this object */
	if (mutex_lock_interruptible(&vstdev->lock)) {
		retval = -ERESTARTSYS;
		goto exit;
	}

	/* anyone home */
	if (!vstdev->present) {
		mutex_unlock(&vstdev->lock);
		dev_err(&vstdev->usb_dev->dev, "%s: device not present\n",
			__func__);
		retval = -ENODEV;
		goto exit;
	}

	/* pull out the necessary data */
	dev = vstdev->usb_dev;

	switch (cmd) {

	case IOCTL_VSTUSB_CONFIG_RW:

		vstdev->rd_pipe = usb_data.rd_pipe;
		vstdev->rd_timeout_ms = usb_data.rd_timeout_ms;
		vstdev->wr_pipe = usb_data.wr_pipe;
		vstdev->wr_timeout_ms = usb_data.wr_timeout_ms;

		mutex_unlock(&vstdev->lock);

		dev_dbg(&dev->dev, "%s: setting pipes/timeouts, "
			"rdpipe = %d, rdtimeout = %d, "
			"wrpipe = %d, wrtimeout = %d\n", __func__,
			vstdev->rd_pipe, vstdev->rd_timeout_ms,
			vstdev->wr_pipe, vstdev->wr_timeout_ms);
		break;

	case IOCTL_VSTUSB_SEND_PIPE:

		if ((usb_data.count == 0) || (usb_data.count > VST_MAXBUFFER)) {
			mutex_unlock(&vstdev->lock);
			retval = -EINVAL;
			goto exit;
		}

		buffer = kmalloc(usb_data.count, GFP_KERNEL);
		if (buffer == NULL) {
			mutex_unlock(&vstdev->lock);
			retval = -ENOMEM;
			goto exit;
		}

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			mutex_unlock(&vstdev->lock);
			retval = -ENOMEM;
			goto exit;
		}

		timeout = usb_data.timeout_ms;

		pipe = usb_sndbulkpipe(dev, usb_data.pipe);

		if (copy_from_user(buffer, usb_data.buffer, usb_data.count)) {
			dev_err(&dev->dev, "%s: can't copy_from_user\n",
				__func__);
			mutex_unlock(&vstdev->lock);
			retval = -EFAULT;
			goto exit;
		}

		usb_anchor_urb(urb, &vstdev->submitted);
		retval = vstusb_fill_and_send_urb(urb, dev, pipe, buffer,
						  usb_data.count, &done);
		mutex_unlock(&vstdev->lock);
		if (retval) {
			usb_unanchor_urb(urb);
			dev_err(&dev->dev,
				"%s: error %d filling and sending urb %d\n",
				__func__, retval, pipe);
			goto exit;
		}

		retval = vstusb_complete_urb(urb, &done, timeout, &cnt);
		if (retval) {
			dev_err(&dev->dev, "%s: error %d completing urb %d\n",
				__func__, retval, pipe);
		}

		break;
	case IOCTL_VSTUSB_RECV_PIPE:

		if ((usb_data.count == 0) || (usb_data.count > VST_MAXBUFFER)) {
			mutex_unlock(&vstdev->lock);
			retval = -EINVAL;
			goto exit;
		}

		buffer = kmalloc(usb_data.count, GFP_KERNEL);
		if (buffer == NULL) {
			mutex_unlock(&vstdev->lock);
			retval = -ENOMEM;
			goto exit;
		}

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			mutex_unlock(&vstdev->lock);
			retval = -ENOMEM;
			goto exit;
		}

		timeout = usb_data.timeout_ms;

		pipe = usb_rcvbulkpipe(dev, usb_data.pipe);

		usb_anchor_urb(urb, &vstdev->submitted);
		retval = vstusb_fill_and_send_urb(urb, dev, pipe, buffer,
						  usb_data.count, &done);
		mutex_unlock(&vstdev->lock);
		if (retval) {
			usb_unanchor_urb(urb);
			dev_err(&dev->dev,
				"%s: error %d filling and sending urb %d\n",
				__func__, retval, pipe);
			goto exit;
		}

		retval = vstusb_complete_urb(urb, &done, timeout, &cnt);
		if (retval) {
			dev_err(&dev->dev, "%s: error %d completing urb %d\n",
				__func__, retval, pipe);
			goto exit;
		}

		if (copy_to_user(usb_data.buffer, buffer, cnt)) {
			dev_err(&dev->dev, "%s: can't copy_to_user\n",
				__func__);
			retval = -EFAULT;
			goto exit;
		}

		usb_data.count = cnt;
		if (copy_to_user(data, &usb_data, sizeof(struct vstusb_args))) {
			dev_err(&dev->dev, "%s: can't copy_to_user\n",
				__func__);
			retval = -EFAULT;
		} else {
			dev_dbg(&dev->dev, "%s: recv %zd bytes from pipe %d\n",
				__func__, usb_data.count, usb_data.pipe);
		}

		break;

	default:
		mutex_unlock(&vstdev->lock);
		dev_warn(&dev->dev, "ioctl_vstusb: invalid ioctl cmd %x\n",
			 cmd);
		return -EINVAL;
		break;
	}
exit:
	usb_free_urb(urb);
	kfree(buffer);
	return retval;
}

static const struct file_operations vstusb_fops = {
	.owner =                THIS_MODULE,
	.read =                 vstusb_read,
	.write =                vstusb_write,
	.unlocked_ioctl =       vstusb_ioctl,
	.compat_ioctl =         vstusb_ioctl,
	.open =                 vstusb_open,
	.release =              vstusb_release,
};

static struct usb_class_driver usb_vstusb_class = {
	.name =         "usb/vstusb%d",
	.fops =         &vstusb_fops,
	.minor_base =   VSTUSB_MINOR_BASE,
};

static int vstusb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct vstusb_device *vstdev;
	int i;
	int retval = 0;

	/* allocate memory for our device state and intialize it */

	vstdev = kzalloc(sizeof(*vstdev), GFP_KERNEL);
	if (vstdev == NULL)
		return -ENOMEM;

	/* must do usb_get_dev() prior to kref_init() since the kref_put()
	 * release function will do a usb_put_dev() */
	usb_get_dev(dev);
	kref_init(&vstdev->kref);
	mutex_init(&vstdev->lock);

	i = dev->descriptor.bcdDevice;

	dev_dbg(&intf->dev, "Version %1d%1d.%1d%1d found at address %d\n",
		(i & 0xF000) >> 12, (i & 0xF00) >> 8,
		(i & 0xF0) >> 4, (i & 0xF), dev->devnum);

	vstdev->present = 1;
	vstdev->isopen = 0;
	vstdev->usb_dev = dev;
	init_usb_anchor(&vstdev->submitted);

	usb_set_intfdata(intf, vstdev);
	retval = usb_register_dev(intf, &usb_vstusb_class);
	if (retval) {
		dev_err(&intf->dev,
			"%s: Not able to get a minor for this device.\n",
			__func__);
		usb_set_intfdata(intf, NULL);
		kref_put(&vstdev->kref, vstusb_delete);
		return retval;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&intf->dev,
		 "VST USB Device #%d now attached to major %d minor %d\n",
		 (intf->minor - VSTUSB_MINOR_BASE), USB_MAJOR, intf->minor);

	dev_info(&intf->dev, "%s, %s\n", DRIVER_DESC, DRIVER_VERSION);

	return retval;
}

static void vstusb_disconnect(struct usb_interface *intf)
{
	struct vstusb_device *vstdev = usb_get_intfdata(intf);

	usb_deregister_dev(intf, &usb_vstusb_class);
	usb_set_intfdata(intf, NULL);

	if (vstdev) {

		mutex_lock(&vstdev->lock);
		vstdev->present = 0;

		usb_kill_anchored_urbs(&vstdev->submitted);

		mutex_unlock(&vstdev->lock);

		kref_put(&vstdev->kref, vstusb_delete);
	}

}

static int vstusb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct vstusb_device *vstdev = usb_get_intfdata(intf);
	int time;
	if (!vstdev)
		return 0;

	mutex_lock(&vstdev->lock);
	time = usb_wait_anchor_empty_timeout(&vstdev->submitted, 1000);
	if (!time)
		usb_kill_anchored_urbs(&vstdev->submitted);
	mutex_unlock(&vstdev->lock);

	return 0;
}

static int vstusb_resume(struct usb_interface *intf)
{
	return 0;
}

static struct usb_driver vstusb_driver = {
	.name =         "vstusb",
	.probe =        vstusb_probe,
	.disconnect =   vstusb_disconnect,
	.suspend =      vstusb_suspend,
	.resume =       vstusb_resume,
	.id_table = id_table,
};

static int __init vstusb_init(void)
{
	int rc;

	rc = usb_register(&vstusb_driver);
	if (rc)
		printk(KERN_ERR "%s: failed to register (%d)", __func__, rc);

	return rc;
}

static void __exit vstusb_exit(void)
{
	usb_deregister(&vstusb_driver);
}

module_init(vstusb_init);
module_exit(vstusb_exit);

MODULE_AUTHOR("Dennis O'Brien/Stephen Ware");
MODULE_DESCRIPTION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
