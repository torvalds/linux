// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/compat.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/cdrom.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/blk-pm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/uaccess.h>

#include <asm/unaligned.h>

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
static blk_status_t sr_init_command(struct scsi_cmnd *SCpnt);
static int sr_done(struct scsi_cmnd *);
static int sr_runtime_suspend(struct device *dev);

static const struct dev_pm_ops sr_pm_ops = {
	.runtime_suspend	= sr_runtime_suspend,
};

static struct scsi_driver sr_template = {
	.gendrv = {
		.name   	= "sr",
		.owner		= THIS_MODULE,
		.probe		= sr_probe,
		.remove		= sr_remove,
		.pm		= &sr_pm_ops,
	},
	.init_command		= sr_init_command,
	.done			= sr_done,
};

static unsigned long sr_index_bits[SR_DISKS / BITS_PER_LONG];
static DEFINE_SPINLOCK(sr_index_lock);

static struct lock_class_key sr_bio_compl_lkclass;

/* This semaphore is used to mediate the 0->1 reference get in the
 * face of object destruction (i.e. we can't allow a get on an
 * object after last put) */
static DEFINE_MUTEX(sr_ref_mutex);

static int sr_open(struct cdrom_device_info *, int);
static void sr_release(struct cdrom_device_info *);

static void get_sectorsize(struct scsi_cd *);
static void get_capabilities(struct scsi_cd *);

static unsigned int sr_check_events(struct cdrom_device_info *cdi,
				    unsigned int clearing, int slot);
static int sr_packet(struct cdrom_device_info *, struct packet_command *);
static int sr_read_cdda_bpc(struct cdrom_device_info *cdi, void __user *ubuf,
		u32 lba, u32 nr, u8 *last_sense);

static const struct cdrom_device_ops sr_dops = {
	.open			= sr_open,
	.release	 	= sr_release,
	.drive_status	 	= sr_drive_status,
	.check_events		= sr_check_events,
	.tray_move		= sr_tray_move,
	.lock_door		= sr_lock_door,
	.select_speed		= sr_select_speed,
	.get_last_session	= sr_get_last_session,
	.get_mcn		= sr_get_mcn,
	.reset			= sr_reset,
	.audio_ioctl		= sr_audio_ioctl,
	.generic_packet		= sr_packet,
	.read_cdda_bpc		= sr_read_cdda_bpc,
	.capability		= SR_CAPABILITIES,
};

static void sr_kref_release(struct kref *kref);

static inline struct scsi_cd *scsi_cd(struct gendisk *disk)
{
	return container_of(disk->private_data, struct scsi_cd, driver);
}

static int sr_runtime_suspend(struct device *dev)
{
	struct scsi_cd *cd = dev_get_drvdata(dev);

	if (!cd)	/* E.g.: runtime suspend following sr_remove() */
		return 0;

	if (cd->media_present)
		return -EBUSY;
	else
		return 0;
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
	if (scsi_device_get(cd->device)) {
		kref_put(&cd->kref, sr_kref_release);
		cd = NULL;
	}
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

static unsigned int sr_get_events(struct scsi_device *sdev)
{
	u8 buf[8];
	u8 cmd[] = { GET_EVENT_STATUS_NOTIFICATION,
		     1,			/* polled */
		     0, 0,		/* reserved */
		     1 << 4,		/* notification class: media */
		     0, 0,		/* reserved */
		     0, sizeof(buf),	/* allocation length */
		     0,			/* control */
	};
	struct event_header *eh = (void *)buf;
	struct media_event_desc *med = (void *)(buf + 4);
	struct scsi_sense_hdr sshdr;
	int result;

	result = scsi_execute_req(sdev, cmd, DMA_FROM_DEVICE, buf, sizeof(buf),
				  &sshdr, SR_TIMEOUT, MAX_RETRIES, NULL);
	if (scsi_sense_valid(&sshdr) && sshdr.sense_key == UNIT_ATTENTION)
		return DISK_EVENT_MEDIA_CHANGE;

	if (result || be16_to_cpu(eh->data_len) < sizeof(*med))
		return 0;

	if (eh->nea || eh->notification_class != 0x4)
		return 0;

	if (med->media_event_code == 1)
		return DISK_EVENT_EJECT_REQUEST;
	else if (med->media_event_code == 2)
		return DISK_EVENT_MEDIA_CHANGE;
	else if (med->media_event_code == 3)
		return DISK_EVENT_MEDIA_CHANGE;
	return 0;
}

/*
 * This function checks to see if the media has been changed or eject
 * button has been pressed.  It is possible that we have already
 * sensed a change, or the drive may have sensed one and not yet
 * reported it.  The past events are accumulated in sdev->changed and
 * returned together with the current state.
 */
static unsigned int sr_check_events(struct cdrom_device_info *cdi,
				    unsigned int clearing, int slot)
{
	struct scsi_cd *cd = cdi->handle;
	bool last_present;
	struct scsi_sense_hdr sshdr;
	unsigned int events;
	int ret;

	/* no changer support */
	if (CDSL_CURRENT != slot)
		return 0;

	events = sr_get_events(cd->device);
	cd->get_event_changed |= events & DISK_EVENT_MEDIA_CHANGE;

	/*
	 * If earlier GET_EVENT_STATUS_NOTIFICATION and TUR did not agree
	 * for several times in a row.  We rely on TUR only for this likely
	 * broken device, to prevent generating incorrect media changed
	 * events for every open().
	 */
	if (cd->ignore_get_event) {
		events &= ~DISK_EVENT_MEDIA_CHANGE;
		goto do_tur;
	}

	/*
	 * GET_EVENT_STATUS_NOTIFICATION is enough unless MEDIA_CHANGE
	 * is being cleared.  Note that there are devices which hang
	 * if asked to execute TUR repeatedly.
	 */
	if (cd->device->changed) {
		events |= DISK_EVENT_MEDIA_CHANGE;
		cd->device->changed = 0;
		cd->tur_changed = true;
	}

	if (!(clearing & DISK_EVENT_MEDIA_CHANGE))
		return events;
do_tur:
	/* let's see whether the media is there with TUR */
	last_present = cd->media_present;
	ret = scsi_test_unit_ready(cd->device, SR_TIMEOUT, MAX_RETRIES, &sshdr);

	/*
	 * Media is considered to be present if TUR succeeds or fails with
	 * sense data indicating something other than media-not-present
	 * (ASC 0x3a).
	 */
	cd->media_present = scsi_status_is_good(ret) ||
		(scsi_sense_valid(&sshdr) && sshdr.asc != 0x3a);

	if (last_present != cd->media_present)
		cd->device->changed = 1;

	if (cd->device->changed) {
		events |= DISK_EVENT_MEDIA_CHANGE;
		cd->device->changed = 0;
		cd->tur_changed = true;
	}

	if (cd->ignore_get_event)
		return events;

	/* check whether GET_EVENT is reporting spurious MEDIA_CHANGE */
	if (!cd->tur_changed) {
		if (cd->get_event_changed) {
			if (cd->tur_mismatch++ > 8) {
				sr_printk(KERN_WARNING, cd,
					  "GET_EVENT and TUR disagree continuously, suppress GET_EVENT events\n");
				cd->ignore_get_event = true;
			}
		} else {
			cd->tur_mismatch = 0;
		}
	}
	cd->tur_changed = false;
	cd->get_event_changed = false;

	return events;
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
	struct request *rq = scsi_cmd_to_rq(SCpnt);
	struct scsi_cd *cd = scsi_cd(rq->rq_disk);

#ifdef DEBUG
	scmd_printk(KERN_INFO, SCpnt, "done: %x\n", result);
#endif

	/*
	 * Handle MEDIUM ERRORs or VOLUME OVERFLOWs that indicate partial
	 * success.  Since this is a relatively rare error condition, no
	 * care is taken to avoid unnecessary additional work such as
	 * memcpy's that could be avoided.
	 */
	if (scsi_status_is_check_condition(result) &&
	    (SCpnt->sense_buffer[0] & 0x7f) == 0x70) { /* Sense current */
		switch (SCpnt->sense_buffer[2]) {
		case MEDIUM_ERROR:
		case VOLUME_OVERFLOW:
		case ILLEGAL_REQUEST:
			if (!(SCpnt->sense_buffer[0] & 0x90))
				break;
			error_sector =
				get_unaligned_be32(&SCpnt->sense_buffer[3]);
			if (rq->bio != NULL)
				block_sectors = bio_sectors(rq->bio);
			if (block_sectors < 4)
				block_sectors = 4;
			if (cd->device->sector_size == 2048)
				error_sector <<= 2;
			error_sector &= ~(block_sectors - 1);
			good_bytes = (error_sector - blk_rq_pos(rq)) << 9;
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
			good_bytes = this_count;
			break;

		default:
			break;
		}
	}

	return good_bytes;
}

static blk_status_t sr_init_command(struct scsi_cmnd *SCpnt)
{
	int block = 0, this_count, s_size;
	struct scsi_cd *cd;
	struct request *rq = scsi_cmd_to_rq(SCpnt);
	blk_status_t ret;

	ret = scsi_alloc_sgtables(SCpnt);
	if (ret != BLK_STS_OK)
		return ret;
	cd = scsi_cd(rq->rq_disk);

	SCSI_LOG_HLQUEUE(1, scmd_printk(KERN_INFO, SCpnt,
		"Doing sr request, block = %d\n", block));

	if (!cd->device || !scsi_device_online(cd->device)) {
		SCSI_LOG_HLQUEUE(2, scmd_printk(KERN_INFO, SCpnt,
			"Finishing %u sectors\n", blk_rq_sectors(rq)));
		SCSI_LOG_HLQUEUE(2, scmd_printk(KERN_INFO, SCpnt,
			"Retry with 0x%p\n", SCpnt));
		goto out;
	}

	if (cd->device->changed) {
		/*
		 * quietly refuse to do anything to a changed disc until the
		 * changed bit has been reset
		 */
		goto out;
	}

	s_size = cd->device->sector_size;
	if (s_size != 512 && s_size != 1024 && s_size != 2048) {
		scmd_printk(KERN_ERR, SCpnt, "bad sector size %d\n", s_size);
		goto out;
	}

	switch (req_op(rq)) {
	case REQ_OP_WRITE:
		if (!cd->writeable)
			goto out;
		SCpnt->cmnd[0] = WRITE_10;
		cd->cdi.media_written = 1;
		break;
	case REQ_OP_READ:
		SCpnt->cmnd[0] = READ_10;
		break;
	default:
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
	if (((unsigned int)blk_rq_pos(rq) % (s_size >> 9)) ||
	    (scsi_bufflen(SCpnt) % s_size)) {
		scmd_printk(KERN_NOTICE, SCpnt, "unaligned transfer\n");
		goto out;
	}

	this_count = (scsi_bufflen(SCpnt) >> 9) / (s_size >> 9);


	SCSI_LOG_HLQUEUE(2, scmd_printk(KERN_INFO, SCpnt,
					"%s %d/%u 512 byte blocks.\n",
					(rq_data_dir(rq) == WRITE) ?
					"writing" : "reading",
					this_count, blk_rq_sectors(rq)));

	SCpnt->cmnd[1] = 0;
	block = (unsigned int)blk_rq_pos(rq) / (s_size >> 9);

	if (this_count > 0xffff) {
		this_count = 0xffff;
		SCpnt->sdb.length = this_count * s_size;
	}

	put_unaligned_be32(block, &SCpnt->cmnd[2]);
	SCpnt->cmnd[6] = SCpnt->cmnd[9] = 0;
	put_unaligned_be16(this_count, &SCpnt->cmnd[7]);

	/*
	 * We shouldn't disconnect in the middle of a sector, so with a dumb
	 * host adapter, it's safe to assume that we can at least transfer
	 * this many bytes between each connect / disconnect.
	 */
	SCpnt->transfersize = cd->device->sector_size;
	SCpnt->underflow = this_count << 9;
	SCpnt->allowed = MAX_RETRIES;
	SCpnt->cmd_len = 10;

	/*
	 * This indicates that the command is ready from our end to be queued.
	 */
	return BLK_STS_OK;
 out:
	scsi_free_sgtables(SCpnt);
	return BLK_STS_IOERR;
}

static void sr_revalidate_disk(struct scsi_cd *cd)
{
	struct scsi_sense_hdr sshdr;

	/* if the unit is not ready, nothing more to do */
	if (scsi_test_unit_ready(cd->device, SR_TIMEOUT, MAX_RETRIES, &sshdr))
		return;
	sr_cd_check(&cd->cdi);
	get_sectorsize(cd);
}

static int sr_block_open(struct block_device *bdev, fmode_t mode)
{
	struct scsi_cd *cd;
	struct scsi_device *sdev;
	int ret = -ENXIO;

	cd = scsi_cd_get(bdev->bd_disk);
	if (!cd)
		goto out;

	sdev = cd->device;
	scsi_autopm_get_device(sdev);
	if (bdev_check_media_change(bdev))
		sr_revalidate_disk(cd);

	mutex_lock(&cd->lock);
	ret = cdrom_open(&cd->cdi, bdev, mode);
	mutex_unlock(&cd->lock);

	scsi_autopm_put_device(sdev);
	if (ret)
		scsi_cd_put(cd);

out:
	return ret;
}

static void sr_block_release(struct gendisk *disk, fmode_t mode)
{
	struct scsi_cd *cd = scsi_cd(disk);

	mutex_lock(&cd->lock);
	cdrom_release(&cd->cdi, mode);
	mutex_unlock(&cd->lock);

	scsi_cd_put(cd);
}

static int sr_block_ioctl(struct block_device *bdev, fmode_t mode, unsigned cmd,
			  unsigned long arg)
{
	struct gendisk *disk = bdev->bd_disk;
	struct scsi_cd *cd = scsi_cd(disk);
	struct scsi_device *sdev = cd->device;
	void __user *argp = (void __user *)arg;
	int ret;

	if (bdev_is_partition(bdev) && !capable(CAP_SYS_RAWIO))
		return -ENOIOCTLCMD;

	mutex_lock(&cd->lock);

	ret = scsi_ioctl_block_when_processing_errors(sdev, cmd,
			(mode & FMODE_NDELAY) != 0);
	if (ret)
		goto out;

	scsi_autopm_get_device(sdev);

	if (ret != CDROMCLOSETRAY && ret != CDROMEJECT) {
		ret = cdrom_ioctl(&cd->cdi, bdev, mode, cmd, arg);
		if (ret != -ENOSYS)
			goto put;
	}
	ret = scsi_ioctl(sdev, disk, mode, cmd, argp);

put:
	scsi_autopm_put_device(sdev);
out:
	mutex_unlock(&cd->lock);
	return ret;
}

static unsigned int sr_block_check_events(struct gendisk *disk,
					  unsigned int clearing)
{
	unsigned int ret = 0;
	struct scsi_cd *cd;

	cd = scsi_cd_get(disk);
	if (!cd)
		return 0;

	if (!atomic_read(&cd->device->disk_events_disable_depth))
		ret = cdrom_check_events(&cd->cdi, clearing);

	scsi_cd_put(cd);
	return ret;
}

static const struct block_device_operations sr_bdops =
{
	.owner		= THIS_MODULE,
	.open		= sr_block_open,
	.release	= sr_block_release,
	.ioctl		= sr_block_ioctl,
	.compat_ioctl	= blkdev_compat_ptr_ioctl,
	.check_events	= sr_block_check_events,
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
}

static int sr_probe(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct gendisk *disk;
	struct scsi_cd *cd;
	int minor, error;

	scsi_autopm_get_device(sdev);
	error = -ENODEV;
	if (sdev->type != TYPE_ROM && sdev->type != TYPE_WORM)
		goto fail;

	error = -ENOMEM;
	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd)
		goto fail;

	kref_init(&cd->kref);

	disk = __alloc_disk_node(sdev->request_queue, NUMA_NO_NODE,
				 &sr_bio_compl_lkclass);
	if (!disk)
		goto fail_free;
	mutex_init(&cd->lock);

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
	disk->minors = 1;
	sprintf(disk->disk_name, "sr%d", minor);
	disk->fops = &sr_bdops;
	disk->flags = GENHD_FL_CD | GENHD_FL_BLOCK_EVENTS_ON_EXCL_WRITE;
	disk->events = DISK_EVENT_MEDIA_CHANGE | DISK_EVENT_EJECT_REQUEST;
	disk->event_flags = DISK_EVENT_FLAG_POLL | DISK_EVENT_FLAG_UEVENT;

	blk_queue_rq_timeout(sdev->request_queue, SR_TIMEOUT);

	cd->device = sdev;
	cd->disk = disk;
	cd->driver = &sr_template;
	cd->capacity = 0x1fffff;
	cd->device->changed = 1;	/* force recheck CD type */
	cd->media_present = 1;
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
	sr_vendor_init(cd);

	set_capacity(disk, cd->capacity);
	disk->private_data = &cd->driver;

	if (register_cdrom(disk, &cd->cdi))
		goto fail_minor;

	/*
	 * Initialize block layer runtime PM stuffs before the
	 * periodic event checking request gets started in add_disk.
	 */
	blk_pm_runtime_init(sdev->request_queue, dev);

	dev_set_drvdata(dev, cd);
	disk->flags |= GENHD_FL_REMOVABLE;
	sr_revalidate_disk(cd);

	error = device_add_disk(&sdev->sdev_gendev, disk, NULL);
	if (error) {
		kref_put(&cd->kref, sr_kref_release);
		goto fail;
	}

	sdev_printk(KERN_DEBUG, sdev,
		    "Attached scsi CD-ROM %s\n", cd->cdi.name);
	scsi_autopm_put_device(cd->device);

	return 0;

fail_minor:
	spin_lock(&sr_index_lock);
	clear_bit(minor, sr_index_bits);
	spin_unlock(&sr_index_lock);
fail_put:
	put_disk(disk);
	mutex_destroy(&cd->lock);
fail_free:
	kfree(cd);
fail:
	scsi_autopm_put_device(sdev);
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
					      SR_TIMEOUT, MAX_RETRIES, NULL);

		retries--;

	} while (the_result && retries);


	if (the_result) {
		cd->capacity = 0x1fffff;
		sector_size = 2048;	/* A guess, just in case */
	} else {
		long last_written;

		cd->capacity = 1 + get_unaligned_be32(&buffer[0]);
		/*
		 * READ_CAPACITY doesn't return the correct size on
		 * certain UDF media.  If last_written is larger, use
		 * it instead.
		 *
		 * http://bugzilla.kernel.org/show_bug.cgi?id=9668
		 */
		if (!cdrom_get_last_written(&cd->cdi, &last_written))
			cd->capacity = max_t(long, cd->capacity, last_written);

		sector_size = get_unaligned_be32(&buffer[4]);
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
			fallthrough;
		case 2048:
			cd->capacity *= 4;
			fallthrough;
		case 512:
			break;
		default:
			sr_printk(KERN_INFO, cd,
				  "unsupported sector size %d.", sector_size);
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
	blk_queue_logical_block_size(queue, sector_size);

	return;
}

static void get_capabilities(struct scsi_cd *cd)
{
	unsigned char *buffer;
	struct scsi_mode_data data;
	struct scsi_sense_hdr sshdr;
	unsigned int ms_len = 128;
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
		sr_printk(KERN_ERR, cd, "out of memory.\n");
		return;
	}

	/* eat unit attentions */
	scsi_test_unit_ready(cd->device, SR_TIMEOUT, MAX_RETRIES, &sshdr);

	/* ask for mode page 0x2a */
	rc = scsi_mode_sense(cd->device, 0, 0x2a, buffer, ms_len,
			     SR_TIMEOUT, 3, &data, NULL);

	if (rc < 0 || data.length > ms_len ||
	    data.header_length + data.block_descriptor_length > data.length) {
		/* failed, drive doesn't have capabilities mode page */
		cd->cdi.speed = 1;
		cd->cdi.mask |= (CDC_CD_R | CDC_CD_RW | CDC_DVD_R |
				 CDC_DVD | CDC_DVD_RAM |
				 CDC_SELECT_DISC | CDC_SELECT_SPEED |
				 CDC_MRW | CDC_MRW_W | CDC_RAM);
		kfree(buffer);
		sr_printk(KERN_INFO, cd, "scsi-1 drive");
		return;
	}

	n = data.header_length + data.block_descriptor_length;
	cd->cdi.speed = get_unaligned_be16(&buffer[n + 8]) / 176;
	cd->readcd_known = 1;
	cd->readcd_cdda = buffer[n + 5] & 0x01;
	/* print some capability bits */
	sr_printk(KERN_INFO, cd,
		  "scsi3-mmc drive: %dx/%dx %s%s%s%s%s%s\n",
		  get_unaligned_be16(&buffer[n + 14]) / 176,
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
		cd->writeable = 1;
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
	struct scsi_cd *cd = cdi->handle;
	struct scsi_device *sdev = cd->device;

	if (cgc->cmd[0] == GPCMD_READ_DISC_INFO && sdev->no_read_disc_info)
		return -EDRIVE_CANT_DO_THIS;

	if (cgc->timeout <= 0)
		cgc->timeout = IOCTL_TIMEOUT;

	sr_do_ioctl(cd, cgc);

	return cgc->stat;
}

static int sr_read_cdda_bpc(struct cdrom_device_info *cdi, void __user *ubuf,
		u32 lba, u32 nr, u8 *last_sense)
{
	struct gendisk *disk = cdi->disk;
	u32 len = nr * CD_FRAMESIZE_RAW;
	struct scsi_request *req;
	struct request *rq;
	struct bio *bio;
	int ret;

	rq = scsi_alloc_request(disk->queue, REQ_OP_DRV_IN, 0);
	if (IS_ERR(rq))
		return PTR_ERR(rq);
	req = scsi_req(rq);

	ret = blk_rq_map_user(disk->queue, rq, NULL, ubuf, len, GFP_KERNEL);
	if (ret)
		goto out_put_request;

	req->cmd[0] = GPCMD_READ_CD;
	req->cmd[1] = 1 << 2;
	req->cmd[2] = (lba >> 24) & 0xff;
	req->cmd[3] = (lba >> 16) & 0xff;
	req->cmd[4] = (lba >>  8) & 0xff;
	req->cmd[5] = lba & 0xff;
	req->cmd[6] = (nr >> 16) & 0xff;
	req->cmd[7] = (nr >>  8) & 0xff;
	req->cmd[8] = nr & 0xff;
	req->cmd[9] = 0xf8;
	req->cmd_len = 12;
	rq->timeout = 60 * HZ;
	bio = rq->bio;

	blk_execute_rq(disk, rq, 0);
	if (scsi_req(rq)->result) {
		struct scsi_sense_hdr sshdr;

		scsi_normalize_sense(req->sense, req->sense_len,
				     &sshdr);
		*last_sense = sshdr.sense_key;
		ret = -EIO;
	}

	if (blk_rq_unmap_user(bio))
		ret = -EFAULT;
out_put_request:
	blk_mq_free_request(rq);
	return ret;
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

	mutex_destroy(&cd->lock);

	kfree(cd);
}

static int sr_remove(struct device *dev)
{
	struct scsi_cd *cd = dev_get_drvdata(dev);

	scsi_autopm_get_device(cd->device);

	del_gendisk(cd->disk);
	dev_set_drvdata(dev, NULL);

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
