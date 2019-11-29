// SPDX-License-Identifier: GPL-2.0
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/blktrace_api.h>
#include <linux/cdrom.h>
#include <linux/compat.h>
#include <linux/elevator.h>
#include <linux/hdreg.h>
#include <linux/pr.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
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

static int compat_put_uint(unsigned long arg, unsigned int val)
{
	return put_user(val, (compat_uint_t __user *)compat_ptr(arg));
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

struct compat_hd_geometry {
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	u32 start;
};

static int compat_hdio_getgeo(struct gendisk *disk, struct block_device *bdev,
			struct compat_hd_geometry __user *ugeo)
{
	struct hd_geometry geo;
	int ret;

	if (!ugeo)
		return -EINVAL;
	if (!disk->fops->getgeo)
		return -ENOTTY;

	memset(&geo, 0, sizeof(geo));
	/*
	 * We need to set the startsect first, the driver may
	 * want to override it.
	 */
	geo.start = get_start_sect(bdev);
	ret = disk->fops->getgeo(bdev, &geo);
	if (ret)
		return ret;

	ret = copy_to_user(ugeo, &geo, 4);
	ret |= put_user(geo.start, &ugeo->start);
	if (ret)
		ret = -EFAULT;

	return ret;
}

static int compat_hdio_ioctl(struct block_device *bdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	unsigned long __user *p;
	int error;

	p = compat_alloc_user_space(sizeof(unsigned long));
	error = __blkdev_driver_ioctl(bdev, mode,
				cmd, (unsigned long)p);
	if (error == 0) {
		unsigned int __user *uvp = compat_ptr(arg);
		unsigned long v;
		if (get_user(v, p) || put_user(v, uvp))
			error = -EFAULT;
	}
	return error;
}

struct compat_cdrom_read_audio {
	union cdrom_addr	addr;
	u8			addr_format;
	compat_int_t		nframes;
	compat_caddr_t		buf;
};

struct compat_cdrom_generic_command {
	unsigned char	cmd[CDROM_PACKET_SIZE];
	compat_caddr_t	buffer;
	compat_uint_t	buflen;
	compat_int_t	stat;
	compat_caddr_t	sense;
	unsigned char	data_direction;
	compat_int_t	quiet;
	compat_int_t	timeout;
	compat_caddr_t	reserved[1];
};

static int compat_cdrom_read_audio(struct block_device *bdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	struct cdrom_read_audio __user *cdread_audio;
	struct compat_cdrom_read_audio __user *cdread_audio32;
	__u32 data;
	void __user *datap;

	cdread_audio = compat_alloc_user_space(sizeof(*cdread_audio));
	cdread_audio32 = compat_ptr(arg);

	if (copy_in_user(&cdread_audio->addr,
			 &cdread_audio32->addr,
			 (sizeof(*cdread_audio32) -
			  sizeof(compat_caddr_t))))
		return -EFAULT;

	if (get_user(data, &cdread_audio32->buf))
		return -EFAULT;
	datap = compat_ptr(data);
	if (put_user(datap, &cdread_audio->buf))
		return -EFAULT;

	return __blkdev_driver_ioctl(bdev, mode, cmd,
			(unsigned long)cdread_audio);
}

static int compat_cdrom_generic_command(struct block_device *bdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	struct cdrom_generic_command __user *cgc;
	struct compat_cdrom_generic_command __user *cgc32;
	u32 data;
	unsigned char dir;
	int itmp;

	cgc = compat_alloc_user_space(sizeof(*cgc));
	cgc32 = compat_ptr(arg);

	if (copy_in_user(&cgc->cmd, &cgc32->cmd, sizeof(cgc->cmd)) ||
	    get_user(data, &cgc32->buffer) ||
	    put_user(compat_ptr(data), &cgc->buffer) ||
	    copy_in_user(&cgc->buflen, &cgc32->buflen,
			 (sizeof(unsigned int) + sizeof(int))) ||
	    get_user(data, &cgc32->sense) ||
	    put_user(compat_ptr(data), &cgc->sense) ||
	    get_user(dir, &cgc32->data_direction) ||
	    put_user(dir, &cgc->data_direction) ||
	    get_user(itmp, &cgc32->quiet) ||
	    put_user(itmp, &cgc->quiet) ||
	    get_user(itmp, &cgc32->timeout) ||
	    put_user(itmp, &cgc->timeout) ||
	    get_user(data, &cgc32->reserved[0]) ||
	    put_user(compat_ptr(data), &cgc->reserved[0]))
		return -EFAULT;

	return __blkdev_driver_ioctl(bdev, mode, cmd, (unsigned long)cgc);
}

struct compat_blkpg_ioctl_arg {
	compat_int_t op;
	compat_int_t flags;
	compat_int_t datalen;
	compat_caddr_t data;
};

static int compat_blkpg_ioctl(struct block_device *bdev, fmode_t mode,
		unsigned int cmd, struct compat_blkpg_ioctl_arg __user *ua32)
{
	struct blkpg_ioctl_arg __user *a = compat_alloc_user_space(sizeof(*a));
	compat_caddr_t udata;
	compat_int_t n;
	int err;

	err = get_user(n, &ua32->op);
	err |= put_user(n, &a->op);
	err |= get_user(n, &ua32->flags);
	err |= put_user(n, &a->flags);
	err |= get_user(n, &ua32->datalen);
	err |= put_user(n, &a->datalen);
	err |= get_user(udata, &ua32->data);
	err |= put_user(compat_ptr(udata), &a->data);
	if (err)
		return err;

	return blkdev_ioctl(bdev, mode, cmd, (unsigned long)a);
}

#define BLKBSZGET_32		_IOR(0x12, 112, int)
#define BLKBSZSET_32		_IOW(0x12, 113, int)
#define BLKGETSIZE64_32		_IOR(0x12, 114, int)

static int compat_blkdev_driver_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned cmd, unsigned long arg)
{
	switch (cmd) {
	case HDIO_GET_UNMASKINTR:
	case HDIO_GET_MULTCOUNT:
	case HDIO_GET_KEEPSETTINGS:
	case HDIO_GET_32BIT:
	case HDIO_GET_NOWERR:
	case HDIO_GET_DMA:
	case HDIO_GET_NICE:
	case HDIO_GET_WCACHE:
	case HDIO_GET_ACOUSTIC:
	case HDIO_GET_ADDRESS:
	case HDIO_GET_BUSSTATE:
		return compat_hdio_ioctl(bdev, mode, cmd, arg);
	case CDROMREADAUDIO:
		return compat_cdrom_read_audio(bdev, mode, cmd, arg);
	case CDROM_SEND_PACKET:
		return compat_cdrom_generic_command(bdev, mode, cmd, arg);

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
	/* 0x330 is reserved -- it used to be HDIO_GETGEO_BIG */
	case 0x330:
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

	return __blkdev_driver_ioctl(bdev, mode, cmd, arg);
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
	fmode_t mode = file->f_mode;
	loff_t size;
	unsigned int max_sectors;

	/*
	 * O_NDELAY can be altered using fcntl(.., F_SETFL, ..), so we have
	 * to updated it before every ioctl.
	 */
	if (file->f_flags & O_NDELAY)
		mode |= FMODE_NDELAY;
	else
		mode &= ~FMODE_NDELAY;

	switch (cmd) {
	case HDIO_GETGEO:
		return compat_hdio_getgeo(disk, bdev, compat_ptr(arg));
	case BLKPBSZGET:
		return compat_put_uint(arg, bdev_physical_block_size(bdev));
	case BLKIOMIN:
		return compat_put_uint(arg, bdev_io_min(bdev));
	case BLKIOOPT:
		return compat_put_uint(arg, bdev_io_opt(bdev));
	case BLKALIGNOFF:
		return compat_put_int(arg, bdev_alignment_offset(bdev));
	case BLKDISCARDZEROES:
		return compat_put_uint(arg, 0);
	case BLKFLSBUF:
	case BLKROSET:
	case BLKDISCARD:
	case BLKSECDISCARD:
	case BLKZEROOUT:
	/*
	 * the ones below are implemented in blkdev_locked_ioctl,
	 * but we call blkdev_ioctl, which gets the lock for us
	 */
	case BLKRRPART:
		return blkdev_ioctl(bdev, mode, cmd,
				(unsigned long)compat_ptr(arg));
	case BLKBSZSET_32:
		return blkdev_ioctl(bdev, mode, BLKBSZSET,
				(unsigned long)compat_ptr(arg));
	case BLKPG:
		return compat_blkpg_ioctl(bdev, mode, cmd, compat_ptr(arg));
	case BLKRAGET:
	case BLKFRAGET:
		if (!arg)
			return -EINVAL;
		return compat_put_long(arg,
			       (bdev->bd_bdi->ra_pages * PAGE_SIZE) / 512);
	case BLKROGET: /* compatible */
		return compat_put_int(arg, bdev_read_only(bdev) != 0);
	case BLKBSZGET_32: /* get the logical block size (cf. BLKSSZGET) */
		return compat_put_int(arg, block_size(bdev));
	case BLKSSZGET: /* get block device hardware sector size */
		return compat_put_int(arg, bdev_logical_block_size(bdev));
	case BLKSECTGET:
		max_sectors = min_t(unsigned int, USHRT_MAX,
				    queue_max_sectors(bdev_get_queue(bdev)));
		return compat_put_ushort(arg, max_sectors);
	case BLKROTATIONAL:
		return compat_put_ushort(arg,
					 !blk_queue_nonrot(bdev_get_queue(bdev)));
	case BLKRASET: /* compatible, but no compat_ptr (!) */
	case BLKFRASET:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		bdev->bd_bdi->ra_pages = (arg * 512) / PAGE_SIZE;
		return 0;
	case BLKGETSIZE:
		size = i_size_read(bdev->bd_inode);
		if ((size >> 9) > ~0UL)
			return -EFBIG;
		return compat_put_ulong(arg, size >> 9);

	case BLKGETSIZE64_32:
		return compat_put_u64(arg, i_size_read(bdev->bd_inode));

	case BLKTRACESETUP32:
	case BLKTRACESTART: /* compatible */
	case BLKTRACESTOP:  /* compatible */
	case BLKTRACETEARDOWN: /* compatible */
		ret = blk_trace_ioctl(bdev, cmd, compat_ptr(arg));
		return ret;
	case IOC_PR_REGISTER:
	case IOC_PR_RESERVE:
	case IOC_PR_RELEASE:
	case IOC_PR_PREEMPT:
	case IOC_PR_PREEMPT_ABORT:
	case IOC_PR_CLEAR:
		return blkdev_ioctl(bdev, mode, cmd,
				(unsigned long)compat_ptr(arg));
	default:
		if (disk->fops->compat_ioctl)
			ret = disk->fops->compat_ioctl(bdev, mode, cmd, arg);
		if (ret == -ENOIOCTLCMD)
			ret = compat_blkdev_driver_ioctl(bdev, mode, cmd, arg);
		return ret;
	}
}
