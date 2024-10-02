// SPDX-License-Identifier: GPL-2.0
/*
 * chaoskey - driver for ChaosKey device from Altus Metrum.
 *
 * This device provides true random numbers using a noise source based
 * on a reverse-biased p-n junction in avalanche breakdown. More
 * details can be found at http://chaoskey.org
 *
 * The driver connects to the kernel hardware RNG interface to provide
 * entropy for /dev/random and other kernel activities. It also offers
 * a separate /dev/ entry to allow for direct access to the random
 * bit stream.
 *
 * Copyright © 2015 Keith Packard <keithp@keithp.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/hw_random.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

static struct usb_driver chaoskey_driver;
static struct usb_class_driver chaoskey_class;
static int chaoskey_rng_read(struct hwrng *rng, void *data,
			     size_t max, bool wait);

static DEFINE_MUTEX(chaoskey_list_lock);

#define usb_dbg(usb_if, format, arg...) \
	dev_dbg(&(usb_if)->dev, format, ## arg)

#define usb_err(usb_if, format, arg...) \
	dev_err(&(usb_if)->dev, format, ## arg)

/* Version Information */
#define DRIVER_AUTHOR	"Keith Packard, keithp@keithp.com"
#define DRIVER_DESC	"Altus Metrum ChaosKey driver"
#define DRIVER_SHORT	"chaoskey"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define CHAOSKEY_VENDOR_ID	0x1d50	/* OpenMoko */
#define CHAOSKEY_PRODUCT_ID	0x60c6	/* ChaosKey */

#define ALEA_VENDOR_ID		0x12d8	/* Araneus */
#define ALEA_PRODUCT_ID		0x0001	/* Alea I */

#define CHAOSKEY_BUF_LEN	64	/* max size of USB full speed packet */

#define NAK_TIMEOUT (HZ)		/* normal stall/wait timeout */
#define ALEA_FIRST_TIMEOUT (HZ*3)	/* first stall/wait timeout for Alea */

#ifdef CONFIG_USB_DYNAMIC_MINORS
#define USB_CHAOSKEY_MINOR_BASE 0
#else

/* IOWARRIOR_MINOR_BASE + 16, not official yet */
#define USB_CHAOSKEY_MINOR_BASE 224
#endif

static const struct usb_device_id chaoskey_table[] = {
	{ USB_DEVICE(CHAOSKEY_VENDOR_ID, CHAOSKEY_PRODUCT_ID) },
	{ USB_DEVICE(ALEA_VENDOR_ID, ALEA_PRODUCT_ID) },
	{ },
};
MODULE_DEVICE_TABLE(usb, chaoskey_table);

static void chaos_read_callback(struct urb *urb);

/* Driver-local specific stuff */
struct chaoskey {
	struct usb_interface *interface;
	char in_ep;
	struct mutex lock;
	struct mutex rng_lock;
	int open;			/* open count */
	bool present;			/* device not disconnected */
	bool reading;			/* ongoing IO */
	bool reads_started;		/* track first read for Alea */
	int size;			/* size of buf */
	int valid;			/* bytes of buf read */
	int used;			/* bytes of buf consumed */
	char *name;			/* product + serial */
	struct hwrng hwrng;		/* Embedded struct for hwrng */
	int hwrng_registered;		/* registered with hwrng API */
	wait_queue_head_t wait_q;	/* for timeouts */
	struct urb *urb;		/* for performing IO */
	char *buf;
};

static void chaoskey_free(struct chaoskey *dev)
{
	if (dev) {
		usb_dbg(dev->interface, "free");
		usb_free_urb(dev->urb);
		kfree(dev->name);
		kfree(dev->buf);
		usb_put_intf(dev->interface);
		kfree(dev);
	}
}

static int chaoskey_probe(struct usb_interface *interface,
			  const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *altsetting = interface->cur_altsetting;
	struct usb_endpoint_descriptor *epd;
	int in_ep;
	struct chaoskey *dev;
	int result = -ENOMEM;
	int size;
	int res;

	usb_dbg(interface, "probe %s-%s", udev->product, udev->serial);

	/* Find the first bulk IN endpoint and its packet size */
	res = usb_find_bulk_in_endpoint(altsetting, &epd);
	if (res) {
		usb_dbg(interface, "no IN endpoint found");
		return res;
	}

	in_ep = usb_endpoint_num(epd);
	size = usb_endpoint_maxp(epd);

	/* Validate endpoint and size */
	if (size <= 0) {
		usb_dbg(interface, "invalid size (%d)", size);
		return -ENODEV;
	}

	if (size > CHAOSKEY_BUF_LEN) {
		usb_dbg(interface, "size reduced from %d to %d\n",
			size, CHAOSKEY_BUF_LEN);
		size = CHAOSKEY_BUF_LEN;
	}

	/* Looks good, allocate and initialize */

	dev = kzalloc(sizeof(struct chaoskey), GFP_KERNEL);

	if (dev == NULL)
		goto out;

	dev->interface = usb_get_intf(interface);

	dev->buf = kmalloc(size, GFP_KERNEL);

	if (dev->buf == NULL)
		goto out;

	dev->urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!dev->urb)
		goto out;

	usb_fill_bulk_urb(dev->urb,
		udev,
		usb_rcvbulkpipe(udev, in_ep),
		dev->buf,
		size,
		chaos_read_callback,
		dev);

	/* Construct a name using the product and serial values. Each
	 * device needs a unique name for the hwrng code
	 */

	if (udev->product && udev->serial) {
		dev->name = kasprintf(GFP_KERNEL, "%s-%s", udev->product,
				      udev->serial);
		if (dev->name == NULL)
			goto out;
	}

	dev->in_ep = in_ep;

	if (le16_to_cpu(udev->descriptor.idVendor) != ALEA_VENDOR_ID)
		dev->reads_started = true;

	dev->size = size;
	dev->present = true;

	init_waitqueue_head(&dev->wait_q);

	mutex_init(&dev->lock);
	mutex_init(&dev->rng_lock);

	usb_set_intfdata(interface, dev);

	result = usb_register_dev(interface, &chaoskey_class);
	if (result) {
		usb_err(interface, "Unable to allocate minor number.");
		goto out;
	}

	dev->hwrng.name = dev->name ? dev->name : chaoskey_driver.name;
	dev->hwrng.read = chaoskey_rng_read;

	dev->hwrng_registered = (hwrng_register(&dev->hwrng) == 0);
	if (!dev->hwrng_registered)
		usb_err(interface, "Unable to register with hwrng");

	usb_enable_autosuspend(udev);

	usb_dbg(interface, "chaoskey probe success, size %d", dev->size);
	return 0;

out:
	usb_set_intfdata(interface, NULL);
	chaoskey_free(dev);
	return result;
}

static void chaoskey_disconnect(struct usb_interface *interface)
{
	struct chaoskey	*dev;

	usb_dbg(interface, "disconnect");
	dev = usb_get_intfdata(interface);
	if (!dev) {
		usb_dbg(interface, "disconnect failed - no dev");
		return;
	}

	if (dev->hwrng_registered)
		hwrng_unregister(&dev->hwrng);

	mutex_lock(&chaoskey_list_lock);
	usb_deregister_dev(interface, &chaoskey_class);

	usb_set_intfdata(interface, NULL);
	mutex_lock(&dev->lock);

	dev->present = false;
	usb_poison_urb(dev->urb);

	if (!dev->open) {
		mutex_unlock(&dev->lock);
		chaoskey_free(dev);
	} else
		mutex_unlock(&dev->lock);

	mutex_unlock(&chaoskey_list_lock);
	usb_dbg(interface, "disconnect done");
}

static int chaoskey_open(struct inode *inode, struct file *file)
{
	struct chaoskey *dev;
	struct usb_interface *interface;
	int rv = 0;

	/* get the interface from minor number and driver information */
	interface = usb_find_interface(&chaoskey_driver, iminor(inode));
	if (!interface)
		return -ENODEV;

	usb_dbg(interface, "open");

	dev = usb_get_intfdata(interface);
	if (!dev) {
		usb_dbg(interface, "open (dev)");
		return -ENODEV;
	}

	file->private_data = dev;
	mutex_lock(&chaoskey_list_lock);
	mutex_lock(&dev->lock);
	if (dev->present)
		++dev->open;
	else
		rv = -ENODEV;
	mutex_unlock(&dev->lock);
	mutex_unlock(&chaoskey_list_lock);

	return rv;
}

static int chaoskey_release(struct inode *inode, struct file *file)
{
	struct chaoskey *dev = file->private_data;
	struct usb_interface *interface;
	int rv = 0;

	if (dev == NULL)
		return -ENODEV;

	interface = dev->interface;

	usb_dbg(interface, "release");

	mutex_lock(&chaoskey_list_lock);
	mutex_lock(&dev->lock);

	usb_dbg(interface, "open count at release is %d", dev->open);

	if (dev->open <= 0) {
		usb_dbg(interface, "invalid open count (%d)", dev->open);
		rv = -ENODEV;
		goto bail;
	}

	--dev->open;

	if (!dev->present) {
		if (dev->open == 0) {
			mutex_unlock(&dev->lock);
			chaoskey_free(dev);
			goto destruction;
		}
	}
bail:
	mutex_unlock(&dev->lock);
destruction:
	mutex_lock(&chaoskey_list_lock);
	usb_dbg(interface, "release success");
	return rv;
}

static void chaos_read_callback(struct urb *urb)
{
	struct chaoskey *dev = urb->context;
	int status = urb->status;

	usb_dbg(dev->interface, "callback status (%d)", status);

	if (status == 0)
		dev->valid = urb->actual_length;
	else
		dev->valid = 0;

	dev->used = 0;

	/* must be seen first before validity is announced */
	smp_wmb();

	dev->reading = false;
	wake_up(&dev->wait_q);
}

/* Fill the buffer. Called with dev->lock held
 */
static int _chaoskey_fill(struct chaoskey *dev)
{
	DEFINE_WAIT(wait);
	int result;
	bool started;

	usb_dbg(dev->interface, "fill");

	/* Return immediately if someone called before the buffer was
	 * empty */
	if (dev->valid != dev->used) {
		usb_dbg(dev->interface, "not empty yet (valid %d used %d)",
			dev->valid, dev->used);
		return 0;
	}

	/* Bail if the device has been removed */
	if (!dev->present) {
		usb_dbg(dev->interface, "device not present");
		return -ENODEV;
	}

	/* Make sure the device is awake */
	result = usb_autopm_get_interface(dev->interface);
	if (result) {
		usb_dbg(dev->interface, "wakeup failed (result %d)", result);
		return result;
	}

	dev->reading = true;
	result = usb_submit_urb(dev->urb, GFP_KERNEL);
	if (result < 0) {
		result = usb_translate_errors(result);
		dev->reading = false;
		goto out;
	}

	/* The first read on the Alea takes a little under 2 seconds.
	 * Reads after the first read take only a few microseconds
	 * though.  Presumably the entropy-generating circuit needs
	 * time to ramp up.  So, we wait longer on the first read.
	 */
	started = dev->reads_started;
	dev->reads_started = true;
	result = wait_event_interruptible_timeout(
		dev->wait_q,
		!dev->reading,
		(started ? NAK_TIMEOUT : ALEA_FIRST_TIMEOUT) );

	if (result < 0) {
		usb_kill_urb(dev->urb);
		goto out;
	}

	if (result == 0) {
		result = -ETIMEDOUT;
		usb_kill_urb(dev->urb);
	} else {
		result = dev->valid;
	}
out:
	/* Let the device go back to sleep eventually */
	usb_autopm_put_interface(dev->interface);

	usb_dbg(dev->interface, "read %d bytes", dev->valid);

	return result;
}

static ssize_t chaoskey_read(struct file *file,
			     char __user *buffer,
			     size_t count,
			     loff_t *ppos)
{
	struct chaoskey *dev;
	ssize_t read_count = 0;
	int this_time;
	int result = 0;
	unsigned long remain;

	dev = file->private_data;

	if (dev == NULL || !dev->present)
		return -ENODEV;

	usb_dbg(dev->interface, "read %zu", count);

	while (count > 0) {

		/* Grab the rng_lock briefly to ensure that the hwrng interface
		 * gets priority over other user access
		 */
		result = mutex_lock_interruptible(&dev->rng_lock);
		if (result)
			goto bail;
		mutex_unlock(&dev->rng_lock);

		result = mutex_lock_interruptible(&dev->lock);
		if (result)
			goto bail;
		if (dev->valid == dev->used) {
			result = _chaoskey_fill(dev);
			if (result < 0) {
				mutex_unlock(&dev->lock);
				goto bail;
			}
		}

		this_time = dev->valid - dev->used;
		if (this_time > count)
			this_time = count;

		remain = copy_to_user(buffer, dev->buf + dev->used, this_time);
		if (remain) {
			result = -EFAULT;

			/* Consume the bytes that were copied so we don't leak
			 * data to user space
			 */
			dev->used += this_time - remain;
			mutex_unlock(&dev->lock);
			goto bail;
		}

		count -= this_time;
		read_count += this_time;
		buffer += this_time;
		dev->used += this_time;
		mutex_unlock(&dev->lock);
	}
bail:
	if (read_count) {
		usb_dbg(dev->interface, "read %zu bytes", read_count);
		return read_count;
	}
	usb_dbg(dev->interface, "empty read, result %d", result);
	if (result == -ETIMEDOUT)
		result = -EAGAIN;
	return result;
}

static int chaoskey_rng_read(struct hwrng *rng, void *data,
			     size_t max, bool wait)
{
	struct chaoskey *dev = container_of(rng, struct chaoskey, hwrng);
	int this_time;

	usb_dbg(dev->interface, "rng_read max %zu wait %d", max, wait);

	if (!dev->present) {
		usb_dbg(dev->interface, "device not present");
		return 0;
	}

	/* Hold the rng_lock until we acquire the device lock so that
	 * this operation gets priority over other user access to the
	 * device
	 */
	mutex_lock(&dev->rng_lock);

	mutex_lock(&dev->lock);

	mutex_unlock(&dev->rng_lock);

	/* Try to fill the buffer if empty. It doesn't actually matter
	 * if _chaoskey_fill works; we'll just return zero bytes as
	 * the buffer will still be empty
	 */
	if (dev->valid == dev->used)
		(void) _chaoskey_fill(dev);

	this_time = dev->valid - dev->used;
	if (this_time > max)
		this_time = max;

	memcpy(data, dev->buf + dev->used, this_time);

	dev->used += this_time;

	mutex_unlock(&dev->lock);

	usb_dbg(dev->interface, "rng_read this_time %d\n", this_time);
	return this_time;
}

#ifdef CONFIG_PM
static int chaoskey_suspend(struct usb_interface *interface,
			    pm_message_t message)
{
	usb_dbg(interface, "suspend");
	return 0;
}

static int chaoskey_resume(struct usb_interface *interface)
{
	struct chaoskey *dev;
	struct usb_device *udev = interface_to_usbdev(interface);

	usb_dbg(interface, "resume");
	dev = usb_get_intfdata(interface);

	/*
	 * We may have lost power.
	 * In that case the device that needs a long time
	 * for the first requests needs an extended timeout
	 * again
	 */
	if (le16_to_cpu(udev->descriptor.idVendor) == ALEA_VENDOR_ID)
		dev->reads_started = false;

	return 0;
}
#else
#define chaoskey_suspend NULL
#define chaoskey_resume NULL
#endif

/* file operation pointers */
static const struct file_operations chaoskey_fops = {
	.owner = THIS_MODULE,
	.read = chaoskey_read,
	.open = chaoskey_open,
	.release = chaoskey_release,
	.llseek = default_llseek,
};

/* class driver information */
static struct usb_class_driver chaoskey_class = {
	.name = "chaoskey%d",
	.fops = &chaoskey_fops,
	.minor_base = USB_CHAOSKEY_MINOR_BASE,
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver chaoskey_driver = {
	.name = DRIVER_SHORT,
	.probe = chaoskey_probe,
	.disconnect = chaoskey_disconnect,
	.suspend = chaoskey_suspend,
	.resume = chaoskey_resume,
	.reset_resume = chaoskey_resume,
	.id_table = chaoskey_table,
	.supports_autosuspend = 1,
};

module_usb_driver(chaoskey_driver);

