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

	/*
	 * We need to set the startsect first, the driver may
	 * want to override it.
	 */
	geo.start = get_start_sect(bdev);
	ret = disk->fops->getgeo(bdev, &geo);
	if (ret)
		return ret;

	ret = copy_to_user(ugeo, &geo, 4);
	ret |= __put_user(geo.start, &ugeo->start);
	if (ret)
		ret = -EFAULT;

	return ret;
}

static int compat_hdio_ioctl(struct block_device *bdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	unsigned long kval;
	unsigned int __user *uvp;
	int error;

	set_fs(KERNEL_DS);
	error = __blkdev_driver_ioctl(bdev, mode,
				cmd, (unsigned long)(&kval));
	set_fs(old_fs);

	if (error == 0) {
		uvp = compat_ptr(arg);
		if (put_user(kval, uvp))
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

struct compat_floppy_struct {
	compat_uint_t	size;
	compat_uint_t	sect;
	compat_uint_t	head;
	compat_uint_t	track;
	compat_uint_t	stretch;
	unsigned char	gap;
	unsigned char	rate;
	unsigned char	spec1;
	unsigned char	fmt_gap;
	const compat_caddr_t name;
};

struct compat_floppy_drive_params {
	char		cmos;
	compat_ulong_t	max_dtr;
	compat_ulong_t	hlt;
	compat_ulong_t	hut;
	compat_ulong_t	srt;
	compat_ulong_t	spinup;
	compat_ulong_t	spindown;
	unsigned char	spindown_offset;
	unsigned char	select_delay;
	unsigned char	rps;
	unsigned char	tracks;
	compat_ulong_t	timeout;
	unsigned char	interleave_sect;
	struct floppy_max_errors max_errors;
	char		flags;
	char		read_track;
	short		autodetect[8];
	compat_int_t	checkfreq;
	compat_int_t	native_format;
};

struct compat_floppy_drive_struct {
	signed char	flags;
	compat_ulong_t	spinup_date;
	compat_ulong_t	select_date;
	compat_ulong_t	first_read_date;
	short		probed_format;
	short		track;
	short		maxblock;
	short		maxtrack;
	compat_int_t	generation;
	compat_int_t	keep_data;
	compat_int_t	fd_ref;
	compat_int_t	fd_device;
	compat_int_t	last_checked;
	compat_caddr_t dmabuf;
	compat_int_t	bufblocks;
};

struct compat_floppy_fdc_state {
	compat_int_t	spec1;
	compat_int_t	spec2;
	compat_int_t	dtr;
	unsigned char	version;
	unsigned char	dor;
	compat_ulong_t	address;
	unsigned int	rawcmd:2;
	unsigned int	reset:1;
	unsigned int	need_configure:1;
	unsigned int	perp_mode:2;
	unsigned int	has_fifo:1;
	unsigned int	driver_version;
	unsigned char	track[4];
};

struct compat_floppy_write_errors {
	unsigned int	write_errors;
	compat_ulong_t	first_error_sector;
	compat_int_t	first_error_generation;
	compat_ulong_t	last_error_sector;
	compat_int_t	last_error_generation;
	compat_uint_t	badness;
};

#define FDSETPRM32 _IOW(2, 0x42, struct compat_floppy_struct)
#define FDDEFPRM32 _IOW(2, 0x43, struct compat_floppy_struct)
#define FDGETPRM32 _IOR(2, 0x04, struct compat_floppy_struct)
#define FDSETDRVPRM32 _IOW(2, 0x90, struct compat_floppy_drive_params)
#define FDGETDRVPRM32 _IOR(2, 0x11, struct compat_floppy_drive_params)
#define FDGETDRVSTAT32 _IOR(2, 0x12, struct compat_floppy_drive_struct)
#define FDPOLLDRVSTAT32 _IOR(2, 0x13, struct compat_floppy_drive_struct)
#define FDGETFDCSTAT32 _IOR(2, 0x15, struct compat_floppy_fdc_state)
#define FDWERRORGET32  _IOR(2, 0x17, struct compat_floppy_write_errors)

static struct {
	unsigned int	cmd32;
	unsigned int	cmd;
} fd_ioctl_trans_table[] = {
	{ FDSETPRM32, FDSETPRM },
	{ FDDEFPRM32, FDDEFPRM },
	{ FDGETPRM32, FDGETPRM },
	{ FDSETDRVPRM32, FDSETDRVPRM },
	{ FDGETDRVPRM32, FDGETDRVPRM },
	{ FDGETDRVSTAT32, FDGETDRVSTAT },
	{ FDPOLLDRVSTAT32, FDPOLLDRVSTAT },
	{ FDGETFDCSTAT32, FDGETFDCSTAT },
	{ FDWERRORGET32, FDWERRORGET }
};

#define NR_FD_IOCTL_TRANS ARRAY_SIZE(fd_ioctl_trans_table)

static int compat_fd_ioctl(struct block_device *bdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	void *karg = NULL;
	unsigned int kcmd = 0;
	int i, err;

	for (i = 0; i < NR_FD_IOCTL_TRANS; i++)
		if (cmd == fd_ioctl_trans_table[i].cmd32) {
			kcmd = fd_ioctl_trans_table[i].cmd;
			break;
		}
	if (!kcmd)
		return -EINVAL;

	switch (cmd) {
	case FDSETPRM32:
	case FDDEFPRM32:
	case FDGETPRM32:
	{
		compat_uptr_t name;
		struct compat_floppy_struct __user *uf;
		struct floppy_struct *f;

		uf = compat_ptr(arg);
		f = karg = kmalloc(sizeof(struct floppy_struct), GFP_KERNEL);
		if (!karg)
			return -ENOMEM;
		if (cmd == FDGETPRM32)
			break;
		err = __get_user(f->size, &uf->size);
		err |= __get_user(f->sect, &uf->sect);
		err |= __get_user(f->head, &uf->head);
		err |= __get_user(f->track, &uf->track);
		err |= __get_user(f->stretch, &uf->stretch);
		err |= __get_user(f->gap, &uf->gap);
		err |= __get_user(f->rate, &uf->rate);
		err |= __get_user(f->spec1, &uf->spec1);
		err |= __get_user(f->fmt_gap, &uf->fmt_gap);
		err |= __get_user(name, &uf->name);
		f->name = compat_ptr(name);
		if (err) {
			err = -EFAULT;
			goto out;
		}
		break;
	}
	case FDSETDRVPRM32:
	case FDGETDRVPRM32:
	{
		struct compat_floppy_drive_params __user *uf;
		struct floppy_drive_params *f;

		uf = compat_ptr(arg);
		f = karg = kmalloc(sizeof(struct floppy_drive_params), GFP_KERNEL);
		if (!karg)
			return -ENOMEM;
		if (cmd == FDGETDRVPRM32)
			break;
		err = __get_user(f->cmos, &uf->cmos);
		err |= __get_user(f->max_dtr, &uf->max_dtr);
		err |= __get_user(f->hlt, &uf->hlt);
		err |= __get_user(f->hut, &uf->hut);
		err |= __get_user(f->srt, &uf->srt);
		err |= __get_user(f->spinup, &uf->spinup);
		err |= __get_user(f->spindown, &uf->spindown);
		err |= __get_user(f->spindown_offset, &uf->spindown_offset);
		err |= __get_user(f->select_delay, &uf->select_delay);
		err |= __get_user(f->rps, &uf->rps);
		err |= __get_user(f->tracks, &uf->tracks);
		err |= __get_user(f->timeout, &uf->timeout);
		err |= __get_user(f->interleave_sect, &uf->interleave_sect);
		err |= __copy_from_user(&f->max_errors, &uf->max_errors, sizeof(f->max_errors));
		err |= __get_user(f->flags, &uf->flags);
		err |= __get_user(f->read_track, &uf->read_track);
		err |= __copy_from_user(f->autodetect, uf->autodetect, sizeof(f->autodetect));
		err |= __get_user(f->checkfreq, &uf->checkfreq);
		err |= __get_user(f->native_format, &uf->native_format);
		if (err) {
			err = -EFAULT;
			goto out;
		}
		break;
	}
	case FDGETDRVSTAT32:
	case FDPOLLDRVSTAT32:
		karg = kmalloc(sizeof(struct floppy_drive_struct), GFP_KERNEL);
		if (!karg)
			return -ENOMEM;
		break;
	case FDGETFDCSTAT32:
		karg = kmalloc(sizeof(struct floppy_fdc_state), GFP_KERNEL);
		if (!karg)
			return -ENOMEM;
		break;
	case FDWERRORGET32:
		karg = kmalloc(sizeof(struct floppy_write_errors), GFP_KERNEL);
		if (!karg)
			return -ENOMEM;
		break;
	default:
		return -EINVAL;
	}
	set_fs(KERNEL_DS);
	err = __blkdev_driver_ioctl(bdev, mode, kcmd, (unsigned long)karg);
	set_fs(old_fs);
	if (err)
		goto out;
	switch (cmd) {
	case FDGETPRM32:
	{
		struct floppy_struct *f = karg;
		struct compat_floppy_struct __user *uf = compat_ptr(arg);

		err = __put_user(f->size, &uf->size);
		err |= __put_user(f->sect, &uf->sect);
		err |= __put_user(f->head, &uf->head);
		err |= __put_user(f->track, &uf->track);
		err |= __put_user(f->stretch, &uf->stretch);
		err |= __put_user(f->gap, &uf->gap);
		err |= __put_user(f->rate, &uf->rate);
		err |= __put_user(f->spec1, &uf->spec1);
		err |= __put_user(f->fmt_gap, &uf->fmt_gap);
		err |= __put_user((u64)f->name, (compat_caddr_t __user *)&uf->name);
		break;
	}
	case FDGETDRVPRM32:
	{
		struct compat_floppy_drive_params __user *uf;
		struct floppy_drive_params *f = karg;

		uf = compat_ptr(arg);
		err = __put_user(f->cmos, &uf->cmos);
		err |= __put_user(f->max_dtr, &uf->max_dtr);
		err |= __put_user(f->hlt, &uf->hlt);
		err |= __put_user(f->hut, &uf->hut);
		err |= __put_user(f->srt, &uf->srt);
		err |= __put_user(f->spinup, &uf->spinup);
		err |= __put_user(f->spindown, &uf->spindown);
		err |= __put_user(f->spindown_offset, &uf->spindown_offset);
		err |= __put_user(f->select_delay, &uf->select_delay);
		err |= __put_user(f->rps, &uf->rps);
		err |= __put_user(f->tracks, &uf->tracks);
		err |= __put_user(f->timeout, &uf->timeout);
		err |= __put_user(f->interleave_sect, &uf->interleave_sect);
		err |= __copy_to_user(&uf->max_errors, &f->max_errors, sizeof(f->max_errors));
		err |= __put_user(f->flags, &uf->flags);
		err |= __put_user(f->read_track, &uf->read_track);
		err |= __copy_to_user(uf->autodetect, f->autodetect, sizeof(f->autodetect));
		err |= __put_user(f->checkfreq, &uf->checkfreq);
		err |= __put_user(f->native_format, &uf->native_format);
		break;
	}
	case FDGETDRVSTAT32:
	case FDPOLLDRVSTAT32:
	{
		struct compat_floppy_drive_struct __user *uf;
		struct floppy_drive_struct *f = karg;

		uf = compat_ptr(arg);
		err = __put_user(f->flags, &uf->flags);
		err |= __put_user(f->spinup_date, &uf->spinup_date);
		err |= __put_user(f->select_date, &uf->select_date);
		err |= __put_user(f->first_read_date, &uf->first_read_date);
		err |= __put_user(f->probed_format, &uf->probed_format);
		err |= __put_user(f->track, &uf->track);
		err |= __put_user(f->maxblock, &uf->maxblock);
		err |= __put_user(f->maxtrack, &uf->maxtrack);
		err |= __put_user(f->generation, &uf->generation);
		err |= __put_user(f->keep_data, &uf->keep_data);
		err |= __put_user(f->fd_ref, &uf->fd_ref);
		err |= __put_user(f->fd_device, &uf->fd_device);
		err |= __put_user(f->last_checked, &uf->last_checked);
		err |= __put_user((u64)f->dmabuf, &uf->dmabuf);
		err |= __put_user((u64)f->bufblocks, &uf->bufblocks);
		break;
	}
	case FDGETFDCSTAT32:
	{
		struct compat_floppy_fdc_state __user *uf;
		struct floppy_fdc_state *f = karg;

		uf = compat_ptr(arg);
		err = __put_user(f->spec1, &uf->spec1);
		err |= __put_user(f->spec2, &uf->spec2);
		err |= __put_user(f->dtr, &uf->dtr);
		err |= __put_user(f->version, &uf->version);
		err |= __put_user(f->dor, &uf->dor);
		err |= __put_user(f->address, &uf->address);
		err |= __copy_to_user((char __user *)&uf->address + sizeof(uf->address),
				   (char *)&f->address + sizeof(f->address), sizeof(int));
		err |= __put_user(f->driver_version, &uf->driver_version);
		err |= __copy_to_user(uf->track, f->track, sizeof(f->track));
		break;
	}
	case FDWERRORGET32:
	{
		struct compat_floppy_write_errors __user *uf;
		struct floppy_write_errors *f = karg;

		uf = compat_ptr(arg);
		err = __put_user(f->write_errors, &uf->write_errors);
		err |= __put_user(f->first_error_sector, &uf->first_error_sector);
		err |= __put_user(f->first_error_generation, &uf->first_error_generation);
		err |= __put_user(f->last_error_sector, &uf->last_error_sector);
		err |= __put_user(f->last_error_generation, &uf->last_error_generation);
		err |= __put_user(f->badness, &uf->badness);
		break;
	}
	default:
		break;
	}
	if (err)
		err = -EFAULT;

out:
	kfree(karg);
	return err;
}

struct compat_blk_user_trace_setup {
	char name[32];
	u16 act_mask;
	u32 buf_size;
	u32 buf_nr;
	compat_u64 start_lba;
	compat_u64 end_lba;
	u32 pid;
};
#define BLKTRACESETUP32 _IOWR(0x12, 115, struct compat_blk_user_trace_setup)

static int compat_blk_trace_setup(struct block_device *bdev, char __user *arg)
{
	struct blk_user_trace_setup buts;
	struct compat_blk_user_trace_setup cbuts;
	struct request_queue *q;
	char b[BDEVNAME_SIZE];
	int ret;

	q = bdev_get_queue(bdev);
	if (!q)
		return -ENXIO;

	if (copy_from_user(&cbuts, arg, sizeof(cbuts)))
		return -EFAULT;

	bdevname(bdev, b);

	buts = (struct blk_user_trace_setup) {
		.act_mask = cbuts.act_mask,
		.buf_size = cbuts.buf_size,
		.buf_nr = cbuts.buf_nr,
		.start_lba = cbuts.start_lba,
		.end_lba = cbuts.end_lba,
		.pid = cbuts.pid,
	};
	memcpy(&buts.name, &cbuts.name, 32);

	mutex_lock(&bdev->bd_mutex);
	ret = do_blk_trace_setup(q, b, bdev->bd_dev, bdev, &buts);
	mutex_unlock(&bdev->bd_mutex);
	if (ret)
		return ret;

	if (copy_to_user(arg, &buts.name, 32))
		return -EFAULT;

	return 0;
}

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
	case FDSETPRM32:
	case FDDEFPRM32:
	case FDGETPRM32:
	case FDSETDRVPRM32:
	case FDGETDRVPRM32:
	case FDGETDRVSTAT32:
	case FDPOLLDRVSTAT32:
	case FDGETFDCSTAT32:
	case FDWERRORGET32:
		return compat_fd_ioctl(bdev, mode, cmd, arg);
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
	struct backing_dev_info *bdi;
	loff_t size;

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
	case BLKFLSBUF:
	case BLKROSET:
	case BLKDISCARD:
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
		return compat_put_int(arg, bdev_logical_block_size(bdev));
	case BLKSECTGET:
		return compat_put_ushort(arg,
					 queue_max_sectors(bdev_get_queue(bdev)));
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
		size = bdev->bd_inode->i_size;
		if ((size >> 9) > ~0UL)
			return -EFBIG;
		return compat_put_ulong(arg, size >> 9);

	case BLKGETSIZE64_32:
		return compat_put_u64(arg, bdev->bd_inode->i_size);

	case BLKTRACESETUP32:
		lock_kernel();
		ret = compat_blk_trace_setup(bdev, compat_ptr(arg));
		unlock_kernel();
		return ret;
	case BLKTRACESTART: /* compatible */
	case BLKTRACESTOP:  /* compatible */
	case BLKTRACETEARDOWN: /* compatible */
		lock_kernel();
		ret = blk_trace_ioctl(bdev, cmd, compat_ptr(arg));
		unlock_kernel();
		return ret;
	default:
		if (disk->fops->compat_ioctl)
			ret = disk->fops->compat_ioctl(bdev, mode, cmd, arg);
		if (ret == -ENOIOCTLCMD)
			ret = compat_blkdev_driver_ioctl(bdev, mode, cmd, arg);
		return ret;
	}
}
