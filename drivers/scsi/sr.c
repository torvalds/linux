/*
 *  sr.c Copyright (C) 1992 David Giller
 *           Copyright (C) 1993, 1994, 1995, 1999 Eric Youngdale
 *
 *  adapted from:
 *      sd.c Copyright (C) 1992 Drew Eckhardt
 *      Linux scsi disk driver by
 *              Drew Eckhardt <drew@colorado.edu>
 *
 *	Modified by Eric Youngdale ericy@andante.org to
 *	add scatter-gather, multiple outstanding request, and other
 *	enhancements.
 *
 *      Modified by Eric Youngdale eric@andante.org to support loadable
 *      low-level scsi drivers.
 *
 *      Modified by Thomas Quinot thomas@melchior.cuivre.fdn.fr to
 *      provide auto-eject.
 *
 *      Modified by Gerd Knorr <kraxel@cs.tu-berlin.de> to support the
 *      generic cdrom interface
 *
 *      Modified by Jens Axboe <axboe@suse.de> - Uniform sr_packet()
 *      interface, capabilities probe additions, ioctl cleanups, etc.
 *
 *	Modified by Richard Gooch <rgooch@atnf.csiro.au> to support devfs
 *
 *	Modified by Jens Axboe <axboe@suse.de> - support DVD-RAM
 *	transparently and lose the GHOST hack
 *
 *	Modified by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *	check resource allocation in sr_init and some cleanups
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/cdrom.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>	/* For the door lock/unlock commands */

#include "scsi_logging.h"
#include "sr.h"


MODULE_DESCRIPTION("SCSI cdrom (sr) driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_BLOCKDEV_MAJOR(SCSI_CDROM_MAJOR);
MODULE_ALIAS_SCSI_DEVICE(TYPE_ROM);
MODULE_ALIAS_SCSI_DEVICE(TYPE_WORM);

#define SR_DISKS	256

#define SR_CAPABILITIES \
	(CDC_CLOSE_TRAY|CDC_OPEN_TRAY|CDC_LOCK|CDC_SELECT_SPEED| \
	 CDC_SELECT_DISC|CDC_MULTI_SESSION|CDC_MCN|CDC_MEDIA_CHANGED| \
	 CDC_PLAY_AUDIO|CDC_RESET|CDC_DRIVE_STATUS| \
	 CDC_CD_R|CDC_CD_RW|CDC_DVD|CDC_DVD_R|CDC_DVD_RAM|CDC_GENERIC_PACKET| \
	 CDC_MRW|CDC_MRW_W|CDC_RAM)

static int sr_probe(struct device *);
static int sr_remove(struct device *);
static int sr_done(struct scsi_cmnd *);

static struct scsi_driver sr_template = {
	.owner			= THIS_MODULE,
	.gendrv = {
		.name   	= "sr",
		.probe		= sr_probe,
		.remove		= sr_remove,
	},
	.done			= sr_done,
};

static unsigned long sr_index_bits[SR_DISKS / BITS_PER_LONG];
static DEFINE_SPINLOCK(sr_index_lock);

/* This semaphore is used to mediate the 0->1 reference get in the
 * face of object destruction (i.e. we can't allow a get on an
 * object after last put) */
static DEFINE_MUTEX(sr_ref_mutex);

static int sr_open(struct cdrom_device_info *, int);
static void sr_release(struct cdrom_device_info *);

static void get_sectorsize(struct scsi_cd *);
static void get_capabilities(struct scsi_cd *);

static int sr_media_change(struct cdrom_device_info *, int);
static int sr_packet(struct cdrom_device_info *, struct packet_command *);

static struct cdrom_device_ops sr_dops = {
	.open			= sr_open,
	.release	 	= sr_release,
	.drive_status	 	= sr_drive_status,
	.media_changed		= sr_media_change,
	.tray_move		= sr_tray_move,
	.lock_door		= sr_lock_door,
	.select_speed		= sr_select_speed,
	.get_last_session	= sr_get_last_session,
	.get_mcn		= sr_get_mcn,
	.reset			= sr_reset,
	.audio_ioctl		= sr_audio_ioctl,
	.capability		= SR_CAPABILITIES,
	.generic_packet		= sr_packet,
};

static void sr_kref_release(struct kref *kref);

static inline struct scsi_cd *scsi_cd(struct gendisk *disk)
{
	return container_of(disk->private_data, struct scsi_cd, driver);
}

/*
 * The get and put routines for the struct scsi_cd.  Note this entity
 * has a scsi_device pointer and owns a reference to this.
 */
static inline struct scsi_cd *scsi_cd_get(struct gendisk *disk)
{
	struct scsi_cd *cd = NULL;

	mutex_lock(&sr_ref_mutex);
	if (disk->private_data == NULL)
		goto out;
	cd = scsi_cd(disk);
	kref_get(&cd->kref);
	if (scsi_device_get(cd->device))
		goto out_put;
	goto out;

 out_put:
	kref_put(&cd->kref, sr_kref_release);
	cd = NULL;
 out:
	mutex_unlock(&sr_ref_mutex);
	return cd;
}

static void scsi_cd_put(struct scsi_cd *cd)
{
	struct scsi_device *sdev = cd->device;

	mutex_lock(&sr_ref_mutex);
	kref_put(&cd->kref, sr_kref_release);
	scsi_device_put(sdev);
	mutex_unlock(&sr_ref_mutex);
}

/* identical to scsi_test_unit_ready except that it doesn't
 * eat the NOT_READY returns for removable media */
int sr_test_unit_ready(struct scsi_device *sdev, struct scsi_sense_hdr *sshdr)
{
	int retries = MAX_RETRIES;
	int the_result;
	u8 cmd[] = {TEST_UNIT_READY, 0, 0, 0, 0, 0 };

	/* issue TEST_UNIT_READY until the initial startup UNIT_ATTENTION
	 * conditions are gone, or a timeout happens
	 */
	do {
		the_result = scsi_execute_req(sdev, cmd, DMA_NONE, NULL,
					      0, sshdr, SR_TIMEOUT,
					      retries--);
		if (scsi_sense_valid(sshdr) &&
		    sshdr->sense_key == UNIT_ATTENTION)
			sdev->changed = 1;

	} while (retries > 0 &&
		 (!scsi_status_is_good(the_result) ||
		  (scsi_sense_valid(sshdr) &&
		   sshdr->sense_key == UNIT_ATTENTION)));
	return the_result;
}

/*
 * This function checks to see if the media has been changed in the
 * CDROM drive.  It is possible that we have already sensed a change,
 * or the drive may have sensed one and not yet reported it.  We must
 * be ready for either case. This function always reports the current
 * value of the changed bit.  If flag is 0, then the changed bit is reset.
 * This function could be done as an ioctl, but we would need to have
 * an inode for that to work, and we do not always have one.
 */

static int sr_media_change(struct cdrom_device_info *cdi, int slot)
{
	struct scsi_cd *cd = cdi->handle;
	int retval;
	struct scsi_sense_hdr *sshdr;

	if (CDSL_CURRENT != slot) {
		/* no changer support */
		return -EINVAL;
	}

	sshdr =  kzalloc(sizeof(*sshdr), GFP_KERNEL);
	retval = sr_test_unit_ready(cd->device, sshdr);
	if (retval || (scsi_sense_valid(sshdr) &&
		       /* 0x3a is medium not present */
		       sshdr->asc == 0x3a)) {
		/* Media not present or unable to test, unit probably not
		 * ready. This usually means there is no disc in the drive.
		 * Mark as changed, and we will figure it out later once
		 * the drive is available again.
		 */
		cd->device->changed = 1;
		/* This will force a flush, if called from check_disk_change */
		retval = 1;
		goto out;
	};

	retval = cd->device->changed;
	cd->device->changed = 0;
	/* If the disk changed, the capacity will now be different,
	 * so we force a re-read of this information */
	if (retval) {
		/* check multisession offset etc */
		sr_cd_check(cdi);
		get_sectorsize(cd);
	}

out:
	/* Notify userspace, that media has changed. */
	if (retval != cd->previous_state)
		sdev_evt_send_simple(cd->device, SDEV_EVT_MEDIA_CHANGE,
				     GFP_KERNEL);
	cd->previous_state = retval;
	kfree(sshdr);

	return retval;
}
 
/*
 * sr_done is the interrupt routine for the device driver.
 *
 * It will be notified on the end of a SCSI read / write, and will take one
 * of several actions based on success or failure.
 */
static int sr_done(struct scsi_cmnd *SCpnt)
{
	int result = SCpnt->result;
	int this_count = scsi_bufflen(SCpnt);
	int good_bytes = (result == 0 ? this_count : 0);
	int block_sectors = 0;
	long error_sector;
	struct scsi_cd *cd = scsi_cd(SCpnt->request->rq_disk);

#ifdef DEBUG
	printk("sr.c done: %x\n", result);
#endif

	/*
	 * Handle MEDIUM ERRORs or VOLUME OVERFLOWs that indicate partial
	 * success.  Since this is a relatively rare error condition, no
	 * care is taken to avoid unnecessary additional work such as
	 * memcpy's that could be avoided.
	 */
	if (driver_byte(result) != 0 &&		/* An error occurred */
	    (SCpnt->sense_buffer[0] & 0x7f) == 0x70) { /* Sense current */
		switch (SCpnt->sense_buffer[2]) {
		case MEDIUM_ERROR:
		case VOLUME_OVERFLOW:
		case ILLEGAL_REQUEST:
			if (!(SCpnt->sense_buffer[0] & 0x90))
				break;
			error_sector = (SCpnt->sense_buffer[3] << 24) |
				(SCpnt->sense_buffer[4] << 16) |
				(SCpnt->sense_buffer[5] << 8) |
				SCpnt->sense_buffer[6];
			if (SCpnt->request->bio != NULL)
				block_sectors =
					bio_sectors(SCpnt->request->bio);
			if (block_sectors < 4)
				block_sectors = 4;
			if (cd->device->sector_size == 2048)
				error_sector <<= 2;
			error_sector &= ~(block_sectors - 1);
			good_bytes = (error_sector - SCpnt->request->sector) << 9;
			if (good_bytes < 0 || good_bytes >= this_count)
				good_bytes = 0;
			/*
			 * The SCSI specification allows for the value
			 * returned by READ CAPACITY to be up to 75 2K
			 * sectors past the last readable block.
			 * Therefore, if we hit a medium error within the
			 * last 75 2K sectors, we decrease the saved size
			 * value.
			 */
			if (error_sector < get_capacity(cd->disk) &&
			    cd->capacity - error_sector < 4 * 75)
				set_capacity(cd->disk, error_sector);
			break;

		case RECOVERED_ERROR:

			/*
			 * An error occured, but it recovered.  Inform the
			 * user, but make sure that it's not treated as a
			 * hard error.
			 */
			scsi_print_sense("sr", SCpnt);
			SCpnt->result = 0;
			SCpnt->sense_buffer[0] = 0x0;
			good_bytes = this_count;
			break;

		default:
			break;
		}
	}

	return good_bytes;
}

static int sr_prep_fn(struct request_queue *q, struct request *rq)
{
	int block = 0, this_count, s_size;
	struct scsi_cd *cd;
	struct scsi_cmnd *SCpnt;
	struct scsi_device *sdp = q->queuedata;
	int ret;

	if (rq->cmd_type == REQ_TYPE_BLOCK_PC) {
		ret = scsi_setup_blk_pc_cmnd(sdp, rq);
		goto out;
	} else if (rq->cmd_type != REQ_TYPE_FS) {
		ret = BLKPREP_KILL;
		goto out;
	}
	ret = scsi_setup_fs_cmnd(sdp, rq);
	if (ret != BLKPREP_OK)
		goto out;
	SCpnt = rq->special;
	cd = scsi_cd(rq->rq_disk);

	/* from here on until we're complete, any goto out
	 * is used for a killable error condition */
	ret = BLKPREP_KILL;

	SCSI_LOG_HLQUEUE(1, printk("Doing sr request, dev = %s, block = %d\n",
				cd->disk->disk_name, block));

	if (!cd->device || !scsi_device_online(cd->device)) {
		SCSI_LOG_HLQUEUE(2, printk("Finishing %ld sectors\n",
					rq->nr_sectors));
		SCSI_LOG_HLQUEUE(2, printk("Retry with 0x%p\n", SCpnt));
		goto out;
	}

	if (cd->device->changed) {
		/*
		 * quietly refuse to do anything to a changed disc until the
		 * changed bit has been reset
		 */
		goto out;
	}

	/*
	 * we do lazy blocksize switching (when reading XA sectors,
	 * see CDROMREADMODE2 ioctl) 
	 */
	s_size = cd->device->sector_size;
	if (s_size > 2048) {
		if (!in_interrupt())
			sr_set_blocklength(cd, 2048);
		else
			printk("sr: can't switch blocksize: in interrupt\n");
	}

	if (s_size != 512 && s_size != 1024 && s_size != 2048) {
		scmd_printk(KERN_ERR, SCpnt, "bad sector size %d\n", s_size);
		goto out;
	}

	if (rq_data_dir(rq) == WRITE) {
		if (!cd->device->writeable)
			goto out;
		SCpnt->cmnd[0] = WRITE_10;
		SCpnt->sc_data_direction = DMA_TO_DEVICE;
 	 	cd->cdi.media_written = 1;
	} else if (rq_data_dir(rq) == READ) {
		SCpnt->cmnd[0] = READ_10;
		SCpnt->sc_data_direction = DMA_FROM_DEVICE;
	} else {
		blk_dump_rq_flags(rq, "Unknown sr command");
		goto out;
	}

	{
		struct scatterlist *sg;
		int i, size = 0, sg_count = scsi_sg_count(SCpnt);

		scsi_for_each_sg(SCpnt, sg, sg_count, i)
			size += sg->length;

		if (size != scsi_bufflen(SCpnt)) {
			scmd_printk(KERN_ERR, SCpnt,
				"mismatch count %d, bytes %d\n",
				size, scsi_bufflen(SCpnt));
			if (scsi_bufflen(SCpnt) > size)
				SCpnt->sdb.length = size;
		}
	}

	/*
	 * request doesn't start on hw block boundary, add scatter pads
	 */
	if (((unsigned int)rq->sector % (s_size >> 9)) ||
	    (scsi_bufflen(SCpnt) % s_size)) {
		scmd_printk(KERN_NOTICE, SCpnt, "unaligned transfer\n");
		goto out;
	}

	this_count = (scsi_bufflen(SCpnt) >> 9) / (s_size >> 9);


	SCSI_LOG_HLQUEUE(2, printk("%s : %s %d/%ld 512 byte blocks.\n",
				cd->cdi.name,
				(rq_data_dir(rq) == WRITE) ?
					"writing" : "reading",
				this_count, rq->nr_sectors));

	SCpnt->cmnd[1] = 0;
	block = (unsigned int)rq->sector / (s_size >> 9);

	if (this_count > 0xffff) {
		this_count = 0xffff;
		SCpnt->sdb.length = this_count * s_size;
	}

	SCpnt->cmnd[2] = (unsigned char) (block >> 24) & 0xff;
	SCpnt->cmnd[3] = (unsigned char) (block >> 16) & 0xff;
	SCpnt->cmnd[4] = (unsigned char) (block >> 8) & 0xff;
	SCpnt->cmnd[5] = (unsigned char) block & 0xff;
	SCpnt->cmnd[6] = SCpnt->cmnd[9] = 0;
	SCpnt->cmnd[7] = (unsigned char) (this_count >> 8) & 0xff;
	SCpnt->cmnd[8] = (unsigned char) this_count & 0xff;

	/*
	 * We shouldn't disconnect in the middle of a sector, so with a dumb
	 * host adapter, it's safe to assume that we can at least transfer
	 * this many bytes between each connect / disconnect.
	 */
	SCpnt->transfersize = cd->device->sector_size;
	SCpnt->underflow = this_count << 9;
	SCpnt->allowed = MAX_RETRIES;

	/*
	 * This indicates that the command is ready from our end to be
	 * queued.
	 */
	ret = BLKPREP_OK;
 out:
	return scsi_prep_return(q, rq, ret);
}

static int sr_block_open(struct inode *inode, struct file *file)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct scsi_cd *cd;
	int ret = 0;

	if(!(cd = scsi_cd_get(disk)))
		return -ENXIO;

	if((ret = cdrom_open(&cd->cdi, inode, file)) != 0)
		scsi_cd_put(cd);

	return ret;
}

static int sr_block_release(struct inode *inode, struct file *file)
{
	int ret;
	struct scsi_cd *cd = scsi_cd(inode->i_bdev->bd_disk);
	ret = cdrom_release(&cd->cdi, file);
	if(ret)
		return ret;
	
	scsi_cd_put(cd);

	return 0;
}

static int sr_block_ioctl(struct inode *inode, struct file *file, unsigned cmd,
			  unsigned long arg)
{
	struct scsi_cd *cd = scsi_cd(inode->i_bdev->bd_disk);
	struct scsi_device *sdev = cd->device;
	void __user *argp = (void __user *)arg;
	int ret;

	/*
	 * Send SCSI addressing ioctls directly to mid level, send other
	 * ioctls to cdrom/block level.
	 */
	switch (cmd) {
	case SCSI_IOCTL_GET_IDLUN:
	case SCSI_IOCTL_GET_BUS_NUMBER:
		return scsi_ioctl(sdev, cmd, argp);
	}

	ret = cdrom_ioctl(file, &cd->cdi, inode, cmd, arg);
	if (ret != -ENOSYS)
		return ret;

	/*
	 * ENODEV means that we didn't recognise the ioctl, or that we
	 * cannot execute it in the current device state.  In either
	 * case fall through to scsi_ioctl, which will return ENDOEV again
	 * if it doesn't recognise the ioctl
	 */
	ret = scsi_nonblockable_ioctl(sdev, cmd, argp, NULL);
	if (ret != -ENODEV)
		return ret;
	return scsi_ioctl(sdev, cmd, argp);
}

static int sr_block_media_changed(struct gendisk *disk)
{
	struct scsi_cd *cd = scsi_cd(disk);
	return cdrom_media_changed(&cd->cdi);
}

static struct block_device_operations sr_bdops =
{
	.owner		= THIS_MODULE,
	.open		= sr_block_open,
	.release	= sr_block_release,
	.ioctl		= sr_block_ioctl,
	.media_changed	= sr_block_media_changed,
	/* 
	 * No compat_ioctl for now because sr_block_ioctl never
	 * seems to pass arbitary ioctls down to host drivers.
	 */
};

static int sr_open(struct cdrom_device_info *cdi, int purpose)
{
	struct scsi_cd *cd = cdi->handle;
	struct scsi_device *sdev = cd->device;
	int retval;

	/*
	 * If the device is in error recovery, wait until it is done.
	 * If the device is offline, then disallow any access to it.
	 */
	retval = -ENXIO;
	if (!scsi_block_when_processing_errors(sdev))
		goto error_out;

	return 0;

error_out:
	return retval;	
}

static void sr_release(struct cdrom_device_info *cdi)
{
	struct scsi_cd *cd = cdi->handle;

	if (cd->device->sector_size > 2048)
		sr_set_blocklength(cd, 2048);

}

static int sr_probe(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct gendisk *disk;
	struct scsi_cd *cd;
	int minor, error;

	error = -ENODEV;
	if (sdev->type != TYPE_ROM && sdev->type != TYPE_WORM)
		goto fail;

	error = -ENOMEM;
	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd)
		goto fail;

	kref_init(&cd->kref);

	disk = alloc_disk(1);
	if (!disk)
		goto fail_free;

	spin_lock(&sr_index_lock);
	minor = find_first_zero_bit(sr_index_bits, SR_DISKS);
	if (minor == SR_DISKS) {
		spin_unlock(&sr_index_lock);
		error = -EBUSY;
		goto fail_put;
	}
	__set_bit(minor, sr_index_bits);
	spin_unlock(&sr_index_lock);

	disk->major = SCSI_CDROM_MAJOR;
	disk->first_minor = minor;
	sprintf(disk->disk_name, "sr%d", minor);
	disk->fops = &sr_bdops;
	disk->flags = GENHD_FL_CD;

	blk_queue_rq_timeout(sdev->request_queue, SR_TIMEOUT);

	cd->device = sdev;
	cd->disk = disk;
	cd->driver = &sr_template;
	cd->disk = disk;
	cd->capacity = 0x1fffff;
	cd->device->changed = 1;	/* force recheck CD type */
	cd->previous_state = 1;
	cd->use = 1;
	cd->readcd_known = 0;
	cd->readcd_cdda = 0;

	cd->cdi.ops = &sr_dops;
	cd->cdi.handle = cd;
	cd->cdi.mask = 0;
	cd->cdi.capacity = 1;
	sprintf(cd->cdi.name, "sr%d", minor);

	sdev->sector_size = 2048;	/* A guess, just in case */

	/* FIXME: need to handle a get_capabilities failure properly ?? */
	get_capabilities(cd);
	blk_queue_prep_rq(sdev->request_queue, sr_prep_fn);
	sr_vendor_init(cd);

	disk->driverfs_dev = &sdev->sdev_gendev;
	set_capacity(disk, cd->capacity);
	disk->private_data = &cd->driver;
	disk->queue = sdev->request_queue;
	cd->cdi.disk = disk;

	if (register_cdrom(&cd->cdi))
		goto fail_put;

	dev_set_drvdata(dev, cd);
	disk->flags |= GENHD_FL_REMOVABLE;
	add_disk(disk);

	sdev_printk(KERN_DEBUG, sdev,
		    "Attached scsi CD-ROM %s\n", cd->cdi.name);
	return 0;

fail_put:
	put_disk(disk);
fail_free:
	kfree(cd);
fail:
	return error;
}


static void get_sectorsize(struct scsi_cd *cd)
{
	unsigned char cmd[10];
	unsigned char buffer[8];
	int the_result, retries = 3;
	int sector_size;
	struct request_queue *queue;

	do {
		cmd[0] = READ_CAPACITY;
		memset((void *) &cmd[1], 0, 9);
		memset(buffer, 0, sizeof(buffer));

		/* Do the command and wait.. */
		the_result = scsi_execute_req(cd->device, cmd, DMA_FROM_DEVICE,
					      buffer, sizeof(buffer), NULL,
					      SR_TIMEOUT, MAX_RETRIES);

		retries--;

	} while (the_result && retries);


	if (the_result) {
		cd->capacity = 0x1fffff;
		sector_size = 2048;	/* A guess, just in case */
	} else {
#if 0
		if (cdrom_get_last_written(&cd->cdi,
					   &cd->capacity))
#endif
			cd->capacity = 1 + ((buffer[0] << 24) |
						    (buffer[1] << 16) |
						    (buffer[2] << 8) |
						    buffer[3]);
		sector_size = (buffer[4] << 24) |
		    (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
		switch (sector_size) {
			/*
			 * HP 4020i CD-Recorder reports 2340 byte sectors
			 * Philips CD-Writers report 2352 byte sectors
			 *
			 * Use 2k sectors for them..
			 */
		case 0:
		case 2340:
		case 2352:
			sector_size = 2048;
			/* fall through */
		case 2048:
			cd->capacity *= 4;
			/* fall through */
		case 512:
			break;
		default:
			printk("%s: unsupported sector size %d.\n",
			       cd->cdi.name, sector_size);
			cd->capacity = 0;
		}

		cd->device->sector_size = sector_size;

		/*
		 * Add this so that we have the ability to correctly gauge
		 * what the device is capable of.
		 */
		set_capacity(cd->disk, cd->capacity);
	}

	queue = cd->device->request_queue;
	blk_queue_hardsect_size(queue, sector_size);

	return;
}

static void get_capabilities(struct scsi_cd *cd)
{
	unsigned char *buffer;
	struct scsi_mode_data data;
	struct scsi_sense_hdr sshdr;
	int rc, n;

	static const char *loadmech[] =
	{
		"caddy",
		"tray",
		"pop-up",
		"",
		"changer",
		"cartridge changer",
		"",
		""
	};


	/* allocate transfer buffer */
	buffer = kmalloc(512, GFP_KERNEL | GFP_DMA);
	if (!buffer) {
		printk(KERN_ERR "sr: out of memory.\n");
		return;
	}

	/* eat unit attentions */
	sr_test_unit_ready(cd->device, &sshdr);

	/* ask for mode page 0x2a */
	rc = scsi_mode_sense(cd->device, 0, 0x2a, buffer, 128,
			     SR_TIMEOUT, 3, &data, NULL);

	if (!scsi_status_is_good(rc)) {
		/* failed, drive doesn't have capabilities mode page */
		cd->cdi.speed = 1;
		cd->cdi.mask |= (CDC_CD_R | CDC_CD_RW | CDC_DVD_R |
				 CDC_DVD | CDC_DVD_RAM |
				 CDC_SELECT_DISC | CDC_SELECT_SPEED |
				 CDC_MRW | CDC_MRW_W | CDC_RAM);
		kfree(buffer);
		printk("%s: scsi-1 drive\n", cd->cdi.name);
		return;
	}

	n = data.header_length + data.block_descriptor_length;
	cd->cdi.speed = ((buffer[n + 8] << 8) + buffer[n + 9]) / 176;
	cd->readcd_known = 1;
	cd->readcd_cdda = buffer[n + 5] & 0x01;
	/* print some capability bits */
	printk("%s: scsi3-mmc drive: %dx/%dx %s%s%s%s%s%s\n", cd->cdi.name,
	       ((buffer[n + 14] << 8) + buffer[n + 15]) / 176,
	       cd->cdi.speed,
	       buffer[n + 3] & 0x01 ? "writer " : "", /* CD Writer */
	       buffer[n + 3] & 0x20 ? "dvd-ram " : "",
	       buffer[n + 2] & 0x02 ? "cd/rw " : "", /* can read rewriteable */
	       buffer[n + 4] & 0x20 ? "xa/form2 " : "",	/* can read xa/from2 */
	       buffer[n + 5] & 0x01 ? "cdda " : "", /* can read audio data */
	       loadmech[buffer[n + 6] >> 5]);
	if ((buffer[n + 6] >> 5) == 0)
		/* caddy drives can't close tray... */
		cd->cdi.mask |= CDC_CLOSE_TRAY;
	if ((buffer[n + 2] & 0x8) == 0)
		/* not a DVD drive */
		cd->cdi.mask |= CDC_DVD;
	if ((buffer[n + 3] & 0x20) == 0) 
		/* can't write DVD-RAM media */
		cd->cdi.mask |= CDC_DVD_RAM;
	if ((buffer[n + 3] & 0x10) == 0)
		/* can't write DVD-R media */
		cd->cdi.mask |= CDC_DVD_R;
	if ((buffer[n + 3] & 0x2) == 0)
		/* can't write CD-RW media */
		cd->cdi.mask |= CDC_CD_RW;
	if ((buffer[n + 3] & 0x1) == 0)
		/* can't write CD-R media */
		cd->cdi.mask |= CDC_CD_R;
	if ((buffer[n + 6] & 0x8) == 0)
		/* can't eject */
		cd->cdi.mask |= CDC_OPEN_TRAY;

	if ((buffer[n + 6] >> 5) == mechtype_individual_changer ||
	    (buffer[n + 6] >> 5) == mechtype_cartridge_changer)
		cd->cdi.capacity =
		    cdrom_number_of_slots(&cd->cdi);
	if (cd->cdi.capacity <= 1)
		/* not a changer */
		cd->cdi.mask |= CDC_SELECT_DISC;
	/*else    I don't think it can close its tray
		cd->cdi.mask |= CDC_CLOSE_TRAY; */

	/*
	 * if DVD-RAM, MRW-W or CD-RW, we are randomly writable
	 */
	if ((cd->cdi.mask & (CDC_DVD_RAM | CDC_MRW_W | CDC_RAM | CDC_CD_RW)) !=
			(CDC_DVD_RAM | CDC_MRW_W | CDC_RAM | CDC_CD_RW)) {
		cd->device->writeable = 1;
	}

	kfree(buffer);
}

/*
 * sr_packet() is the entry point for the generic commands generated
 * by the Uniform CD-ROM layer. 
 */
static int sr_packet(struct cdrom_device_info *cdi,
		struct packet_command *cgc)
{
	if (cgc->timeout <= 0)
		cgc->timeout = IOCTL_TIMEOUT;

	sr_do_ioctl(cdi->handle, cgc);

	return cgc->stat;
}

/**
 *	sr_kref_release - Called to free the scsi_cd structure
 *	@kref: pointer to embedded kref
 *
 *	sr_ref_mutex must be held entering this routine.  Because it is
 *	called on last put, you should always use the scsi_cd_get()
 *	scsi_cd_put() helpers which manipulate the semaphore directly
 *	and never do a direct kref_put().
 **/
static void sr_kref_release(struct kref *kref)
{
	struct scsi_cd *cd = container_of(kref, struct scsi_cd, kref);
	struct gendisk *disk = cd->disk;

	spin_lock(&sr_index_lock);
	clear_bit(MINOR(disk_devt(disk)), sr_index_bits);
	spin_unlock(&sr_index_lock);

	unregister_cdrom(&cd->cdi);

	disk->private_data = NULL;

	put_disk(disk);

	kfree(cd);
}

static int sr_remove(struct device *dev)
{
	struct scsi_cd *cd = dev_get_drvdata(dev);

	del_gendisk(cd->disk);

	mutex_lock(&sr_ref_mutex);
	kref_put(&cd->kref, sr_kref_release);
	mutex_unlock(&sr_ref_mutex);

	return 0;
}

static int __init init_sr(void)
{
	int rc;

	rc = register_blkdev(SCSI_CDROM_MAJOR, "sr");
	if (rc)
		return rc;
	rc = scsi_register_driver(&sr_template.gendrv);
	if (rc)
		unregister_blkdev(SCSI_CDROM_MAJOR, "sr");

	return rc;
}

static void __exit exit_sr(void)
{
	scsi_unregister_driver(&sr_template.gendrv);
	unregister_blkdev(SCSI_CDROM_MAJOR, "sr");
}

module_init(init_sr);
module_exit(exit_sr);
MODULE_LICENSE("GPL");
