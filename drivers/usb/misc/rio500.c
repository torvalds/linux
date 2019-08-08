// SPDX-License-Identifier: GPL-2.0+
/* -*- linux-c -*- */

/* 
 * Driver for USB Rio 500
 *
 * Cesar Miquel (miquel@df.uba.ar)
 * 
 * based on hp_scanner.c by David E. Nelson (dnelson@jump.net)
 *
 * Based upon mouse.c (Brad Keryan) and printer.c (Michael Gee).
 *
 * Changelog:
 * 30/05/2003  replaced lock/unlock kernel with up/down
 *             Daniele Bellucci  bellucda@tiscali.it
 * */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/wait.h>

#include "rio500_usb.h"

#define DRIVER_AUTHOR "Cesar Miquel <miquel@df.uba.ar>"
#define DRIVER_DESC "USB Rio 500 driver"

#define RIO_MINOR	64

/* stall/wait timeout for rio */
#define NAK_TIMEOUT (HZ)

#define IBUF_SIZE 0x1000

/* Size of the rio buffer */
#define OBUF_SIZE 0x10000

struct rio_usb_data {
        struct usb_device *rio_dev;     /* init: probe_rio */
        unsigned int ifnum;             /* Interface number of the USB device */
        int isopen;                     /* nz if open */
        int present;                    /* Device is present on the bus */
        char *obuf, *ibuf;              /* transfer buffers */
        char bulk_in_ep, bulk_out_ep;   /* Endpoint assignments */
        wait_queue_head_t wait_q;       /* for timeouts */
	struct mutex lock;          /* general race avoidance */
};

static DEFINE_MUTEX(rio500_mutex);
static struct rio_usb_data rio_instance;

static int open_rio(struct inode *inode, struct file *file)
{
	struct rio_usb_data *rio = &rio_instance;

	/* against disconnect() */
	mutex_lock(&rio500_mutex);
	mutex_lock(&(rio->lock));

	if (rio->isopen || !rio->present) {
		mutex_unlock(&(rio->lock));
		mutex_unlock(&rio500_mutex);
		return -EBUSY;
	}
	rio->isopen = 1;

	init_waitqueue_head(&rio->wait_q);

	mutex_unlock(&(rio->lock));

	dev_info(&rio->rio_dev->dev, "Rio opened.\n");
	mutex_unlock(&rio500_mutex);

	return 0;
}

static int close_rio(struct inode *inode, struct file *file)
{
	struct rio_usb_data *rio = &rio_instance;

	/* against disconnect() */
	mutex_lock(&rio500_mutex);
	mutex_lock(&(rio->lock));

	rio->isopen = 0;
	if (!rio->present) {
		/* cleanup has been delayed */
		kfree(rio->ibuf);
		kfree(rio->obuf);
		rio->ibuf = NULL;
		rio->obuf = NULL;
	} else {
		dev_info(&rio->rio_dev->dev, "Rio closed.\n");
	}
	mutex_unlock(&(rio->lock));
	mutex_unlock(&rio500_mutex);
	return 0;
}

static long ioctl_rio(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct RioCommand rio_cmd;
	struct rio_usb_data *rio = &rio_instance;
	void __user *data;
	unsigned char *buffer;
	int result, requesttype;
	int retries;
	int retval=0;

	mutex_lock(&(rio->lock));
        /* Sanity check to make sure rio is connected, powered, etc */
        if (rio->present == 0 || rio->rio_dev == NULL) {
		retval = -ENODEV;
		goto err_out;
	}

	switch (cmd) {
	case RIO_RECV_COMMAND:
		data = (void __user *) arg;
		if (data == NULL)
			break;
		if (copy_from_user(&rio_cmd, data, sizeof(struct RioCommand))) {
			retval = -EFAULT;
			goto err_out;
		}
		if (rio_cmd.length < 0 || rio_cmd.length > PAGE_SIZE) {
			retval = -EINVAL;
			goto err_out;
		}
		buffer = (unsigned char *) __get_free_page(GFP_KERNEL);
		if (buffer == NULL) {
			retval = -ENOMEM;
			goto err_out;
		}
		if (copy_from_user(buffer, rio_cmd.buffer, rio_cmd.length)) {
			retval = -EFAULT;
			free_page((unsigned long) buffer);
			goto err_out;
		}

		requesttype = rio_cmd.requesttype | USB_DIR_IN |
		    USB_TYPE_VENDOR | USB_RECIP_DEVICE;
		dev_dbg(&rio->rio_dev->dev,
			"sending command:reqtype=%0x req=%0x value=%0x index=%0x len=%0x\n",
			requesttype, rio_cmd.request, rio_cmd.value,
			rio_cmd.index, rio_cmd.length);
		/* Send rio control message */
		retries = 3;
		while (retries) {
			result = usb_control_msg(rio->rio_dev,
						 usb_rcvctrlpipe(rio-> rio_dev, 0),
						 rio_cmd.request,
						 requesttype,
						 rio_cmd.value,
						 rio_cmd.index, buffer,
						 rio_cmd.length,
						 jiffies_to_msecs(rio_cmd.timeout));
			if (result == -ETIMEDOUT)
				retries--;
			else if (result < 0) {
				dev_err(&rio->rio_dev->dev,
					"Error executing ioctrl. code = %d\n",
					result);
				retries = 0;
			} else {
				dev_dbg(&rio->rio_dev->dev,
					"Executed ioctl. Result = %d (data=%02x)\n",
					result, buffer[0]);
				if (copy_to_user(rio_cmd.buffer, buffer,
						 rio_cmd.length)) {
					free_page((unsigned long) buffer);
					retval = -EFAULT;
					goto err_out;
				}
				retries = 0;
			}

			/* rio_cmd.buffer contains a raw stream of single byte
			   data which has been returned from rio.  Data is
			   interpreted at application level.  For data that
			   will be cast to data types longer than 1 byte, data
			   will be little_endian and will potentially need to
			   be swapped at the app level */

		}
		free_page((unsigned long) buffer);
		break;

	case RIO_SEND_COMMAND:
		data = (void __user *) arg;
		if (data == NULL)
			break;
		if (copy_from_user(&rio_cmd, data, sizeof(struct RioCommand))) {
			retval = -EFAULT;
			goto err_out;
		}
		if (rio_cmd.length < 0 || rio_cmd.length > PAGE_SIZE) {
			retval = -EINVAL;
			goto err_out;
		}
		buffer = (unsigned char *) __get_free_page(GFP_KERNEL);
		if (buffer == NULL) {
			retval = -ENOMEM;
			goto err_out;
		}
		if (copy_from_user(buffer, rio_cmd.buffer, rio_cmd.length)) {
			free_page((unsigned long)buffer);
			retval = -EFAULT;
			goto err_out;
		}

		requesttype = rio_cmd.requesttype | USB_DIR_OUT |
		    USB_TYPE_VENDOR | USB_RECIP_DEVICE;
		dev_dbg(&rio->rio_dev->dev,
			"sending command: reqtype=%0x req=%0x value=%0x index=%0x len=%0x\n",
			requesttype, rio_cmd.request, rio_cmd.value,
			rio_cmd.index, rio_cmd.length);
		/* Send rio control message */
		retries = 3;
		while (retries) {
			result = usb_control_msg(rio->rio_dev,
						 usb_sndctrlpipe(rio-> rio_dev, 0),
						 rio_cmd.request,
						 requesttype,
						 rio_cmd.value,
						 rio_cmd.index, buffer,
						 rio_cmd.length,
						 jiffies_to_msecs(rio_cmd.timeout));
			if (result == -ETIMEDOUT)
				retries--;
			else if (result < 0) {
				dev_err(&rio->rio_dev->dev,
					"Error executing ioctrl. code = %d\n",
					result);
				retries = 0;
			} else {
				dev_dbg(&rio->rio_dev->dev,
					"Executed ioctl. Result = %d\n", result);
				retries = 0;

			}

		}
		free_page((unsigned long) buffer);
		break;

	default:
		retval = -ENOTTY;
		break;
	}


err_out:
	mutex_unlock(&(rio->lock));
	return retval;
}

static ssize_t
write_rio(struct file *file, const char __user *buffer,
	  size_t count, loff_t * ppos)
{
	DEFINE_WAIT(wait);
	struct rio_usb_data *rio = &rio_instance;

	unsigned long copy_size;
	unsigned long bytes_written = 0;
	unsigned int partial;

	int result = 0;
	int maxretry;
	int errn = 0;
	int intr;

	intr = mutex_lock_interruptible(&(rio->lock));
	if (intr)
		return -EINTR;
        /* Sanity check to make sure rio is connected, powered, etc */
        if (rio->present == 0 || rio->rio_dev == NULL) {
		mutex_unlock(&(rio->lock));
		return -ENODEV;
	}



	do {
		unsigned long thistime;
		char *obuf = rio->obuf;

		thistime = copy_size =
		    (count >= OBUF_SIZE) ? OBUF_SIZE : count;
		if (copy_from_user(rio->obuf, buffer, copy_size)) {
			errn = -EFAULT;
			goto error;
		}
		maxretry = 5;
		while (thistime) {
			if (!rio->rio_dev) {
				errn = -ENODEV;
				goto error;
			}
			if (signal_pending(current)) {
				mutex_unlock(&(rio->lock));
				return bytes_written ? bytes_written : -EINTR;
			}

			result = usb_bulk_msg(rio->rio_dev,
					 usb_sndbulkpipe(rio->rio_dev, 2),
					 obuf, thistime, &partial, 5000);

			dev_dbg(&rio->rio_dev->dev,
				"write stats: result:%d thistime:%lu partial:%u\n",
				result, thistime, partial);

			if (result == -ETIMEDOUT) {	/* NAK - so hold for a while */
				if (!maxretry--) {
					errn = -ETIME;
					goto error;
				}
				prepare_to_wait(&rio->wait_q, &wait, TASK_INTERRUPTIBLE);
				schedule_timeout(NAK_TIMEOUT);
				finish_wait(&rio->wait_q, &wait);
				continue;
			} else if (!result && partial) {
				obuf += partial;
				thistime -= partial;
			} else
				break;
		}
		if (result) {
			dev_err(&rio->rio_dev->dev, "Write Whoops - %x\n",
				result);
			errn = -EIO;
			goto error;
		}
		bytes_written += copy_size;
		count -= copy_size;
		buffer += copy_size;
	} while (count > 0);

	mutex_unlock(&(rio->lock));

	return bytes_written ? bytes_written : -EIO;

error:
	mutex_unlock(&(rio->lock));
	return errn;
}

static ssize_t
read_rio(struct file *file, char __user *buffer, size_t count, loff_t * ppos)
{
	DEFINE_WAIT(wait);
	struct rio_usb_data *rio = &rio_instance;
	ssize_t read_count;
	unsigned int partial;
	int this_read;
	int result;
	int maxretry = 10;
	char *ibuf;
	int intr;

	intr = mutex_lock_interruptible(&(rio->lock));
	if (intr)
		return -EINTR;
	/* Sanity check to make sure rio is connected, powered, etc */
        if (rio->present == 0 || rio->rio_dev == NULL) {
		mutex_unlock(&(rio->lock));
		return -ENODEV;
	}

	ibuf = rio->ibuf;

	read_count = 0;


	while (count > 0) {
		if (signal_pending(current)) {
			mutex_unlock(&(rio->lock));
			return read_count ? read_count : -EINTR;
		}
		if (!rio->rio_dev) {
			mutex_unlock(&(rio->lock));
			return -ENODEV;
		}
		this_read = (count >= IBUF_SIZE) ? IBUF_SIZE : count;

		result = usb_bulk_msg(rio->rio_dev,
				      usb_rcvbulkpipe(rio->rio_dev, 1),
				      ibuf, this_read, &partial,
				      8000);

		dev_dbg(&rio->rio_dev->dev,
			"read stats: result:%d this_read:%u partial:%u\n",
			result, this_read, partial);

		if (partial) {
			count = this_read = partial;
		} else if (result == -ETIMEDOUT || result == 15) {	/* FIXME: 15 ??? */
			if (!maxretry--) {
				mutex_unlock(&(rio->lock));
				dev_err(&rio->rio_dev->dev,
					"read_rio: maxretry timeout\n");
				return -ETIME;
			}
			prepare_to_wait(&rio->wait_q, &wait, TASK_INTERRUPTIBLE);
			schedule_timeout(NAK_TIMEOUT);
			finish_wait(&rio->wait_q, &wait);
			continue;
		} else if (result != -EREMOTEIO) {
			mutex_unlock(&(rio->lock));
			dev_err(&rio->rio_dev->dev,
				"Read Whoops - result:%d partial:%u this_read:%u\n",
				result, partial, this_read);
			return -EIO;
		} else {
			mutex_unlock(&(rio->lock));
			return (0);
		}

		if (this_read) {
			if (copy_to_user(buffer, ibuf, this_read)) {
				mutex_unlock(&(rio->lock));
				return -EFAULT;
			}
			count -= this_read;
			read_count += this_read;
			buffer += this_read;
		}
	}
	mutex_unlock(&(rio->lock));
	return read_count;
}

static const struct file_operations usb_rio_fops = {
	.owner =	THIS_MODULE,
	.read =		read_rio,
	.write =	write_rio,
	.unlocked_ioctl = ioctl_rio,
	.open =		open_rio,
	.release =	close_rio,
	.llseek =	noop_llseek,
};

static struct usb_class_driver usb_rio_class = {
	.name =		"rio500%d",
	.fops =		&usb_rio_fops,
	.minor_base =	RIO_MINOR,
};

static int probe_rio(struct usb_interface *intf,
		     const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct rio_usb_data *rio = &rio_instance;
	int retval = 0;

	mutex_lock(&rio500_mutex);
	if (rio->present) {
		dev_info(&intf->dev, "Second USB Rio at address %d refused\n", dev->devnum);
		retval = -EBUSY;
		goto bail_out;
	} else {
		dev_info(&intf->dev, "USB Rio found at address %d\n", dev->devnum);
	}

	retval = usb_register_dev(intf, &usb_rio_class);
	if (retval) {
		dev_err(&dev->dev,
			"Not able to get a minor for this device.\n");
		retval = -ENOMEM;
		goto bail_out;
	}

	rio->rio_dev = dev;

	if (!(rio->obuf = kmalloc(OBUF_SIZE, GFP_KERNEL))) {
		dev_err(&dev->dev,
			"probe_rio: Not enough memory for the output buffer\n");
		usb_deregister_dev(intf, &usb_rio_class);
		retval = -ENOMEM;
		goto bail_out;
	}
	dev_dbg(&intf->dev, "obuf address:%p\n", rio->obuf);

	if (!(rio->ibuf = kmalloc(IBUF_SIZE, GFP_KERNEL))) {
		dev_err(&dev->dev,
			"probe_rio: Not enough memory for the input buffer\n");
		usb_deregister_dev(intf, &usb_rio_class);
		kfree(rio->obuf);
		retval = -ENOMEM;
		goto bail_out;
	}
	dev_dbg(&intf->dev, "ibuf address:%p\n", rio->ibuf);

	mutex_init(&(rio->lock));

	usb_set_intfdata (intf, rio);
	rio->present = 1;
bail_out:
	mutex_unlock(&rio500_mutex);

	return retval;
}

static void disconnect_rio(struct usb_interface *intf)
{
	struct rio_usb_data *rio = usb_get_intfdata (intf);

	usb_set_intfdata (intf, NULL);
	mutex_lock(&rio500_mutex);
	if (rio) {
		usb_deregister_dev(intf, &usb_rio_class);

		mutex_lock(&(rio->lock));
		if (rio->isopen) {
			rio->isopen = 0;
			/* better let it finish - the release will do whats needed */
			rio->rio_dev = NULL;
			mutex_unlock(&(rio->lock));
			mutex_unlock(&rio500_mutex);
			return;
		}
		kfree(rio->ibuf);
		kfree(rio->obuf);

		dev_info(&intf->dev, "USB Rio disconnected.\n");

		rio->present = 0;
		mutex_unlock(&(rio->lock));
	}
	mutex_unlock(&rio500_mutex);
}

static const struct usb_device_id rio_table[] = {
	{ USB_DEVICE(0x0841, 1) }, 		/* Rio 500 */
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, rio_table);

static struct usb_driver rio_driver = {
	.name =		"rio500",
	.probe =	probe_rio,
	.disconnect =	disconnect_rio,
	.id_table =	rio_table,
};

module_usb_driver(rio_driver);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

