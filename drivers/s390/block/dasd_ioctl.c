/*
 * File...........: linux/drivers/s390/block/dasd_ioctl.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * i/o controls for the dasd driver.
 */
#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/blkpg.h>

#include <asm/ccwdev.h>
#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_ioctl:"

#include "dasd_int.h"

/*
 * SECTION: ioctl functions.
 */
static struct list_head dasd_ioctl_list = LIST_HEAD_INIT(dasd_ioctl_list);

/*
 * Find the ioctl with number no.
 */
static struct dasd_ioctl *
dasd_find_ioctl(int no)
{
	struct dasd_ioctl *ioctl;

	list_for_each_entry (ioctl, &dasd_ioctl_list, list)
		if (ioctl->no == no)
			return ioctl;
	return NULL;
}

/*
 * Register ioctl with number no.
 */
int
dasd_ioctl_no_register(struct module *owner, int no, dasd_ioctl_fn_t handler)
{
	struct dasd_ioctl *new;
	if (dasd_find_ioctl(no))
		return -EBUSY;
	new = kmalloc(sizeof (struct dasd_ioctl), GFP_KERNEL);
	if (new == NULL)
		return -ENOMEM;
	new->owner = owner;
	new->no = no;
	new->handler = handler;
	list_add(&new->list, &dasd_ioctl_list);
	return 0;
}

/*
 * Deregister ioctl with number no.
 */
int
dasd_ioctl_no_unregister(struct module *owner, int no, dasd_ioctl_fn_t handler)
{
	struct dasd_ioctl *old = dasd_find_ioctl(no);
	if (old == NULL)
		return -ENOENT;
	if (old->no != no || old->handler != handler || owner != old->owner)
		return -EINVAL;
	list_del(&old->list);
	kfree(old);
	return 0;
}

int
dasd_ioctl(struct inode *inp, struct file *filp,
	   unsigned int no, unsigned long data)
{
	struct block_device *bdev = inp->i_bdev;
	struct dasd_device *device = bdev->bd_disk->private_data;
	struct dasd_ioctl *ioctl;
	const char *dir;
	int rc;

	if ((_IOC_DIR(no) != _IOC_NONE) && (data == 0)) {
		PRINT_DEBUG("empty data ptr");
		return -EINVAL;
	}
	dir = _IOC_DIR (no) == _IOC_NONE ? "0" :
		_IOC_DIR (no) == _IOC_READ ? "r" :
		_IOC_DIR (no) == _IOC_WRITE ? "w" : 
		_IOC_DIR (no) == (_IOC_READ | _IOC_WRITE) ? "rw" : "u";
	DBF_DEV_EVENT(DBF_DEBUG, device,
		      "ioctl 0x%08x %s'0x%x'%d(%d) with data %8lx", no,
		      dir, _IOC_TYPE(no), _IOC_NR(no), _IOC_SIZE(no), data);
	/* Search for ioctl no in the ioctl list. */
	list_for_each_entry(ioctl, &dasd_ioctl_list, list) {
		if (ioctl->no == no) {
			/* Found a matching ioctl. Call it. */
			if (!try_module_get(ioctl->owner))
				continue;
			rc = ioctl->handler(bdev, no, data);
			module_put(ioctl->owner);
			return rc;
		}
	}
	/* No ioctl with number no. */
	DBF_DEV_EVENT(DBF_INFO, device,
		      "unknown ioctl 0x%08x=%s'0x%x'%d(%d) data %8lx", no,
		      dir, _IOC_TYPE(no), _IOC_NR(no), _IOC_SIZE(no), data);
	return -EINVAL;
}

long
dasd_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int rval;

	lock_kernel();
	rval = dasd_ioctl(filp->f_dentry->d_inode, filp, cmd, arg);
	unlock_kernel();

	return (rval == -EINVAL) ? -ENOIOCTLCMD : rval;
}

static int
dasd_ioctl_api_version(struct block_device *bdev, int no, long args)
{
	int ver = DASD_API_VERSION;
	return put_user(ver, (int __user *) args);
}

/*
 * Enable device.
 * used by dasdfmt after BIODASDDISABLE to retrigger blocksize detection
 */
static int
dasd_ioctl_enable(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	device = bdev->bd_disk->private_data;
	if (device == NULL)
		return -ENODEV;
	dasd_enable_device(device);
	/* Formatting the dasd device can change the capacity. */
	down(&bdev->bd_sem);
	i_size_write(bdev->bd_inode, (loff_t)get_capacity(device->gdp) << 9);
	up(&bdev->bd_sem);
	return 0;
}

/*
 * Disable device.
 * Used by dasdfmt. Disable I/O operations but allow ioctls.
 */
static int
dasd_ioctl_disable(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	device = bdev->bd_disk->private_data;
	if (device == NULL)
		return -ENODEV;
	/*
	 * Man this is sick. We don't do a real disable but only downgrade
	 * the device to DASD_STATE_BASIC. The reason is that dasdfmt uses
	 * BIODASDDISABLE to disable accesses to the device via the block
	 * device layer but it still wants to do i/o on the device by
	 * using the BIODASDFMT ioctl. Therefore the correct state for the
	 * device is DASD_STATE_BASIC that allows to do basic i/o.
	 */
	dasd_set_target_state(device, DASD_STATE_BASIC);
	/*
	 * Set i_size to zero, since read, write, etc. check against this
	 * value.
	 */
	down(&bdev->bd_sem);
	i_size_write(bdev->bd_inode, 0);
	up(&bdev->bd_sem);
	return 0;
}

/*
 * Quiesce device.
 */
static int
dasd_ioctl_quiesce(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;
	unsigned long flags;
	
	if (!capable (CAP_SYS_ADMIN))
		return -EACCES;
	
	device = bdev->bd_disk->private_data;
	if (device == NULL)
		return -ENODEV;
	
	DEV_MESSAGE (KERN_DEBUG, device, "%s",
		     "Quiesce IO on device");
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	device->stopped |= DASD_STOPPED_QUIESCE;
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	return 0;
}


/*
 * Quiesce device.
 */
static int
dasd_ioctl_resume(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;
	unsigned long flags;
	
	if (!capable (CAP_SYS_ADMIN)) 
		return -EACCES;

	device = bdev->bd_disk->private_data;
	if (device == NULL)
		return -ENODEV;

	DEV_MESSAGE (KERN_DEBUG, device, "%s",
		     "resume IO on device");
	
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	device->stopped &= ~DASD_STOPPED_QUIESCE;
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);

	dasd_schedule_bh (device);
	return 0;
}

/*
 * performs formatting of _device_ according to _fdata_
 * Note: The discipline's format_function is assumed to deliver formatting
 * commands to format a single unit of the device. In terms of the ECKD
 * devices this means CCWs are generated to format a single track.
 */
static int
dasd_format(struct dasd_device * device, struct format_data_t * fdata)
{
	struct dasd_ccw_req *cqr;
	int rc;

	if (device->discipline->format_device == NULL)
		return -EPERM;

	if (device->state != DASD_STATE_BASIC) {
		DEV_MESSAGE(KERN_WARNING, device, "%s",
			    "dasd_format: device is not disabled! ");
		return -EBUSY;
	}

	DBF_DEV_EVENT(DBF_NOTICE, device,
		      "formatting units %d to %d (%d B blocks) flags %d",
		      fdata->start_unit,
		      fdata->stop_unit, fdata->blksize, fdata->intensity);

	/* Since dasdfmt keeps the device open after it was disabled,
	 * there still exists an inode for this device.
	 * We must update i_blkbits, otherwise we might get errors when
	 * enabling the device later.
	 */
	if (fdata->start_unit == 0) {
		struct block_device *bdev = bdget_disk(device->gdp, 0);
		bdev->bd_inode->i_blkbits = blksize_bits(fdata->blksize);
		bdput(bdev);
	}

	while (fdata->start_unit <= fdata->stop_unit) {
		cqr = device->discipline->format_device(device, fdata);
		if (IS_ERR(cqr))
			return PTR_ERR(cqr);
		rc = dasd_sleep_on_interruptible(cqr);
		dasd_sfree_request(cqr, cqr->device);
		if (rc) {
			if (rc != -ERESTARTSYS)
				DEV_MESSAGE(KERN_ERR, device,
					    " Formatting of unit %d failed "
					    "with rc = %d",
					    fdata->start_unit, rc);
			return rc;
		}
		fdata->start_unit++;
	}
	return 0;
}

/*
 * Format device.
 */
static int
dasd_ioctl_format(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;
	struct format_data_t fdata;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!args)
		return -EINVAL;
	/* fdata == NULL is no longer a valid arg to dasd_format ! */
	device = bdev->bd_disk->private_data;

	if (device == NULL)
		return -ENODEV;

	if (device->features & DASD_FEATURE_READONLY)
		return -EROFS;
	if (copy_from_user(&fdata, (void __user *) args,
			   sizeof (struct format_data_t)))
		return -EFAULT;
	if (bdev != bdev->bd_contains) {
		DEV_MESSAGE(KERN_WARNING, device, "%s",
			    "Cannot low-level format a partition");
		return -EINVAL;
	}
	return dasd_format(device, &fdata);
}

#ifdef CONFIG_DASD_PROFILE
/*
 * Reset device profile information
 */
static int
dasd_ioctl_reset_profile(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	device = bdev->bd_disk->private_data;
	if (device == NULL)
		return -ENODEV;

	memset(&device->profile, 0, sizeof (struct dasd_profile_info_t));
	return 0;
}

/*
 * Return device profile information
 */
static int
dasd_ioctl_read_profile(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;

	device = bdev->bd_disk->private_data;
	if (device == NULL)
		return -ENODEV;

	if (dasd_profile_level == DASD_PROFILE_OFF)
		return -EIO;

	if (copy_to_user((long __user *) args, (long *) &device->profile,
			 sizeof (struct dasd_profile_info_t)))
		return -EFAULT;
	return 0;
}
#else
static int
dasd_ioctl_reset_profile(struct block_device *bdev, int no, long args)
{
	return -ENOSYS;
}

static int
dasd_ioctl_read_profile(struct block_device *bdev, int no, long args)
{
	return -ENOSYS;
}
#endif

/*
 * Return dasd information. Used for BIODASDINFO and BIODASDINFO2.
 */
static int
dasd_ioctl_information(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;
	struct dasd_information2_t *dasd_info;
	unsigned long flags;
	int rc;
	struct ccw_device *cdev;

	device = bdev->bd_disk->private_data;
	if (device == NULL)
		return -ENODEV;

	if (!device->discipline->fill_info)
		return -EINVAL;

	dasd_info = kmalloc(sizeof(struct dasd_information2_t), GFP_KERNEL);
	if (dasd_info == NULL)
		return -ENOMEM;

	rc = device->discipline->fill_info(device, dasd_info);
	if (rc) {
		kfree(dasd_info);
		return rc;
	}

	cdev = device->cdev;

	dasd_info->devno = _ccw_device_get_device_number(device->cdev);
	dasd_info->schid = _ccw_device_get_subchannel_number(device->cdev);
	dasd_info->cu_type = cdev->id.cu_type;
	dasd_info->cu_model = cdev->id.cu_model;
	dasd_info->dev_type = cdev->id.dev_type;
	dasd_info->dev_model = cdev->id.dev_model;
	dasd_info->status = device->state;
	/*
	 * The open_count is increased for every opener, that includes
	 * the blkdev_get in dasd_scan_partitions.
	 * This must be hidden from user-space.
	 */
	dasd_info->open_count = atomic_read(&device->open_count);
	if (!device->bdev)
		dasd_info->open_count++;
	
	/*
	 * check if device is really formatted
	 * LDL / CDL was returned by 'fill_info'
	 */
	if ((device->state < DASD_STATE_READY) ||
	    (dasd_check_blocksize(device->bp_block)))
		dasd_info->format = DASD_FORMAT_NONE;

	dasd_info->features |=
		((device->features & DASD_FEATURE_READONLY) != 0);

	if (device->discipline)
		memcpy(dasd_info->type, device->discipline->name, 4);
	else
		memcpy(dasd_info->type, "none", 4);
	dasd_info->req_queue_len = 0;
	dasd_info->chanq_len = 0;
	if (device->request_queue->request_fn) {
		struct list_head *l;
#ifdef DASD_EXTENDED_PROFILING
		{
			struct list_head *l;
			spin_lock_irqsave(&device->lock, flags);
			list_for_each(l, &device->request_queue->queue_head)
				dasd_info->req_queue_len++;
			spin_unlock_irqrestore(&device->lock, flags);
		}
#endif				/* DASD_EXTENDED_PROFILING */
		spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
		list_for_each(l, &device->ccw_queue)
			dasd_info->chanq_len++;
		spin_unlock_irqrestore(get_ccwdev_lock(device->cdev),
				       flags);
	}

	rc = 0;
	if (copy_to_user((long __user *) args, (long *) dasd_info,
			 ((no == (unsigned int) BIODASDINFO2) ?
			  sizeof (struct dasd_information2_t) :
			  sizeof (struct dasd_information_t))))
		rc = -EFAULT;
	kfree(dasd_info);
	return rc;
}

/*
 * Set read only
 */
static int
dasd_ioctl_set_ro(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;
	int intval, rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (bdev != bdev->bd_contains)
		// ro setting is not allowed for partitions
		return -EINVAL;
	if (get_user(intval, (int __user *) args))
		return -EFAULT;
	device =  bdev->bd_disk->private_data;
	if (device == NULL)
		return -ENODEV;

	set_disk_ro(bdev->bd_disk, intval);
	rc = dasd_set_feature(device->cdev, DASD_FEATURE_READONLY, intval);

	return rc;
}

/*
 * List of static ioctls.
 */
static struct { int no; dasd_ioctl_fn_t fn; } dasd_ioctls[] =
{
	{ BIODASDDISABLE, dasd_ioctl_disable },
	{ BIODASDENABLE, dasd_ioctl_enable },
	{ BIODASDQUIESCE, dasd_ioctl_quiesce },
	{ BIODASDRESUME, dasd_ioctl_resume },
	{ BIODASDFMT, dasd_ioctl_format },
	{ BIODASDINFO, dasd_ioctl_information },
	{ BIODASDINFO2, dasd_ioctl_information },
	{ BIODASDPRRD, dasd_ioctl_read_profile },
	{ BIODASDPRRST, dasd_ioctl_reset_profile },
	{ BLKROSET, dasd_ioctl_set_ro },
	{ DASDAPIVER, dasd_ioctl_api_version },
	{ -1, NULL }
};

int
dasd_ioctl_init(void)
{
	int i;

	for (i = 0; dasd_ioctls[i].no != -1; i++)
		dasd_ioctl_no_register(NULL, dasd_ioctls[i].no,
				       dasd_ioctls[i].fn);
	return 0;

}

void
dasd_ioctl_exit(void)
{
	int i;

	for (i = 0; dasd_ioctls[i].no != -1; i++)
		dasd_ioctl_no_unregister(NULL, dasd_ioctls[i].no,
					 dasd_ioctls[i].fn);

}

EXPORT_SYMBOL(dasd_ioctl_no_register);
EXPORT_SYMBOL(dasd_ioctl_no_unregister);
