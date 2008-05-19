/*!

	\file		smschar.c

	\brief		Implementation of smscore client for cdev based access

    \par 		Copyright (c), 2005-2008 Siano Mobile Silicon, Inc.

    \par 		This program is free software; you can redistribute it and/or modify
			it under the terms of the GNU General Public License version 3 as
			published by the Free Software Foundation;

			Software distributed under the License is distributed on an "AS
			IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
			implied.

	\author		Anatoly Greenblat

*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>		/* printk() */
#include <linux/fs.h>			/* everything... */
#include <linux/types.h>		/* size_t */
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/system.h>			/* cli(), *_flags */
#include <asm/uaccess.h>		/* copy_*_user */

#include "smskdefs.h" // page, scatterlist, kmutex
#include "smscoreapi.h"
#include "smstypes.h"

#include "smscharioctl.h"

#define SMS_CHR_MAX_Q_LEN	10 // max number of packets allowed to be pending on queue
#define SMSCHAR_NR_DEVS		7

typedef struct _smschar_device
{
	struct cdev			cdev;				//!<  Char device structure - kernel's device model representation

	wait_queue_head_t	waitq;					/* Processes waiting */
	spinlock_t			lock;				//!< critical section
	int					pending_count;
	struct list_head	pending_data;		//!< list of pending data

	smscore_buffer_t	*currentcb;

	int					device_index;

	smscore_device_t	*coredev;
	smscore_client_t	*smsclient;
} smschar_device_t;

//!  Holds the major number of the device node. may be changed at load time.
int smschar_major = 251;

//!  Holds the first minor number of the device node. may be changed at load time.
int smschar_minor = 0;

// macros that allow the load time parameters change
module_param ( smschar_major, int, S_IRUGO );
module_param ( smschar_minor, int, S_IRUGO );

#ifdef SMSCHAR_DEBUG

	#undef PERROR
#  define PERROR(fmt, args...) printk( KERN_INFO "smschar error: line %d- %s(): " fmt,__LINE__,  __FUNCTION__, ## args)
	#undef PWARNING
#  define PWARNING(fmt, args...) printk( KERN_INFO "smschar warning: line %d- %s(): " fmt,__LINE__,  __FUNCTION__, ## args)
	#undef PDEBUG					/* undef it, just in case */
#  define PDEBUG(fmt, args...)	printk( KERN_INFO "smschar - %s(): " fmt, __FUNCTION__, ## args)

#else /* not debugging: nothing */

	#define PDEBUG(fmt, args...)
	#define PERROR(fmt, args...)
	#define PWARNING(fmt, args...)

#endif

smschar_device_t smschar_devices[SMSCHAR_NR_DEVS];
static int g_smschar_inuse = 0;

/**
 * unregisters sms client and returns all queued buffers
 *
 * @param dev pointer to the client context (smschar parameters block)
 *
 */
void smschar_unregister_client(smschar_device_t* dev)
{
	unsigned long flags;

	if (dev->coredev && dev->smsclient)
	{
		wake_up_interruptible(&dev->waitq);

		spin_lock_irqsave(&dev->lock, flags);

		while (!list_empty(&dev->pending_data))
		{
			smscore_buffer_t *cb = (smscore_buffer_t *) dev->pending_data.next;
			list_del(&cb->entry);

			smscore_putbuffer(dev->coredev, cb);

			dev->pending_count --;
		}

		if (dev->currentcb)
		{
			smscore_putbuffer(dev->coredev, dev->currentcb);
			dev->currentcb = NULL;
			dev->pending_count --;
		}

		smscore_unregister_client(dev->smsclient);
		dev->smsclient = NULL;

		spin_unlock_irqrestore(&dev->lock, flags);
	}
}

/**
 * queues incoming buffers into buffers queue
 *
 * @param context pointer to the client context (smschar parameters block)
 * @param cb pointer to incoming buffer descriptor
 *
 * @return 0 on success, <0 on queue overflow.
 */
int smschar_onresponse(void *context, smscore_buffer_t *cb)
{
	smschar_device_t *dev = context;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);

	if (dev->pending_count > SMS_CHR_MAX_Q_LEN)
	{
		spin_unlock_irqrestore(&dev->lock, flags);
		return -EBUSY;
	}

	dev->pending_count ++;

	// if data channel, remove header
	if (dev->device_index)
	{
		cb->size -= sizeof(SmsMsgHdr_ST);
		cb->offset += sizeof(SmsMsgHdr_ST);
	}

	list_add_tail(&cb->entry, &dev->pending_data);
	spin_unlock_irqrestore(&dev->lock, flags);

	if (waitqueue_active(&dev->waitq))
		wake_up_interruptible(&dev->waitq);

	return 0;
}

/**
 * handles device removal event
 *
 * @param context pointer to the client context (smschar parameters block)
 *
 */
void smschar_onremove(void *context)
{
	smschar_device_t *dev = (smschar_device_t *) context;

	smschar_unregister_client(dev);
	dev->coredev = NULL;
}

/**
 * registers client associated with the node
 *
 * @param inode Inode concerned.
 * @param file File concerned.
 *
 * @return 0 on success, <0 on error.
 */
int smschar_open (struct inode *inode, struct file *file)
{
	smschar_device_t *dev = container_of(inode->i_cdev, smschar_device_t, cdev);
	int rc = -ENODEV;

	PDEBUG("entering index %d\n", dev->device_index);

	if (dev->coredev)
	{
		smsclient_params_t params;

		params.initial_id = dev->device_index ? dev->device_index : SMS_HOST_LIB;
		params.data_type = dev->device_index ? MSG_SMS_DAB_CHANNEL : 0;
		params.onresponse_handler = smschar_onresponse;
		params.onremove_handler = smschar_onremove;
		params.context = dev;

		rc = smscore_register_client(dev->coredev, &params, &dev->smsclient);
		if (!rc)
		{
			file->private_data = dev;
		}
	}

	PDEBUG("exiting, rc %d\n", rc);

	return rc;
}

/**
 * unregisters client associated with the node
 *
 * @param inode Inode concerned.
 * @param file File concerned.
 *
 */
int smschar_release(struct inode *inode, struct file *file)
{
	smschar_unregister_client(file->private_data);

	PDEBUG("exiting\n");

	return 0;
}

/**
 * copies data from buffers in incoming queue into a user buffer
 *
 * @param file File structure.
 * @param buf Source buffer.
 * @param count Size of source buffer.
 * @param f_pos Position in file (ignored).
 *
 * @return Number of bytes read, or <0 on error.
 */
ssize_t smschar_read ( struct file * file, char __user * buf, size_t count, loff_t * f_pos )
{
	smschar_device_t *dev = file->private_data;
	unsigned long flags;
	int copied = 0;

	if (!dev->coredev || !dev->smsclient)
	{
		PERROR("no client\n");
		return -ENODEV;
	}

	while (copied != count)
	{
		if (0 > wait_event_interruptible(dev->waitq, !list_empty(&dev->pending_data)))
		{
			PERROR("wait_event_interruptible error\n");
			return -ENODEV;
		}

		if (!dev->smsclient)
		{
			PERROR("no client\n");
			return -ENODEV;
		}

		spin_lock_irqsave(&dev->lock, flags);

		while (!list_empty(&dev->pending_data) && (copied != count))
		{
			smscore_buffer_t *cb = (smscore_buffer_t *) dev->pending_data.next;
			int actual_size = min(((int) count - copied), cb->size);

			copy_to_user(&buf[copied], &((char*)cb->p)[cb->offset], actual_size);

			copied += actual_size;
			cb->offset += actual_size;
			cb->size -= actual_size;

			if (!cb->size)
			{
				list_del(&cb->entry);
				smscore_putbuffer(dev->coredev, cb);

				dev->pending_count --;
			}
		}

		spin_unlock_irqrestore(&dev->lock, flags);
	}

	return copied;
}

/**
 * sends the buffer to the associated device
 *
 * @param file File structure.
 * @param buf Source buffer.
 * @param count Size of source buffer.
 * @param f_pos Position in file (ignored).
 *
 * @return Number of bytes read, or <0 on error.
 */
ssize_t smschar_write(struct file *file, const char __user *buf, size_t count, loff_t *f_pos)
{
	smschar_device_t *dev = file->private_data;
	void *buffer;

	if (!dev->smsclient)
	{
		PERROR("no client\n");
		return -ENODEV;
	}

	buffer = kmalloc(ALIGN(count, SMS_ALLOC_ALIGNMENT) + SMS_DMA_ALIGNMENT, GFP_KERNEL | GFP_DMA);
	if (buffer)
	{
		void *msg_buffer = (void*) SMS_ALIGN_ADDRESS(buffer);

		if (!copy_from_user(msg_buffer, buf, count))
			smsclient_sendrequest(dev->smsclient, msg_buffer, count);
		else
			count = 0;

		kfree(buffer);
	}

	return count;
}

int smschar_mmap(struct file *file, struct vm_area_struct *vma)
{
	smschar_device_t *dev = file->private_data;
	return smscore_map_common_buffer(dev->coredev, vma);
}

/**
 * waits until buffer inserted into a queue. when inserted buffer offset are reported
 * to the calling process. previously reported buffer is returned to smscore pool
 *
 * @param dev pointer to smschar parameters block
 * @param touser pointer to a structure that receives incoming buffer offsets
 *
 * @return 0 on success, <0 on error.
 */
int smschar_wait_get_buffer(smschar_device_t* dev, smschar_buffer_t* touser)
{
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&dev->lock, flags);

	if (dev->currentcb)
	{
		smscore_putbuffer(dev->coredev, dev->currentcb);
		dev->currentcb = NULL;
		dev->pending_count --;
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	rc = wait_event_interruptible(dev->waitq, !list_empty(&dev->pending_data));
	if (rc < 0)
	{
		PERROR("wait_event_interruptible error\n");
		return rc;
	}

	if (!dev->smsclient)
	{
		PERROR("no client\n");
		return -ENODEV;
	}

	spin_lock_irqsave(&dev->lock, flags);

	if (!list_empty(&dev->pending_data))
	{
		smscore_buffer_t *cb = (smscore_buffer_t *) dev->pending_data.next;

		touser->offset = cb->offset_in_common + cb->offset;
		touser->size = cb->size;

		list_del(&cb->entry);

		dev->currentcb = cb;
	}
	else
	{
		touser->offset = 0;
		touser->size = 0;
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

int smschar_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	smschar_device_t *dev = file->private_data;
	void __user *up = (void __user *) arg;

	if (!dev->coredev || !dev->smsclient)
	{
		PERROR("no client\n");
		return -ENODEV;
	}

	switch(cmd)
	{
		case SMSCHAR_SET_DEVICE_MODE:
			return smscore_set_device_mode(dev->coredev, (int) arg);

		case SMSCHAR_GET_DEVICE_MODE:
		{
			if (put_user(smscore_get_device_mode(dev->coredev), (int*) up))
				return -EFAULT;

			break;
		}

		case SMSCHAR_GET_BUFFER_SIZE:
		{
			if (put_user(smscore_get_common_buffer_size(dev->coredev), (int*) up))
				return -EFAULT;

			break;
		}

		case SMSCHAR_WAIT_GET_BUFFER:
		{
			smschar_buffer_t touser;
			int rc;

			rc = smschar_wait_get_buffer(dev, &touser);
			if (rc < 0)
				return rc;

			if (copy_to_user(up, &touser, sizeof(smschar_buffer_t)))
				return -EFAULT;

			break;
		}

		default:
			return -ENOIOCTLCMD;
	}

	return 0;
}

struct file_operations smschar_fops =
{
	.owner = THIS_MODULE,
	.read = smschar_read,
	.write = smschar_write,
	.open = smschar_open,
	.release = smschar_release,
	.mmap = smschar_mmap,
	.ioctl = smschar_ioctl,
};

static int smschar_setup_cdev ( smschar_device_t *dev, int index )
{
	int rc, devno = MKDEV ( smschar_major, smschar_minor + index );

	cdev_init ( &dev->cdev, &smschar_fops );

	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &smschar_fops;

	kobject_set_name(&dev->cdev.kobj, "Siano_sms%d", index);

	rc = cdev_add ( &dev->cdev, devno, 1 );

	PDEBUG("exiting %p %d, rc %d\n", dev, index, rc);

	return rc;
}

/**
 * smschar callback that called when device plugged in/out. the function
 * register or unregisters char device interface according to plug in/out
 *
 * @param coredev pointer to device that is being plugged in/out
 * @param device pointer to system device object
 * @param arrival 1 on plug-on, 0 othewise
 *
 * @return 0 on success, <0 on error.
 */
int smschar_hotplug(smscore_device_t* coredev, struct device* device, int arrival)
{
	int rc = 0, i;

	PDEBUG("entering %d\n", arrival);

	if (arrival)
	{
		// currently only 1 instance supported
		if (!g_smschar_inuse)
		{
			/* data notification callbacks assignment */
			memset ( smschar_devices, 0, SMSCHAR_NR_DEVS * sizeof ( smschar_device_t ) );

			/* Initialize each device. */
			for (i = 0; i < SMSCHAR_NR_DEVS; i++)
			{
				smschar_setup_cdev ( &smschar_devices[i], i );

				INIT_LIST_HEAD(&smschar_devices[i].pending_data);
				spin_lock_init(&smschar_devices[i].lock);
				init_waitqueue_head(&smschar_devices[i].waitq);

				smschar_devices[i].coredev = coredev;
				smschar_devices[i].device_index = i;
			}

			g_smschar_inuse = 1;
		}
	}
	else
	{
		// currently only 1 instance supported
		if (g_smschar_inuse)
		{
			/* Get rid of our char dev entries */
			for(i = 0; i < SMSCHAR_NR_DEVS; i++)
				cdev_del(&smschar_devices[i].cdev);

			g_smschar_inuse = 0;
		}
	}

	PDEBUG("exiting, rc %d\n", rc);

	return rc;					/* succeed */
}

int smschar_initialize(void)
{
	dev_t devno = MKDEV ( smschar_major, smschar_minor );
	int rc;

	if(smschar_major)
	{
		rc = register_chrdev_region ( devno, SMSCHAR_NR_DEVS, "smschar" );
	}
	else
	{
		rc = alloc_chrdev_region ( &devno, smschar_minor, SMSCHAR_NR_DEVS, "smschar" );
		smschar_major = MAJOR ( devno );
	}

	if (rc < 0)
	{
		PWARNING (  "smschar: can't get major %d\n", smschar_major );
		return rc;
	}

	return smscore_register_hotplug(smschar_hotplug);
}

void smschar_terminate(void)
{
	dev_t devno = MKDEV ( smschar_major, smschar_minor );

	unregister_chrdev_region(devno, SMSCHAR_NR_DEVS);
	smscore_unregister_hotplug(smschar_hotplug);
}
