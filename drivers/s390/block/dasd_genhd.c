/*
 * File...........: linux/drivers/s390/block/dasd_genhd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * gendisk related functions for the dasd driver.
 *
 * $Revision: 1.48 $
 */

#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/blkpg.h>

#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_gendisk:"

#include "dasd_int.h"

/*
 * Allocate and register gendisk structure for device.
 */
int
dasd_gendisk_alloc(struct dasd_device *device)
{
	struct gendisk *gdp;
	int len;

	/* Make sure the minor for this device exists. */
	if (device->devindex >= DASD_PER_MAJOR)
		return -EBUSY;

	gdp = alloc_disk(1 << DASD_PARTN_BITS);
	if (!gdp)
		return -ENOMEM;

	/* Initialize gendisk structure. */
	gdp->major = DASD_MAJOR;
	gdp->first_minor = device->devindex << DASD_PARTN_BITS;
	gdp->fops = &dasd_device_operations;
	gdp->driverfs_dev = &device->cdev->dev;

	/*
	 * Set device name.
	 *   dasda - dasdz : 26 devices
	 *   dasdaa - dasdzz : 676 devices, added up = 702
	 *   dasdaaa - dasdzzz : 17576 devices, added up = 18278
	 *   dasdaaaa - dasdzzzz : 456976 devices, added up = 475252
	 */
	len = sprintf(gdp->disk_name, "dasd");
	if (device->devindex > 25) {
	        if (device->devindex > 701) {
		        if (device->devindex > 18277)
			        len += sprintf(gdp->disk_name + len, "%c",
					       'a'+(((device->devindex-18278)
						     /17576)%26));
			len += sprintf(gdp->disk_name + len, "%c",
				       'a'+(((device->devindex-702)/676)%26));
		}
		len += sprintf(gdp->disk_name + len, "%c",
			       'a'+(((device->devindex-26)/26)%26));
	}
	len += sprintf(gdp->disk_name + len, "%c", 'a'+(device->devindex%26));

 	sprintf(gdp->devfs_name, "dasd/%s", device->cdev->dev.bus_id);

	if (test_bit(DASD_FLAG_RO, &device->flags))
		set_disk_ro(gdp, 1);
	gdp->private_data = device;
	gdp->queue = device->request_queue;
	device->gdp = gdp;
	set_capacity(device->gdp, 0);
	add_disk(device->gdp);
	return 0;
}

/*
 * Unregister and free gendisk structure for device.
 */
void
dasd_gendisk_free(struct dasd_device *device)
{
	del_gendisk(device->gdp);
	device->gdp->queue = 0;
	put_disk(device->gdp);
	device->gdp = 0;
}

/*
 * Trigger a partition detection.
 */
int
dasd_scan_partitions(struct dasd_device * device)
{
	struct block_device *bdev;

	/* Make the disk known. */
	set_capacity(device->gdp, device->blocks << device->s2b_shift);
	bdev = bdget_disk(device->gdp, 0);
	if (!bdev || blkdev_get(bdev, FMODE_READ, 1) < 0)
		return -ENODEV;
	/*
	 * See fs/partition/check.c:register_disk,rescan_partitions
	 * Can't call rescan_partitions directly. Use ioctl.
	 */
	ioctl_by_bdev(bdev, BLKRRPART, 0);
	/*
	 * Since the matching blkdev_put call to the blkdev_get in
	 * this function is not called before dasd_destroy_partitions
	 * the offline open_count limit needs to be increased from
	 * 0 to 1. This is done by setting device->bdev (see
	 * dasd_generic_set_offline). As long as the partition
	 * detection is running no offline should be allowed. That
	 * is why the assignment to device->bdev is done AFTER
	 * the BLKRRPART ioctl.
	 */
	device->bdev = bdev;
	return 0;
}

/*
 * Remove all inodes in the system for a device, delete the
 * partitions and make device unusable by setting its size to zero.
 */
void
dasd_destroy_partitions(struct dasd_device * device)
{
	/* The two structs have 168/176 byte on 31/64 bit. */
	struct blkpg_partition bpart;
	struct blkpg_ioctl_arg barg;
	struct block_device *bdev;

	/*
	 * Get the bdev pointer from the device structure and clear
	 * device->bdev to lower the offline open_count limit again.
	 */
	bdev = device->bdev;
	device->bdev = 0;

	/*
	 * See fs/partition/check.c:delete_partition
	 * Can't call delete_partitions directly. Use ioctl.
	 * The ioctl also does locking and invalidation.
	 */
	memset(&bpart, 0, sizeof(struct blkpg_partition));
	memset(&barg, 0, sizeof(struct blkpg_ioctl_arg));
	barg.data = &bpart;
	barg.op = BLKPG_DEL_PARTITION;
	for (bpart.pno = device->gdp->minors - 1; bpart.pno > 0; bpart.pno--)
		ioctl_by_bdev(bdev, BLKPG, (unsigned long) &barg);

	invalidate_partition(device->gdp, 0);
	/* Matching blkdev_put to the blkdev_get in dasd_scan_partitions. */
	blkdev_put(bdev);
	set_capacity(device->gdp, 0);
}

int
dasd_gendisk_init(void)
{
	int rc;

	/* Register to static dasd major 94 */
	rc = register_blkdev(DASD_MAJOR, "dasd");
	if (rc != 0) {
		MESSAGE(KERN_WARNING,
			"Couldn't register successfully to "
			"major no %d", DASD_MAJOR);
		return rc;
	}
	return 0;
}

void
dasd_gendisk_exit(void)
{
	unregister_blkdev(DASD_MAJOR, "dasd");
}
