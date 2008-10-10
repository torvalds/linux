/*
 * ide-floppy IOCTLs handling.
 */

#include <linux/kernel.h>
#include <linux/ide.h>
#include <linux/cdrom.h>

#include <asm/unaligned.h>

#include <scsi/scsi_ioctl.h>

#include "ide-floppy.h"

/*
 * Obtain the list of formattable capacities.
 * Very similar to ide_floppy_get_capacity, except that we push the capacity
 * descriptors to userland, instead of our own structures.
 *
 * Userland gives us the following structure:
 *
 * struct idefloppy_format_capacities {
 *	int nformats;
 *	struct {
 *		int nblocks;
 *		int blocksize;
 *	} formats[];
 * };
 *
 * userland initializes nformats to the number of allocated formats[] records.
 * On exit we set nformats to the number of records we've actually initialized.
 */

static int ide_floppy_get_format_capacities(ide_drive_t *drive, int __user *arg)
{
	struct ide_floppy_obj *floppy = drive->driver_data;
	struct ide_atapi_pc pc;
	u8 header_len, desc_cnt;
	int i, blocks, length, u_array_size, u_index;
	int __user *argp;

	if (get_user(u_array_size, arg))
		return -EFAULT;

	if (u_array_size <= 0)
		return -EINVAL;

	ide_floppy_create_read_capacity_cmd(&pc);
	if (ide_queue_pc_tail(drive, floppy->disk, &pc)) {
		printk(KERN_ERR "ide-floppy: Can't get floppy parameters\n");
		return -EIO;
	}

	header_len = pc.buf[3];
	desc_cnt = header_len / 8; /* capacity descriptor of 8 bytes */

	u_index = 0;
	argp = arg + 1;

	/*
	 * We always skip the first capacity descriptor.  That's the current
	 * capacity.  We are interested in the remaining descriptors, the
	 * formattable capacities.
	 */
	for (i = 1; i < desc_cnt; i++) {
		unsigned int desc_start = 4 + i*8;

		if (u_index >= u_array_size)
			break;	/* User-supplied buffer too small */

		blocks = be32_to_cpup((__be32 *)&pc.buf[desc_start]);
		length = be16_to_cpup((__be16 *)&pc.buf[desc_start + 6]);

		if (put_user(blocks, argp))
			return -EFAULT;

		++argp;

		if (put_user(length, argp))
			return -EFAULT;

		++argp;

		++u_index;
	}

	if (put_user(u_index, arg))
		return -EFAULT;

	return 0;
}

static void ide_floppy_create_format_unit_cmd(struct ide_atapi_pc *pc, int b,
		int l, int flags)
{
	ide_init_pc(pc);
	pc->c[0] = GPCMD_FORMAT_UNIT;
	pc->c[1] = 0x17;

	memset(pc->buf, 0, 12);
	pc->buf[1] = 0xA2;
	/* Default format list header, u8 1: FOV/DCRT/IMM bits set */

	if (flags & 1)				/* Verify bit on... */
		pc->buf[1] ^= 0x20;		/* ... turn off DCRT bit */
	pc->buf[3] = 8;

	put_unaligned(cpu_to_be32(b), (unsigned int *)(&pc->buf[4]));
	put_unaligned(cpu_to_be32(l), (unsigned int *)(&pc->buf[8]));
	pc->buf_size = 12;
	pc->flags |= PC_FLAG_WRITING;
}

static int ide_floppy_get_sfrp_bit(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct ide_atapi_pc pc;

	drive->atapi_flags &= ~IDE_AFLAG_SRFP;

	ide_floppy_create_mode_sense_cmd(&pc, IDEFLOPPY_CAPABILITIES_PAGE);
	pc.flags |= PC_FLAG_SUPPRESS_ERROR;

	if (ide_queue_pc_tail(drive, floppy->disk, &pc))
		return 1;

	if (pc.buf[8 + 2] & 0x40)
		drive->atapi_flags |= IDE_AFLAG_SRFP;

	return 0;
}

static int ide_floppy_format_unit(ide_drive_t *drive, int __user *arg)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct ide_atapi_pc pc;
	int blocks, length, flags, err = 0;

	if (floppy->openers > 1) {
		/* Don't format if someone is using the disk */
		drive->atapi_flags &= ~IDE_AFLAG_FORMAT_IN_PROGRESS;
		return -EBUSY;
	}

	drive->atapi_flags |= IDE_AFLAG_FORMAT_IN_PROGRESS;

	/*
	 * Send ATAPI_FORMAT_UNIT to the drive.
	 *
	 * Userland gives us the following structure:
	 *
	 * struct idefloppy_format_command {
	 *        int nblocks;
	 *        int blocksize;
	 *        int flags;
	 *        } ;
	 *
	 * flags is a bitmask, currently, the only defined flag is:
	 *
	 *        0x01 - verify media after format.
	 */
	if (get_user(blocks, arg) ||
			get_user(length, arg+1) ||
			get_user(flags, arg+2)) {
		err = -EFAULT;
		goto out;
	}

	(void)ide_floppy_get_sfrp_bit(drive);
	ide_floppy_create_format_unit_cmd(&pc, blocks, length, flags);

	if (ide_queue_pc_tail(drive, floppy->disk, &pc))
		err = -EIO;

out:
	if (err)
		drive->atapi_flags &= ~IDE_AFLAG_FORMAT_IN_PROGRESS;
	return err;
}

/*
 * Get ATAPI_FORMAT_UNIT progress indication.
 *
 * Userland gives a pointer to an int.  The int is set to a progress
 * indicator 0-65536, with 65536=100%.
 *
 * If the drive does not support format progress indication, we just check
 * the dsc bit, and return either 0 or 65536.
 */

static int ide_floppy_get_format_progress(ide_drive_t *drive, int __user *arg)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct ide_atapi_pc pc;
	int progress_indication = 0x10000;

	if (drive->atapi_flags & IDE_AFLAG_SRFP) {
		ide_floppy_create_request_sense_cmd(&pc);
		if (ide_queue_pc_tail(drive, floppy->disk, &pc))
			return -EIO;

		if (floppy->sense_key == 2 &&
		    floppy->asc == 4 &&
		    floppy->ascq == 4)
			progress_indication = floppy->progress_indication;

		/* Else assume format_unit has finished, and we're at 0x10000 */
	} else {
		ide_hwif_t *hwif = drive->hwif;
		unsigned long flags;
		u8 stat;

		local_irq_save(flags);
		stat = hwif->tp_ops->read_status(hwif);
		local_irq_restore(flags);

		progress_indication = ((stat & ATA_DSC) == 0) ? 0 : 0x10000;
	}

	if (put_user(progress_indication, arg))
		return -EFAULT;

	return 0;
}

int ide_floppy_format_ioctl(ide_drive_t *drive, struct file *file,
			    unsigned int cmd, void __user *argp)
{
	switch (cmd) {
	case IDEFLOPPY_IOCTL_FORMAT_SUPPORTED:
		return 0;
	case IDEFLOPPY_IOCTL_FORMAT_GET_CAPACITY:
		return ide_floppy_get_format_capacities(drive, argp);
	case IDEFLOPPY_IOCTL_FORMAT_START:
		if (!(file->f_mode & 2))
			return -EPERM;
		return ide_floppy_format_unit(drive, (int __user *)argp);
	case IDEFLOPPY_IOCTL_FORMAT_GET_PROGRESS:
		return ide_floppy_get_format_progress(drive, argp);
	default:
		return -ENOTTY;
	}
}
