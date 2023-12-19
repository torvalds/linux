// SPDX-License-Identifier: GPL-2.0
/*
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 1999, 2001
 *
 * i/o controls for the dasd driver.
 */

#define KMSG_COMPONENT "dasd"

#include <linux/interrupt.h>
#include <linux/compat.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#include <asm/ccwdev.h>
#include <asm/schid.h>
#include <asm/cmb.h>
#include <linux/uaccess.h>
#include <linux/dasd_mod.h>

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
	struct dasd_device *base;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	base = dasd_device_from_gendisk(bdev->bd_disk);
	if (!base)
		return -ENODEV;

	dasd_enable_device(base);
	dasd_put_device(base);
	return 0;
}

/*
 * Disable device.
 * Used by dasdfmt. Disable I/O operations but allow ioctls.
 */
static int
dasd_ioctl_disable(struct block_device *bdev)
{
	struct dasd_device *base;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	base = dasd_device_from_gendisk(bdev->bd_disk);
	if (!base)
		return -ENODEV;
	/*
	 * Man this is sick. We don't do a real disable but only downgrade
	 * the device to DASD_STATE_BASIC. The reason is that dasdfmt uses
	 * BIODASDDISABLE to disable accesses to the device via the block
	 * device layer but it still wants to do i/o on the device by
	 * using the BIODASDFMT ioctl. Therefore the correct state for the
	 * device is DASD_STATE_BASIC that allows to do basic i/o.
	 */
	dasd_set_target_state(base, DASD_STATE_BASIC);
	/*
	 * Set i_size to zero, since read, write, etc. check against this
	 * value.
	 */
	set_capacity(bdev->bd_disk, 0);
	dasd_put_device(base);
	return 0;
}

/*
 * Quiesce device.
 */
static int dasd_ioctl_quiesce(struct dasd_block *block)
{
	unsigned long flags;
	struct dasd_device *base;

	base = block->base;
	if (!capable (CAP_SYS_ADMIN))
		return -EACCES;

	pr_info("%s: The DASD has been put in the quiesce "
		"state\n", dev_name(&base->cdev->dev));
	spin_lock_irqsave(get_ccwdev_lock(base->cdev), flags);
	dasd_device_set_stop_bits(base, DASD_STOPPED_QUIESCE);
	spin_unlock_irqrestore(get_ccwdev_lock(base->cdev), flags);
	return 0;
}


/*
 * Resume device.
 */
static int dasd_ioctl_resume(struct dasd_block *block)
{
	unsigned long flags;
	struct dasd_device *base;

	base = block->base;
	if (!capable (CAP_SYS_ADMIN))
		return -EACCES;

	pr_info("%s: I/O operations have been resumed "
		"on the DASD\n", dev_name(&base->cdev->dev));
	spin_lock_irqsave(get_ccwdev_lock(base->cdev), flags);
	dasd_device_remove_stop_bits(base, DASD_STOPPED_QUIESCE);
	spin_unlock_irqrestore(get_ccwdev_lock(base->cdev), flags);

	dasd_schedule_block_bh(block);
	dasd_schedule_device_bh(base);
	return 0;
}

/*
 * Abort all failfast I/O on a device.
 */
static int dasd_ioctl_abortio(struct dasd_block *block)
{
	unsigned long flags;
	struct dasd_device *base;
	struct dasd_ccw_req *cqr, *n;

	base = block->base;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (test_and_set_bit(DASD_FLAG_ABORTALL, &base->flags))
		return 0;
	DBF_DEV_EVENT(DBF_NOTICE, base, "%s", "abortall flag set");

	spin_lock_irqsave(&block->request_queue_lock, flags);
	spin_lock(&block->queue_lock);
	list_for_each_entry_safe(cqr, n, &block->ccw_queue, blocklist) {
		if (test_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags) &&
		    cqr->callback_data &&
		    cqr->callback_data != DASD_SLEEPON_START_TAG &&
		    cqr->callback_data != DASD_SLEEPON_END_TAG) {
			spin_unlock(&block->queue_lock);
			blk_abort_request(cqr->callback_data);
			spin_lock(&block->queue_lock);
		}
	}
	spin_unlock(&block->queue_lock);
	spin_unlock_irqrestore(&block->request_queue_lock, flags);

	dasd_schedule_block_bh(block);
	return 0;
}

/*
 * Allow I/O on a device
 */
static int dasd_ioctl_allowio(struct dasd_block *block)
{
	struct dasd_device *base;

	base = block->base;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (test_and_clear_bit(DASD_FLAG_ABORTALL, &base->flags))
		DBF_DEV_EVENT(DBF_NOTICE, base, "%s", "abortall flag unset");

	return 0;
}

/*
 * performs formatting of _device_ according to _fdata_
 * Note: The discipline's format_function is assumed to deliver formatting
 * commands to format multiple units of the device. In terms of the ECKD
 * devices this means CCWs are generated to format multiple tracks.
 */
static int
dasd_format(struct dasd_block *block, struct format_data_t *fdata)
{
	struct dasd_device *base;
	int rc;

	base = block->base;
	if (base->discipline->format_device == NULL)
		return -EPERM;

	if (base->state != DASD_STATE_BASIC) {
		pr_warn("%s: The DASD cannot be formatted while it is enabled\n",
			dev_name(&base->cdev->dev));
		return -EBUSY;
	}

	DBF_DEV_EVENT(DBF_NOTICE, base,
		      "formatting units %u to %u (%u B blocks) flags %u",
		      fdata->start_unit,
		      fdata->stop_unit, fdata->blksize, fdata->intensity);

	/* Since dasdfmt keeps the device open after it was disabled,
	 * there still exists an inode for this device.
	 * We must update i_blkbits, otherwise we might get errors when
	 * enabling the device later.
	 */
	if (fdata->start_unit == 0) {
		block->gdp->part0->bd_inode->i_blkbits =
			blksize_bits(fdata->blksize);
	}

	rc = base->discipline->format_device(base, fdata, 1);
	if (rc == -EAGAIN)
		rc = base->discipline->format_device(base, fdata, 0);

	return rc;
}

static int dasd_check_format(struct dasd_block *block,
			     struct format_check_t *cdata)
{
	struct dasd_device *base;
	int rc;

	base = block->base;
	if (!base->discipline->check_device_format)
		return -ENOTTY;

	rc = base->discipline->check_device_format(base, cdata, 1);
	if (rc == -EAGAIN)
		rc = base->discipline->check_device_format(base, cdata, 0);

	return rc;
}

/*
 * Format device.
 */
static int
dasd_ioctl_format(struct block_device *bdev, void __user *argp)
{
	struct dasd_device *base;
	struct format_data_t fdata;
	int rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!argp)
		return -EINVAL;
	base = dasd_device_from_gendisk(bdev->bd_disk);
	if (!base)
		return -ENODEV;
	if (base->features & DASD_FEATURE_READONLY ||
	    test_bit(DASD_FLAG_DEVICE_RO, &base->flags)) {
		dasd_put_device(base);
		return -EROFS;
	}
	if (copy_from_user(&fdata, argp, sizeof(struct format_data_t))) {
		dasd_put_device(base);
		return -EFAULT;
	}
	if (bdev_is_partition(bdev)) {
		pr_warn("%s: The specified DASD is a partition and cannot be formatted\n",
			dev_name(&base->cdev->dev));
		dasd_put_device(base);
		return -EINVAL;
	}
	rc = dasd_format(base->block, &fdata);
	dasd_put_device(base);

	return rc;
}

/*
 * Check device format
 */
static int dasd_ioctl_check_format(struct block_device *bdev, void __user *argp)
{
	struct format_check_t cdata;
	struct dasd_device *base;
	int rc = 0;

	if (!argp)
		return -EINVAL;

	base = dasd_device_from_gendisk(bdev->bd_disk);
	if (!base)
		return -ENODEV;
	if (bdev_is_partition(bdev)) {
		pr_warn("%s: The specified DASD is a partition and cannot be checked\n",
			dev_name(&base->cdev->dev));
		rc = -EINVAL;
		goto out_err;
	}

	if (copy_from_user(&cdata, argp, sizeof(cdata))) {
		rc = -EFAULT;
		goto out_err;
	}

	rc = dasd_check_format(base->block, &cdata);
	if (rc)
		goto out_err;

	if (copy_to_user(argp, &cdata, sizeof(cdata)))
		rc = -EFAULT;

out_err:
	dasd_put_device(base);

	return rc;
}

static int dasd_release_space(struct dasd_device *device,
			      struct format_data_t *rdata)
{
	if (!device->discipline->is_ese && !device->discipline->is_ese(device))
		return -ENOTSUPP;
	if (!device->discipline->release_space)
		return -ENOTSUPP;

	return device->discipline->release_space(device, rdata);
}

/*
 * Release allocated space
 */
static int dasd_ioctl_release_space(struct block_device *bdev, void __user *argp)
{
	struct format_data_t rdata;
	struct dasd_device *base;
	int rc = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!argp)
		return -EINVAL;

	base = dasd_device_from_gendisk(bdev->bd_disk);
	if (!base)
		return -ENODEV;
	if (base->features & DASD_FEATURE_READONLY ||
	    test_bit(DASD_FLAG_DEVICE_RO, &base->flags)) {
		rc = -EROFS;
		goto out_err;
	}
	if (bdev_is_partition(bdev)) {
		pr_warn("%s: The specified DASD is a partition and tracks cannot be released\n",
			dev_name(&base->cdev->dev));
		rc = -EINVAL;
		goto out_err;
	}

	if (copy_from_user(&rdata, argp, sizeof(rdata))) {
		rc = -EFAULT;
		goto out_err;
	}

	rc = dasd_release_space(base, &rdata);

out_err:
	dasd_put_device(base);

	return rc;
}

/*
 * Swap driver iternal copy relation.
 */
static int
dasd_ioctl_copy_pair_swap(struct block_device *bdev, void __user *argp)
{
	struct dasd_copypair_swap_data_t data;
	struct dasd_device *device;
	int rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	device = dasd_device_from_gendisk(bdev->bd_disk);
	if (!device)
		return -ENODEV;

	if (copy_from_user(&data, argp, sizeof(struct dasd_copypair_swap_data_t))) {
		dasd_put_device(device);
		return -EFAULT;
	}
	if (memchr_inv(data.reserved, 0, sizeof(data.reserved))) {
		pr_warn("%s: Invalid swap data specified\n",
			dev_name(&device->cdev->dev));
		dasd_put_device(device);
		return DASD_COPYPAIRSWAP_INVALID;
	}
	if (bdev_is_partition(bdev)) {
		pr_warn("%s: The specified DASD is a partition and cannot be swapped\n",
			dev_name(&device->cdev->dev));
		dasd_put_device(device);
		return DASD_COPYPAIRSWAP_INVALID;
	}
	if (!device->copy) {
		pr_warn("%s: The specified DASD has no copy pair set up\n",
			dev_name(&device->cdev->dev));
		dasd_put_device(device);
		return -ENODEV;
	}
	if (!device->discipline->copy_pair_swap) {
		dasd_put_device(device);
		return -EOPNOTSUPP;
	}
	rc = device->discipline->copy_pair_swap(device, data.primary,
						data.secondary);
	dasd_put_device(device);

	return rc;
}

#ifdef CONFIG_DASD_PROFILE
/*
 * Reset device profile information
 */
static int dasd_ioctl_reset_profile(struct dasd_block *block)
{
	dasd_profile_reset(&block->profile);
	return 0;
}

/*
 * Return device profile information
 */
static int dasd_ioctl_read_profile(struct dasd_block *block, void __user *argp)
{
	struct dasd_profile_info_t *data;
	int rc = 0;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spin_lock_bh(&block->profile.lock);
	if (block->profile.data) {
		data->dasd_io_reqs = block->profile.data->dasd_io_reqs;
		data->dasd_io_sects = block->profile.data->dasd_io_sects;
		memcpy(data->dasd_io_secs, block->profile.data->dasd_io_secs,
		       sizeof(data->dasd_io_secs));
		memcpy(data->dasd_io_times, block->profile.data->dasd_io_times,
		       sizeof(data->dasd_io_times));
		memcpy(data->dasd_io_timps, block->profile.data->dasd_io_timps,
		       sizeof(data->dasd_io_timps));
		memcpy(data->dasd_io_time1, block->profile.data->dasd_io_time1,
		       sizeof(data->dasd_io_time1));
		memcpy(data->dasd_io_time2, block->profile.data->dasd_io_time2,
		       sizeof(data->dasd_io_time2));
		memcpy(data->dasd_io_time2ps,
		       block->profile.data->dasd_io_time2ps,
		       sizeof(data->dasd_io_time2ps));
		memcpy(data->dasd_io_time3, block->profile.data->dasd_io_time3,
		       sizeof(data->dasd_io_time3));
		memcpy(data->dasd_io_nr_req,
		       block->profile.data->dasd_io_nr_req,
		       sizeof(data->dasd_io_nr_req));
		spin_unlock_bh(&block->profile.lock);
	} else {
		spin_unlock_bh(&block->profile.lock);
		rc = -EIO;
		goto out;
	}
	if (copy_to_user(argp, data, sizeof(*data)))
		rc = -EFAULT;
out:
	kfree(data);
	return rc;
}
#else
static int dasd_ioctl_reset_profile(struct dasd_block *block)
{
	return -ENOTTY;
}

static int dasd_ioctl_read_profile(struct dasd_block *block, void __user *argp)
{
	return -ENOTTY;
}
#endif

/*
 * Return dasd information. Used for BIODASDINFO and BIODASDINFO2.
 */
static int __dasd_ioctl_information(struct dasd_block *block,
		struct dasd_information2_t *dasd_info)
{
	struct subchannel_id sch_id;
	struct ccw_dev_id dev_id;
	struct dasd_device *base;
	struct ccw_device *cdev;
	struct list_head *l;
	unsigned long flags;
	int rc;

	base = block->base;
	if (!base->discipline || !base->discipline->fill_info)
		return -EINVAL;

	rc = base->discipline->fill_info(base, dasd_info);
	if (rc)
		return rc;

	cdev = base->cdev;
	ccw_device_get_id(cdev, &dev_id);
	ccw_device_get_schid(cdev, &sch_id);

	dasd_info->devno = dev_id.devno;
	dasd_info->schid = sch_id.sch_no;
	dasd_info->cu_type = cdev->id.cu_type;
	dasd_info->cu_model = cdev->id.cu_model;
	dasd_info->dev_type = cdev->id.dev_type;
	dasd_info->dev_model = cdev->id.dev_model;
	dasd_info->status = base->state;
	/*
	 * The open_count is increased for every opener, that includes
	 * the blkdev_get in dasd_scan_partitions.
	 * This must be hidden from user-space.
	 */
	dasd_info->open_count = atomic_read(&block->open_count);
	if (!block->bdev_handle)
		dasd_info->open_count++;

	/*
	 * check if device is really formatted
	 * LDL / CDL was returned by 'fill_info'
	 */
	if ((base->state < DASD_STATE_READY) ||
	    (dasd_check_blocksize(block->bp_block)))
		dasd_info->format = DASD_FORMAT_NONE;

	dasd_info->features |=
		((base->features & DASD_FEATURE_READONLY) != 0);

	memcpy(dasd_info->type, base->discipline->name, 4);

	spin_lock_irqsave(get_ccwdev_lock(base->cdev), flags);
	list_for_each(l, &base->ccw_queue)
		dasd_info->chanq_len++;
	spin_unlock_irqrestore(get_ccwdev_lock(base->cdev), flags);
	return 0;
}

static int dasd_ioctl_information(struct dasd_block *block, void __user *argp,
		size_t copy_size)
{
	struct dasd_information2_t *dasd_info;
	int error;

	dasd_info = kzalloc(sizeof(*dasd_info), GFP_KERNEL);
	if (!dasd_info)
		return -ENOMEM;

	error = __dasd_ioctl_information(block, dasd_info);
	if (!error && copy_to_user(argp, dasd_info, copy_size))
		error = -EFAULT;
	kfree(dasd_info);
	return error;
}

/*
 * Set read only
 */
int dasd_set_read_only(struct block_device *bdev, bool ro)
{
	struct dasd_device *base;
	int rc;

	/* do not manipulate hardware state for partitions */
	if (bdev_is_partition(bdev))
		return 0;

	base = dasd_device_from_gendisk(bdev->bd_disk);
	if (!base)
		return -ENODEV;
	if (!ro && test_bit(DASD_FLAG_DEVICE_RO, &base->flags))
		rc = -EROFS;
	else
		rc = dasd_set_feature(base->cdev, DASD_FEATURE_READONLY, ro);
	dasd_put_device(base);
	return rc;
}

static int dasd_ioctl_readall_cmb(struct dasd_block *block, unsigned int cmd,
				  struct cmbdata __user *argp)
{
	size_t size = _IOC_SIZE(cmd);
	struct cmbdata data;
	int ret;

	ret = cmf_readall(block->base->cdev, &data);
	if (!ret && copy_to_user(argp, &data, min(size, sizeof(*argp))))
		return -EFAULT;
	return ret;
}

int dasd_ioctl(struct block_device *bdev, blk_mode_t mode,
	       unsigned int cmd, unsigned long arg)
{
	struct dasd_block *block;
	struct dasd_device *base;
	void __user *argp;
	int rc;

	if (is_compat_task())
		argp = compat_ptr(arg);
	else
		argp = (void __user *)arg;

	if ((_IOC_DIR(cmd) != _IOC_NONE) && !arg)
		return -EINVAL;

	base = dasd_device_from_gendisk(bdev->bd_disk);
	if (!base)
		return -ENODEV;
	block = base->block;
	rc = 0;
	switch (cmd) {
	case BIODASDDISABLE:
		rc = dasd_ioctl_disable(bdev);
		break;
	case BIODASDENABLE:
		rc = dasd_ioctl_enable(bdev);
		break;
	case BIODASDQUIESCE:
		rc = dasd_ioctl_quiesce(block);
		break;
	case BIODASDRESUME:
		rc = dasd_ioctl_resume(block);
		break;
	case BIODASDABORTIO:
		rc = dasd_ioctl_abortio(block);
		break;
	case BIODASDALLOWIO:
		rc = dasd_ioctl_allowio(block);
		break;
	case BIODASDFMT:
		rc = dasd_ioctl_format(bdev, argp);
		break;
	case BIODASDCHECKFMT:
		rc = dasd_ioctl_check_format(bdev, argp);
		break;
	case BIODASDINFO:
		rc = dasd_ioctl_information(block, argp,
				sizeof(struct dasd_information_t));
		break;
	case BIODASDINFO2:
		rc = dasd_ioctl_information(block, argp,
				sizeof(struct dasd_information2_t));
		break;
	case BIODASDPRRD:
		rc = dasd_ioctl_read_profile(block, argp);
		break;
	case BIODASDPRRST:
		rc = dasd_ioctl_reset_profile(block);
		break;
	case DASDAPIVER:
		rc = dasd_ioctl_api_version(argp);
		break;
	case BIODASDCMFENABLE:
		rc = enable_cmf(base->cdev);
		break;
	case BIODASDCMFDISABLE:
		rc = disable_cmf(base->cdev);
		break;
	case BIODASDREADALLCMB:
		rc = dasd_ioctl_readall_cmb(block, cmd, argp);
		break;
	case BIODASDRAS:
		rc = dasd_ioctl_release_space(bdev, argp);
		break;
	case BIODASDCOPYPAIRSWAP:
		rc = dasd_ioctl_copy_pair_swap(bdev, argp);
		break;
	default:
		/* if the discipline has an ioctl method try it. */
		rc = -ENOTTY;
		if (base->discipline->ioctl)
			rc = base->discipline->ioctl(block, cmd, argp);
	}
	dasd_put_device(base);
	return rc;
}


/**
 * dasd_biodasdinfo() - fill out the dasd information structure
 * @disk: [in] pointer to gendisk structure that references a DASD
 * @info: [out] pointer to the dasd_information2_t structure
 *
 * Provide access to DASD specific information.
 * The gendisk structure is checked if it belongs to the DASD driver by
 * comparing the gendisk->fops pointer.
 * If it does not belong to the DASD driver -EINVAL is returned.
 * Otherwise the provided dasd_information2_t structure is filled out.
 *
 * Returns:
 *   %0 on success and a negative error value on failure.
 */
int dasd_biodasdinfo(struct gendisk *disk, struct dasd_information2_t *info)
{
	struct dasd_device *base;
	int error;

	if (disk->fops != &dasd_device_operations)
		return -EINVAL;

	base = dasd_device_from_gendisk(disk);
	if (!base)
		return -ENODEV;
	error = __dasd_ioctl_information(base->block, info);
	dasd_put_device(base);
	return error;
}
/* export that symbol_get in partition detection is possible */
EXPORT_SYMBOL_GPL(dasd_biodasdinfo);
