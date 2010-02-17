/*
 * IDE ioctls handling.
 */

#include <linux/hdreg.h>
#include <linux/ide.h>

static const struct ide_ioctl_devset ide_ioctl_settings[] = {
{ HDIO_GET_32BIT,	 HDIO_SET_32BIT,	&ide_devset_io_32bit  },
{ HDIO_GET_KEEPSETTINGS, HDIO_SET_KEEPSETTINGS,	&ide_devset_keepsettings },
{ HDIO_GET_UNMASKINTR,	 HDIO_SET_UNMASKINTR,	&ide_devset_unmaskirq },
{ HDIO_GET_DMA,		 HDIO_SET_DMA,		&ide_devset_using_dma },
{ -1,			 HDIO_SET_PIO_MODE,	&ide_devset_pio_mode  },
{ 0 }
};

int ide_setting_ioctl(ide_drive_t *drive, struct block_device *bdev,
		      unsigned int cmd, unsigned long arg,
		      const struct ide_ioctl_devset *s)
{
	const struct ide_devset *ds;
	int err = -EOPNOTSUPP;

	for (; (ds = s->setting); s++) {
		if (ds->get && s->get_ioctl == cmd)
			goto read_val;
		else if (ds->set && s->set_ioctl == cmd)
			goto set_val;
	}

	return err;

read_val:
	mutex_lock(&ide_setting_mtx);
	err = ds->get(drive);
	mutex_unlock(&ide_setting_mtx);
	return err >= 0 ? put_user(err, (long __user *)arg) : err;

set_val:
	if (bdev != bdev->bd_contains)
		err = -EINVAL;
	else {
		if (!capable(CAP_SYS_ADMIN))
			err = -EACCES;
		else {
			mutex_lock(&ide_setting_mtx);
			err = ide_devset_execute(drive, ds, arg);
			mutex_unlock(&ide_setting_mtx);
		}
	}
	return err;
}
EXPORT_SYMBOL_GPL(ide_setting_ioctl);

static int ide_get_identity_ioctl(ide_drive_t *drive, unsigned int cmd,
				  unsigned long arg)
{
	u16 *id = NULL;
	int size = (cmd == HDIO_GET_IDENTITY) ? (ATA_ID_WORDS * 2) : 142;
	int rc = 0;

	if ((drive->dev_flags & IDE_DFLAG_ID_READ) == 0) {
		rc = -ENOMSG;
		goto out;
	}

	/* ata_id_to_hd_driveid() relies on 'id' to be fully allocated. */
	id = kmalloc(ATA_ID_WORDS * 2, GFP_KERNEL);
	if (id == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	memcpy(id, drive->id, size);
	ata_id_to_hd_driveid(id);

	if (copy_to_user((void __user *)arg, id, size))
		rc = -EFAULT;

	kfree(id);
out:
	return rc;
}

static int ide_get_nice_ioctl(ide_drive_t *drive, unsigned long arg)
{
	return put_user((!!(drive->dev_flags & IDE_DFLAG_DSC_OVERLAP)
			 << IDE_NICE_DSC_OVERLAP) |
			(!!(drive->dev_flags & IDE_DFLAG_NICE1)
			 << IDE_NICE_1), (long __user *)arg);
}

static int ide_set_nice_ioctl(ide_drive_t *drive, unsigned long arg)
{
	if (arg != (arg & ((1 << IDE_NICE_DSC_OVERLAP) | (1 << IDE_NICE_1))))
		return -EPERM;

	if (((arg >> IDE_NICE_DSC_OVERLAP) & 1) &&
	    (drive->media != ide_tape))
		return -EPERM;

	if ((arg >> IDE_NICE_DSC_OVERLAP) & 1)
		drive->dev_flags |= IDE_DFLAG_DSC_OVERLAP;
	else
		drive->dev_flags &= ~IDE_DFLAG_DSC_OVERLAP;

	if ((arg >> IDE_NICE_1) & 1)
		drive->dev_flags |= IDE_DFLAG_NICE1;
	else
		drive->dev_flags &= ~IDE_DFLAG_NICE1;

	return 0;
}

static int ide_cmd_ioctl(ide_drive_t *drive, unsigned long arg)
{
	u8 *buf = NULL;
	int bufsize = 0, err = 0;
	u8 args[4], xfer_rate = 0;
	struct ide_cmd cmd;
	struct ide_taskfile *tf = &cmd.tf;

	if (NULL == (void *) arg) {
		struct request *rq;

		rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
		rq->cmd_type = REQ_TYPE_ATA_TASKFILE;
		err = blk_execute_rq(drive->queue, NULL, rq, 0);
		blk_put_request(rq);

		return err;
	}

	if (copy_from_user(args, (void __user *)arg, 4))
		return -EFAULT;

	memset(&cmd, 0, sizeof(cmd));
	tf->feature = args[2];
	if (args[0] == ATA_CMD_SMART) {
		tf->nsect = args[3];
		tf->lbal  = args[1];
		tf->lbam  = 0x4f;
		tf->lbah  = 0xc2;
		cmd.valid.out.tf = IDE_VALID_OUT_TF;
		cmd.valid.in.tf  = IDE_VALID_NSECT;
	} else {
		tf->nsect = args[1];
		cmd.valid.out.tf = IDE_VALID_FEATURE | IDE_VALID_NSECT;
		cmd.valid.in.tf  = IDE_VALID_NSECT;
	}
	tf->command = args[0];
	cmd.protocol = args[3] ? ATA_PROT_PIO : ATA_PROT_NODATA;

	if (args[3]) {
		cmd.tf_flags |= IDE_TFLAG_IO_16BIT;
		bufsize = SECTOR_SIZE * args[3];
		buf = kzalloc(bufsize, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;
	}

	if (tf->command == ATA_CMD_SET_FEATURES &&
	    tf->feature == SETFEATURES_XFER &&
	    tf->nsect >= XFER_SW_DMA_0) {
		xfer_rate = ide_find_dma_mode(drive, tf->nsect);
		if (xfer_rate != tf->nsect) {
			err = -EINVAL;
			goto abort;
		}

		cmd.tf_flags |= IDE_TFLAG_SET_XFER;
	}

	err = ide_raw_taskfile(drive, &cmd, buf, args[3]);

	args[0] = tf->status;
	args[1] = tf->error;
	args[2] = tf->nsect;
abort:
	if (copy_to_user((void __user *)arg, &args, 4))
		err = -EFAULT;
	if (buf) {
		if (copy_to_user((void __user *)(arg + 4), buf, bufsize))
			err = -EFAULT;
		kfree(buf);
	}
	return err;
}

static int ide_task_ioctl(ide_drive_t *drive, unsigned long arg)
{
	void __user *p = (void __user *)arg;
	int err = 0;
	u8 args[7];
	struct ide_cmd cmd;

	if (copy_from_user(args, p, 7))
		return -EFAULT;

	memset(&cmd, 0, sizeof(cmd));
	memcpy(&cmd.tf.feature, &args[1], 6);
	cmd.tf.command = args[0];
	cmd.valid.out.tf = IDE_VALID_OUT_TF | IDE_VALID_DEVICE;
	cmd.valid.in.tf  = IDE_VALID_IN_TF  | IDE_VALID_DEVICE;

	err = ide_no_data_taskfile(drive, &cmd);

	args[0] = cmd.tf.command;
	memcpy(&args[1], &cmd.tf.feature, 6);

	if (copy_to_user(p, args, 7))
		err = -EFAULT;

	return err;
}

static int generic_drive_reset(ide_drive_t *drive)
{
	struct request *rq;
	int ret = 0;

	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd_len = 1;
	rq->cmd[0] = REQ_DRIVE_RESET;
	if (blk_execute_rq(drive->queue, NULL, rq, 1))
		ret = rq->errors;
	blk_put_request(rq);
	return ret;
}

int generic_ide_ioctl(ide_drive_t *drive, struct block_device *bdev,
		      unsigned int cmd, unsigned long arg)
{
	int err;

	err = ide_setting_ioctl(drive, bdev, cmd, arg, ide_ioctl_settings);
	if (err != -EOPNOTSUPP)
		return err;

	switch (cmd) {
	case HDIO_OBSOLETE_IDENTITY:
	case HDIO_GET_IDENTITY:
		if (bdev != bdev->bd_contains)
			return -EINVAL;
		return ide_get_identity_ioctl(drive, cmd, arg);
	case HDIO_GET_NICE:
		return ide_get_nice_ioctl(drive, arg);
	case HDIO_SET_NICE:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		return ide_set_nice_ioctl(drive, arg);
#ifdef CONFIG_IDE_TASK_IOCTL
	case HDIO_DRIVE_TASKFILE:
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			return -EACCES;
		if (drive->media == ide_disk)
			return ide_taskfile_ioctl(drive, arg);
		return -ENOMSG;
#endif
	case HDIO_DRIVE_CMD:
		if (!capable(CAP_SYS_RAWIO))
			return -EACCES;
		return ide_cmd_ioctl(drive, arg);
	case HDIO_DRIVE_TASK:
		if (!capable(CAP_SYS_RAWIO))
			return -EACCES;
		return ide_task_ioctl(drive, arg);
	case HDIO_DRIVE_RESET:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		return generic_drive_reset(drive);
	case HDIO_GET_BUSSTATE:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		if (put_user(BUSSTATE_ON, (long __user *)arg))
			return -EFAULT;
		return 0;
	case HDIO_SET_BUSSTATE:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(generic_ide_ioctl);
