#include <linux/capability.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/hdreg.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>
#include <linux/smp_lock.h>
#include <linux/blktrace_api.h>
#include <asm/uaccess.h>

static int blkpg_ioctl(struct block_device *bdev, struct blkpg_ioctl_arg __user *arg)
{
	struct block_device *bdevp;
	struct gendisk *disk;
	struct hd_struct *part;
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;
	struct disk_part_iter piter;
	long long start, length;
	int partno;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (copy_from_user(&a, arg, sizeof(struct blkpg_ioctl_arg)))
		return -EFAULT;
	if (copy_from_user(&p, a.data, sizeof(struct blkpg_partition)))
		return -EFAULT;
	disk = bdev->bd_disk;
	if (bdev != bdev->bd_contains)
		return -EINVAL;
	partno = p.pno;
	if (partno <= 0)
		return -EINVAL;
	switch (a.op) {
		case BLKPG_ADD_PARTITION:
			start = p.start >> 9;
			length = p.length >> 9;
			/* check for fit in a hd_struct */ 
			if (sizeof(sector_t) == sizeof(long) && 
			    sizeof(long long) > sizeof(long)) {
				long pstart = start, plength = length;
				if (pstart != start || plength != length
				    || pstart < 0 || plength < 0)
					return -EINVAL;
			}

			mutex_lock(&bdev->bd_mutex);

			/* overlap? */
			disk_part_iter_init(&piter, disk,
					    DISK_PITER_INCL_EMPTY);
			while ((part = disk_part_iter_next(&piter))) {
				if (!(start + length <= part->start_sect ||
				      start >= part->start_sect + part->nr_sects)) {
					disk_part_iter_exit(&piter);
					mutex_unlock(&bdev->bd_mutex);
					return -EBUSY;
				}
			}
			disk_part_iter_exit(&piter);

			/* all seems OK */
			err = add_partition(disk, partno, start, length,
					    ADDPART_FLAG_NONE);
			mutex_unlock(&bdev->bd_mutex);
			return err;
		case BLKPG_DEL_PARTITION:
			part = disk_get_part(disk, partno);
			if (!part)
				return -ENXIO;

			bdevp = bdget(part_devt(part));
			disk_put_part(part);
			if (!bdevp)
				return -ENOMEM;

			mutex_lock(&bdevp->bd_mutex);
			if (bdevp->bd_openers) {
				mutex_unlock(&bdevp->bd_mutex);
				bdput(bdevp);
				return -EBUSY;
			}
			/* all seems OK */
			fsync_bdev(bdevp);
			invalidate_bdev(bdevp);

			mutex_lock_nested(&bdev->bd_mutex, 1);
			delete_partition(disk, partno);
			mutex_unlock(&bdev->bd_mutex);
			mutex_unlock(&bdevp->bd_mutex);
			bdput(bdevp);

			return 0;
		default:
			return -EINVAL;
	}
}

static int blkdev_reread_part(struct block_device *bdev)
{
	struct gendisk *disk = bdev->bd_disk;
	int res;

	if (!disk_partitionable(disk) || bdev != bdev->bd_contains)
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!mutex_trylock(&bdev->bd_mutex))
		return -EBUSY;
	res = rescan_partitions(disk, bdev);
	mutex_unlock(&bdev->bd_mutex);
	return res;
}

static void blk_ioc_discard_endio(struct bio *bio, int err)
{
	if (err) {
		if (err == -EOPNOTSUPP)
			set_bit(BIO_EOPNOTSUPP, &bio->bi_flags);
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
	}
	complete(bio->bi_private);
}

static int blk_ioctl_discard(struct block_device *bdev, uint64_t start,
			     uint64_t len)
{
	struct request_queue *q = bdev_get_queue(bdev);
	int ret = 0;

	if (start & 511)
		return -EINVAL;
	if (len & 511)
		return -EINVAL;
	start >>= 9;
	len >>= 9;

	if (start + len > (bdev->bd_inode->i_size >> 9))
		return -EINVAL;

	if (!q->prepare_discard_fn)
		return -EOPNOTSUPP;

	while (len && !ret) {
		DECLARE_COMPLETION_ONSTACK(wait);
		struct bio *bio;

		bio = bio_alloc(GFP_KERNEL, 0);
		if (!bio)
			return -ENOMEM;

		bio->bi_end_io = blk_ioc_discard_endio;
		bio->bi_bdev = bdev;
		bio->bi_private = &wait;
		bio->bi_sector = start;

		if (len > q->max_hw_sectors) {
			bio->bi_size = q->max_hw_sectors << 9;
			len -= q->max_hw_sectors;
			start += q->max_hw_sectors;
		} else {
			bio->bi_size = len << 9;
			len = 0;
		}
		submit_bio(DISCARD_NOBARRIER, bio);

		wait_for_completion(&wait);

		if (bio_flagged(bio, BIO_EOPNOTSUPP))
			ret = -EOPNOTSUPP;
		else if (!bio_flagged(bio, BIO_UPTODATE))
			ret = -EIO;
		bio_put(bio);
	}
	return ret;
}

static int put_ushort(unsigned long arg, unsigned short val)
{
	return put_user(val, (unsigned short __user *)arg);
}

static int put_int(unsigned long arg, int val)
{
	return put_user(val, (int __user *)arg);
}

static int put_long(unsigned long arg, long val)
{
	return put_user(val, (long __user *)arg);
}

static int put_ulong(unsigned long arg, unsigned long val)
{
	return put_user(val, (unsigned long __user *)arg);
}

static int put_u64(unsigned long arg, u64 val)
{
	return put_user(val, (u64 __user *)arg);
}

static int blkdev_locked_ioctl(struct file *file, struct block_device *bdev,
				unsigned cmd, unsigned long arg)
{
	struct backing_dev_info *bdi;
	int ret, n;

	switch (cmd) {
	case BLKRAGET:
	case BLKFRAGET:
		if (!arg)
			return -EINVAL;
		bdi = blk_get_backing_dev_info(bdev);
		if (bdi == NULL)
			return -ENOTTY;
		return put_long(arg, (bdi->ra_pages * PAGE_CACHE_SIZE) / 512);
	case BLKROGET:
		return put_int(arg, bdev_read_only(bdev) != 0);
	case BLKBSZGET: /* get the logical block size (cf. BLKSSZGET) */
		return put_int(arg, block_size(bdev));
	case BLKSSZGET: /* get block device hardware sector size */
		return put_int(arg, bdev_hardsect_size(bdev));
	case BLKSECTGET:
		return put_ushort(arg, bdev_get_queue(bdev)->max_sectors);
	case BLKRASET:
	case BLKFRASET:
		if(!capable(CAP_SYS_ADMIN))
			return -EACCES;
		bdi = blk_get_backing_dev_info(bdev);
		if (bdi == NULL)
			return -ENOTTY;
		bdi->ra_pages = (arg * 512) / PAGE_CACHE_SIZE;
		return 0;
	case BLKBSZSET:
		/* set the logical block size */
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		if (!arg)
			return -EINVAL;
		if (get_user(n, (int __user *) arg))
			return -EFAULT;
		if (bd_claim(bdev, file) < 0)
			return -EBUSY;
		ret = set_blocksize(bdev, n);
		bd_release(bdev);
		return ret;
	case BLKPG:
		return blkpg_ioctl(bdev, (struct blkpg_ioctl_arg __user *) arg);
	case BLKRRPART:
		return blkdev_reread_part(bdev);
	case BLKGETSIZE:
		if ((bdev->bd_inode->i_size >> 9) > ~0UL)
			return -EFBIG;
		return put_ulong(arg, bdev->bd_inode->i_size >> 9);
	case BLKGETSIZE64:
		return put_u64(arg, bdev->bd_inode->i_size);
	case BLKTRACESTART:
	case BLKTRACESTOP:
	case BLKTRACESETUP:
	case BLKTRACETEARDOWN:
		return blk_trace_ioctl(bdev, cmd, (char __user *) arg);
	}
	return -ENOIOCTLCMD;
}

int blkdev_driver_ioctl(struct inode *inode, struct file *file,
			struct gendisk *disk, unsigned cmd, unsigned long arg)
{
	int ret;
	if (disk->fops->unlocked_ioctl)
		return disk->fops->unlocked_ioctl(file, cmd, arg);

	if (disk->fops->ioctl) {
		lock_kernel();
		ret = disk->fops->ioctl(inode, file, cmd, arg);
		unlock_kernel();
		return ret;
	}

	return -ENOTTY;
}
EXPORT_SYMBOL_GPL(blkdev_driver_ioctl);

/*
 * always keep this in sync with compat_blkdev_ioctl() and
 * compat_blkdev_locked_ioctl()
 */
int blkdev_ioctl(struct inode *inode, struct file *file, unsigned cmd,
			unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	struct gendisk *disk = bdev->bd_disk;
	int ret, n;

	switch(cmd) {
	case BLKFLSBUF:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;

		ret = blkdev_driver_ioctl(inode, file, disk, cmd, arg);
		/* -EINVAL to handle old uncorrected drivers */
		if (ret != -EINVAL && ret != -ENOTTY)
			return ret;

		lock_kernel();
		fsync_bdev(bdev);
		invalidate_bdev(bdev);
		unlock_kernel();
		return 0;

	case BLKROSET:
		ret = blkdev_driver_ioctl(inode, file, disk, cmd, arg);
		/* -EINVAL to handle old uncorrected drivers */
		if (ret != -EINVAL && ret != -ENOTTY)
			return ret;
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		if (get_user(n, (int __user *)(arg)))
			return -EFAULT;
		lock_kernel();
		set_device_ro(bdev, n);
		unlock_kernel();
		return 0;

	case BLKDISCARD: {
		uint64_t range[2];

		if (!(file->f_mode & FMODE_WRITE))
			return -EBADF;

		if (copy_from_user(range, (void __user *)arg, sizeof(range)))
			return -EFAULT;

		return blk_ioctl_discard(bdev, range[0], range[1]);
	}

	case HDIO_GETGEO: {
		struct hd_geometry geo;

		if (!arg)
			return -EINVAL;
		if (!disk->fops->getgeo)
			return -ENOTTY;

		/*
		 * We need to set the startsect first, the driver may
		 * want to override it.
		 */
		geo.start = get_start_sect(bdev);
		ret = disk->fops->getgeo(bdev, &geo);
		if (ret)
			return ret;
		if (copy_to_user((struct hd_geometry __user *)arg, &geo,
					sizeof(geo)))
			return -EFAULT;
		return 0;
	}
	}

	lock_kernel();
	ret = blkdev_locked_ioctl(file, bdev, cmd, arg);
	unlock_kernel();
	if (ret != -ENOIOCTLCMD)
		return ret;

	return blkdev_driver_ioctl(inode, file, disk, cmd, arg);
}
EXPORT_SYMBOL_GPL(blkdev_ioctl);
