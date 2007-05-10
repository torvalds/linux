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
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/blkpg.h>

#include <asm/ccwdev.h>
#include <asm/cmb.h>
#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_ioctl:"

#include "dasd_int.h"


static int
dasd_ioctl_api_version(void __user *argp)
{
	int ver = DASD_API_VERSION;
	return put_user(ver, (int __user *)argp);
}

/*
 * Enable device.
 * used by dasdfmt after BIODASDDISABLE to retrigger blocksize detection
 */
static int
dasd_ioctl_enable(struct block_device *bdev)
{
	struct dasd_device *device = bdev->bd_disk->private_data;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	dasd_enable_device(device);
	/* Formatting the dasd device can change the capacity. */
	mutex_lock(&bdev->bd_mutex);
	i_size_write(bdev->bd_inode, (loff_t)get_capacity(device->gdp) << 9);
	mutex_unlock(&bdev->bd_mutex);
	return 0;
}

/*
 * Disable device.
 * Used by dasdfmt. Disable I/O operations but allow ioctls.
 */
static int
dasd_ioctl_disable(struct block_device *bdev)
{
	struct dasd_device *device = bdev->bd_disk->private_data;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

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
	mutex_lock(&bdev->bd_mutex);
	i_size_write(bdev->bd_inode, 0);
	mutex_unlock(&bdev->bd_mutex);
	return 0;
}

/*
 * Quiesce device.
 */
static int
dasd_ioctl_quiesce(struct dasd_device *device)
{
	unsigned long flags;

	if (!capable (CAP_SYS_ADMIN))
		return -EACCES;

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
dasd_ioctl_resume(struct dasd_device *device)
{
	unsigned long flags;

	if (!capable (CAP_SYS_ADMIN))
		return -EACCES;

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
dasd_ioctl_format(struct block_device *bdev, void __user *argp)
{
	struct dasd_device *device = bdev->bd_disk->private_data;
	struct format_data_t fdata;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!argp)
		return -EINVAL;

	if (device->features & DASD_FEATURE_READONLY)
		return -EROFS;
	if (copy_from_user(&fdata, argp, sizeof(struct format_data_t)))
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
dasd_ioctl_reset_profile(struct dasd_device *device)
{
	memset(&device->profile, 0, sizeof (struct dasd_profile_info_t));
	return 0;
}

/*
 * Return device profile information
 */
static int
dasd_ioctl_read_profile(struct dasd_device *device, void __user *argp)
{
	if (dasd_profile_level == DASD_PROFILE_OFF)
		return -EIO;
	if (copy_to_user(argp, &device->profile,
			 sizeof (struct dasd_profile_info_t)))
		return -EFAULT;
	return 0;
}
#else
static int
dasd_ioctl_reset_profile(struct dasd_device *device)
{
	return -ENOSYS;
}

static int
dasd_ioctl_read_profile(struct dasd_device *device, void __user *argp)
{
	return -ENOSYS;
}
#endif

/*
 * Return dasd information. Used for BIODASDINFO and BIODASDINFO2.
 */
static int
dasd_ioctl_information(struct dasd_device *device,
		unsigned int cmd, void __user *argp)
{
	struct dasd_information2_t *dasd_info;
	unsigned long flags;
	int rc;
	struct ccw_device *cdev;
	struct ccw_dev_id dev_id;

	if (!device->discipline->fill_info)
		return -EINVAL;

	dasd_info = kzalloc(sizeof(struct dasd_information2_t), GFP_KERNEL);
	if (dasd_info == NULL)
		return -ENOMEM;

	rc = device->discipline->fill_info(device, dasd_info);
	if (rc) {
		kfree(dasd_info);
		return rc;
	}

	cdev = device->cdev;
	ccw_device_get_id(cdev, &dev_id);

	dasd_info->devno = dev_id.devno;
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
	if (copy_to_user(argp, dasd_info,
			 ((cmd == (unsigned int) BIODASDINFO2) ?
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
dasd_ioctl_set_ro(struct block_device *bdev, void __user *argp)
{
	struct dasd_device *device =  bdev->bd_disk->private_data;
	int intval;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (bdev != bdev->bd_contains)
		// ro setting is not allowed for partitions
		return -EINVAL;
	if (get_user(intval, (int __user *)argp))
		return -EFAULT;

	set_disk_ro(bdev->bd_disk, intval);
	return dasd_set_feature(device->cdev, DASD_FEATURE_READONLY, intval);
}

static int
dasd_ioctl_readall_cmb(struct dasd_device *device, unsigned int cmd,
		unsigned long arg)
{
	struct cmbdata __user *argp = (void __user *) arg;
	size_t size = _IOC_SIZE(cmd);
	struct cmbdata data;
	int ret;

	ret = cmf_readall(device->cdev, &data);
	if (!ret && copy_to_user(argp, &data, min(size, sizeof(*argp))))
		return -EFAULT;
	return ret;
}

int
dasd_ioctl(struct inode *inode, struct file *file,
	   unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	struct dasd_device *device = bdev->bd_disk->private_data;
	void __user *argp = (void __user *)arg;

	if (!device)
                return -ENODEV;

	if ((_IOC_DIR(cmd) != _IOC_NONE) && !arg) {
		PRINT_DEBUG("empty data ptr");
		return -EINVAL;
	}

	switch (cmd) {
	case BIODASDDISABLE:
		return dasd_ioctl_disable(bdev);
	case BIODASDENABLE:
		return dasd_ioctl_enable(bdev);
	case BIODASDQUIESCE:
		return dasd_ioctl_quiesce(device);
	case BIODASDRESUME:
		return dasd_ioctl_resume(device);
	case BIODASDFMT:
		return dasd_ioctl_format(bdev, argp);
	case BIODASDINFO:
		return dasd_ioctl_information(device, cmd, argp);
	case BIODASDINFO2:
		return dasd_ioctl_information(device, cmd, argp);
	case BIODASDPRRD:
		return dasd_ioctl_read_profile(device, argp);
	case BIODASDPRRST:
		return dasd_ioctl_reset_profile(device);
	case BLKROSET:
		return dasd_ioctl_set_ro(bdev, argp);
	case DASDAPIVER:
		return dasd_ioctl_api_version(argp);
	case BIODASDCMFENABLE:
		return enable_cmf(device->cdev);
	case BIODASDCMFDISABLE:
		return disable_cmf(device->cdev);
	case BIODASDREADALLCMB:
		return dasd_ioctl_readall_cmb(device, cmd, arg);
	default:
		/* if the discipline has an ioctl method try it. */
		if (device->discipline->ioctl) {
			int rval = device->discipline->ioctl(device, cmd, argp);
			if (rval != -ENOIOCTLCMD)
				return rval;
		}

		return -EINVAL;
	}
}

long
dasd_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int rval;

	lock_kernel();
	rval = dasd_ioctl(filp->f_path.dentry->d_inode, filp, cmd, arg);
	unlock_kernel();

	return (rval == -EINVAL) ? -ENOIOCTLCMD : rval;
}
