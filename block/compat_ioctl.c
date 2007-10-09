#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/blktrace_api.h>
#include <linux/cdrom.h>
#include <linux/compat.h>
#include <linux/elevator.h>
#include <linux/fd.h>
#include <linux/hdreg.h>
#include <linux/syscalls.h>
#include <linux/smp_lock.h>
#include <linux/types.h>
#include <linux/uaccess.h>

static int compat_put_ushort(unsigned long arg, unsigned short val)
{
	return put_user(val, (unsigned short __user *)compat_ptr(arg));
}

static int compat_put_int(unsigned long arg, int val)
{
	return put_user(val, (compat_int_t __user *)compat_ptr(arg));
}

static int compat_put_long(unsigned long arg, long val)
{
	return put_user(val, (compat_long_t __user *)compat_ptr(arg));
}

static int compat_put_ulong(unsigned long arg, compat_ulong_t val)
{
	return put_user(val, (compat_ulong_t __user *)compat_ptr(arg));
}

static int compat_put_u64(unsigned long arg, u64 val)
{
	return put_user(val, (compat_u64 __user *)compat_ptr(arg));
}

#define BLKBSZGET_32		_IOR(0x12, 112, int)
#define BLKBSZSET_32		_IOW(0x12, 113, int)
#define BLKGETSIZE64_32		_IOR(0x12, 114, int)

static int compat_blkdev_driver_ioctl(struct inode *inode, struct file *file,
			struct gendisk *disk, unsigned cmd, unsigned long arg)
{
	int ret;

	switch (arg) {
	/*
	 * No handler required for the ones below, we just need to
	 * convert arg to a 64 bit pointer.
	 */
	case BLKSECTSET:
	/*
	 * 0x03 -- HD/IDE ioctl's used by hdparm and friends.
	 *         Some need translations, these do not.
	 */
	case HDIO_GET_IDENTITY:
	case HDIO_DRIVE_TASK:
	case HDIO_DRIVE_CMD:
	case HDIO_SCAN_HWIF:
	/* 0x330 is reserved -- it used to be HDIO_GETGEO_BIG */
	case 0x330:
	/* 0x02 -- Floppy ioctls */
	case FDMSGON:
	case FDMSGOFF:
	case FDSETEMSGTRESH:
	case FDFLUSH:
	case FDWERRORCLR:
	case FDSETMAXERRS:
	case FDGETMAXERRS:
	case FDGETDRVTYP:
	case FDEJECT:
	case FDCLRPRM:
	case FDFMTBEG:
	case FDFMTEND:
	case FDRESET:
	case FDTWADDLE:
	case FDFMTTRK:
	case FDRAWCMD:
	/* CDROM stuff */
	case CDROMPAUSE:
	case CDROMRESUME:
	case CDROMPLAYMSF:
	case CDROMPLAYTRKIND:
	case CDROMREADTOCHDR:
	case CDROMREADTOCENTRY:
	case CDROMSTOP:
	case CDROMSTART:
	case CDROMEJECT:
	case CDROMVOLCTRL:
	case CDROMSUBCHNL:
	case CDROMMULTISESSION:
	case CDROM_GET_MCN:
	case CDROMRESET:
	case CDROMVOLREAD:
	case CDROMSEEK:
	case CDROMPLAYBLK:
	case CDROMCLOSETRAY:
	case CDROM_DISC_STATUS:
	case CDROM_CHANGER_NSLOTS:
	case CDROM_GET_CAPABILITY:
	/* Ignore cdrom.h about these next 5 ioctls, they absolutely do
	 * not take a struct cdrom_read, instead they take a struct cdrom_msf
	 * which is compatible.
	 */
	case CDROMREADMODE2:
	case CDROMREADMODE1:
	case CDROMREADRAW:
	case CDROMREADCOOKED:
	case CDROMREADALL:
	/* DVD ioctls */
	case DVD_READ_STRUCT:
	case DVD_WRITE_STRUCT:
	case DVD_AUTH:
		arg = (unsigned long)compat_ptr(arg);
	/* These intepret arg as an unsigned long, not as a pointer,
	 * so we must not do compat_ptr() conversion. */
	case HDIO_SET_MULTCOUNT:
	case HDIO_SET_UNMASKINTR:
	case HDIO_SET_KEEPSETTINGS:
	case HDIO_SET_32BIT:
	case HDIO_SET_NOWERR:
	case HDIO_SET_DMA:
	case HDIO_SET_PIO_MODE:
	case HDIO_SET_NICE:
	case HDIO_SET_WCACHE:
	case HDIO_SET_ACOUSTIC:
	case HDIO_SET_BUSSTATE:
	case HDIO_SET_ADDRESS:
	case CDROMEJECT_SW:
	case CDROM_SET_OPTIONS:
	case CDROM_CLEAR_OPTIONS:
	case CDROM_SELECT_SPEED:
	case CDROM_SELECT_DISC:
	case CDROM_MEDIA_CHANGED:
	case CDROM_DRIVE_STATUS:
	case CDROM_LOCKDOOR:
	case CDROM_DEBUG:
		break;
	default:
		/* unknown ioctl number */
		return -ENOIOCTLCMD;
	}

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

static int compat_blkdev_locked_ioctl(struct inode *inode, struct file *file,
				struct block_device *bdev,
				unsigned cmd, unsigned long arg)
{
	struct backing_dev_info *bdi;

	switch (cmd) {
	case BLKRAGET:
	case BLKFRAGET:
		if (!arg)
			return -EINVAL;
		bdi = blk_get_backing_dev_info(bdev);
		if (bdi == NULL)
			return -ENOTTY;
		return compat_put_long(arg,
				       (bdi->ra_pages * PAGE_CACHE_SIZE) / 512);
	case BLKROGET: /* compatible */
		return compat_put_int(arg, bdev_read_only(bdev) != 0);
	case BLKBSZGET_32: /* get the logical block size (cf. BLKSSZGET) */
		return compat_put_int(arg, block_size(bdev));
	case BLKSSZGET: /* get block device hardware sector size */
		return compat_put_int(arg, bdev_hardsect_size(bdev));
	case BLKSECTGET:
		return compat_put_ushort(arg,
					 bdev_get_queue(bdev)->max_sectors);
	case BLKRASET: /* compatible, but no compat_ptr (!) */
	case BLKFRASET:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		bdi = blk_get_backing_dev_info(bdev);
		if (bdi == NULL)
			return -ENOTTY;
		bdi->ra_pages = (arg * 512) / PAGE_CACHE_SIZE;
		return 0;
	case BLKGETSIZE:
		if ((bdev->bd_inode->i_size >> 9) > ~0UL)
			return -EFBIG;
		return compat_put_ulong(arg, bdev->bd_inode->i_size >> 9);

	case BLKGETSIZE64_32:
		return compat_put_u64(arg, bdev->bd_inode->i_size);
	}
	return -ENOIOCTLCMD;
}

/* Most of the generic ioctls are handled in the normal fallback path.
   This assumes the blkdev's low level compat_ioctl always returns
   ENOIOCTLCMD for unknown ioctls. */
long compat_blkdev_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	int ret = -ENOIOCTLCMD;
	struct inode *inode = file->f_mapping->host;
	struct block_device *bdev = inode->i_bdev;
	struct gendisk *disk = bdev->bd_disk;

	switch (cmd) {
	case BLKFLSBUF:
	case BLKROSET:
	/*
	 * the ones below are implemented in blkdev_locked_ioctl,
	 * but we call blkdev_ioctl, which gets the lock for us
	 */
	case BLKRRPART:
		return blkdev_ioctl(inode, file, cmd,
				(unsigned long)compat_ptr(arg));
	case BLKBSZSET_32:
		return blkdev_ioctl(inode, file, BLKBSZSET,
				(unsigned long)compat_ptr(arg));
	}

	lock_kernel();
	ret = compat_blkdev_locked_ioctl(inode, file, bdev, cmd, arg);
	/* FIXME: why do we assume -> compat_ioctl needs the BKL? */
	if (ret == -ENOIOCTLCMD && disk->fops->compat_ioctl)
		ret = disk->fops->compat_ioctl(file, cmd, arg);
	unlock_kernel();

	if (ret != -ENOIOCTLCMD)
		return ret;

	return compat_blkdev_driver_ioctl(inode, file, disk, cmd, arg);
}
