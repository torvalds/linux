/*
 * cdrom.c IOCTLs handling for ide-cd driver.
 *
 * Copyright (C) 1994-1996  Scott Snyder <snyder@fnald0.fnal.gov>
 * Copyright (C) 1996-1998  Erik Andersen <andersee@debian.org>
 * Copyright (C) 1998-2000  Jens Axboe <axboe@suse.de>
 */

#include <linux/kernel.h>
#include <linux/cdrom.h>
#include <linux/gfp.h>
#include <linux/ide.h>
#include <scsi/scsi.h>

#include "ide-cd.h"

/****************************************************************************
 * Other driver requests (open, close, check media change).
 */
int ide_cdrom_open_real(struct cdrom_device_info *cdi, int purpose)
{
	return 0;
}

/*
 * Close down the device.  Invalidate all cached blocks.
 */
void ide_cdrom_release_real(struct cdrom_device_info *cdi)
{
	ide_drive_t *drive = cdi->handle;

	if (!cdi->use_count)
		drive->atapi_flags &= ~IDE_AFLAG_TOC_VALID;
}

/*
 * add logic to try GET_EVENT command first to check for media and tray
 * status. this should be supported by newer cd-r/w and all DVD etc
 * drives
 */
int ide_cdrom_drive_status(struct cdrom_device_info *cdi, int slot_nr)
{
	ide_drive_t *drive = cdi->handle;
	struct media_event_desc med;
	struct request_sense sense;
	int stat;

	if (slot_nr != CDSL_CURRENT)
		return -EINVAL;

	stat = cdrom_check_status(drive, &sense);
	if (!stat || sense.sense_key == UNIT_ATTENTION)
		return CDS_DISC_OK;

	if (!cdrom_get_media_event(cdi, &med)) {
		if (med.media_present)
			return CDS_DISC_OK;
		else if (med.door_open)
			return CDS_TRAY_OPEN;
		else
			return CDS_NO_DISC;
	}

	if (sense.sense_key == NOT_READY && sense.asc == 0x04
			&& sense.ascq == 0x04)
		return CDS_DISC_OK;

	/*
	 * If not using Mt Fuji extended media tray reports,
	 * just return TRAY_OPEN since ATAPI doesn't provide
	 * any other way to detect this...
	 */
	if (sense.sense_key == NOT_READY) {
		if (sense.asc == 0x3a && sense.ascq == 1)
			return CDS_NO_DISC;
		else
			return CDS_TRAY_OPEN;
	}
	return CDS_DRIVE_NOT_READY;
}

/*
 * ide-cd always generates media changed event if media is missing, which
 * makes it impossible to use for proper event reporting, so disk->events
 * is cleared to 0 and the following function is used only to trigger
 * revalidation and never propagated to userland.
 */
unsigned int ide_cdrom_check_events_real(struct cdrom_device_info *cdi,
					 unsigned int clearing, int slot_nr)
{
	ide_drive_t *drive = cdi->handle;
	int retval;

	if (slot_nr == CDSL_CURRENT) {
		(void) cdrom_check_status(drive, NULL);
		retval = (drive->dev_flags & IDE_DFLAG_MEDIA_CHANGED) ? 1 : 0;
		drive->dev_flags &= ~IDE_DFLAG_MEDIA_CHANGED;
		return retval ? DISK_EVENT_MEDIA_CHANGE : 0;
	} else {
		return 0;
	}
}

/* Eject the disk if EJECTFLAG is 0.
   If EJECTFLAG is 1, try to reload the disk. */
static
int cdrom_eject(ide_drive_t *drive, int ejectflag,
		struct request_sense *sense)
{
	struct cdrom_info *cd = drive->driver_data;
	struct cdrom_device_info *cdi = &cd->devinfo;
	char loej = 0x02;
	unsigned char cmd[BLK_MAX_CDB];

	if ((drive->atapi_flags & IDE_AFLAG_NO_EJECT) && !ejectflag)
		return -EDRIVE_CANT_DO_THIS;

	/* reload fails on some drives, if the tray is locked */
	if ((drive->atapi_flags & IDE_AFLAG_DOOR_LOCKED) && ejectflag)
		return 0;

	/* only tell drive to close tray if open, if it can do that */
	if (ejectflag && (cdi->mask & CDC_CLOSE_TRAY))
		loej = 0;

	memset(cmd, 0, BLK_MAX_CDB);

	cmd[0] = GPCMD_START_STOP_UNIT;
	cmd[4] = loej | (ejectflag != 0);

	return ide_cd_queue_pc(drive, cmd, 0, NULL, NULL, sense, 0, 0);
}

/* Lock the door if LOCKFLAG is nonzero; unlock it otherwise. */
static
int ide_cd_lockdoor(ide_drive_t *drive, int lockflag,
		    struct request_sense *sense)
{
	struct request_sense my_sense;
	int stat;

	if (sense == NULL)
		sense = &my_sense;

	/* If the drive cannot lock the door, just pretend. */
	if ((drive->dev_flags & IDE_DFLAG_DOORLOCKING) == 0) {
		stat = 0;
	} else {
		unsigned char cmd[BLK_MAX_CDB];

		memset(cmd, 0, BLK_MAX_CDB);

		cmd[0] = GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL;
		cmd[4] = lockflag ? 1 : 0;

		stat = ide_cd_queue_pc(drive, cmd, 0, NULL, NULL,
				       sense, 0, 0);
	}

	/* If we got an illegal field error, the drive
	   probably cannot lock the door. */
	if (stat != 0 &&
	    sense->sense_key == ILLEGAL_REQUEST &&
	    (sense->asc == 0x24 || sense->asc == 0x20)) {
		printk(KERN_ERR "%s: door locking not supported\n",
			drive->name);
		drive->dev_flags &= ~IDE_DFLAG_DOORLOCKING;
		stat = 0;
	}

	/* no medium, that's alright. */
	if (stat != 0 && sense->sense_key == NOT_READY && sense->asc == 0x3a)
		stat = 0;

	if (stat == 0) {
		if (lockflag)
			drive->atapi_flags |= IDE_AFLAG_DOOR_LOCKED;
		else
			drive->atapi_flags &= ~IDE_AFLAG_DOOR_LOCKED;
	}

	return stat;
}

int ide_cdrom_tray_move(struct cdrom_device_info *cdi, int position)
{
	ide_drive_t *drive = cdi->handle;
	struct request_sense sense;

	if (position) {
		int stat = ide_cd_lockdoor(drive, 0, &sense);

		if (stat)
			return stat;
	}

	return cdrom_eject(drive, !position, &sense);
}

int ide_cdrom_lock_door(struct cdrom_device_info *cdi, int lock)
{
	ide_drive_t *drive = cdi->handle;

	return ide_cd_lockdoor(drive, lock, NULL);
}

/*
 * ATAPI devices are free to select the speed you request or any slower
 * rate. :-(  Requesting too fast a speed will _not_ produce an error.
 */
int ide_cdrom_select_speed(struct cdrom_device_info *cdi, int speed)
{
	ide_drive_t *drive = cdi->handle;
	struct cdrom_info *cd = drive->driver_data;
	struct request_sense sense;
	u8 buf[ATAPI_CAPABILITIES_PAGE_SIZE];
	int stat;
	unsigned char cmd[BLK_MAX_CDB];

	if (speed == 0)
		speed = 0xffff; /* set to max */
	else
		speed *= 177;   /* Nx to kbytes/s */

	memset(cmd, 0, BLK_MAX_CDB);

	cmd[0] = GPCMD_SET_SPEED;
	/* Read Drive speed in kbytes/second MSB/LSB */
	cmd[2] = (speed >> 8) & 0xff;
	cmd[3] = speed & 0xff;
	if ((cdi->mask & (CDC_CD_R | CDC_CD_RW | CDC_DVD_R)) !=
	    (CDC_CD_R | CDC_CD_RW | CDC_DVD_R)) {
		/* Write Drive speed in kbytes/second MSB/LSB */
		cmd[4] = (speed >> 8) & 0xff;
		cmd[5] = speed & 0xff;
	}

	stat = ide_cd_queue_pc(drive, cmd, 0, NULL, NULL, &sense, 0, 0);

	if (!ide_cdrom_get_capabilities(drive, buf)) {
		ide_cdrom_update_speed(drive, buf);
		cdi->speed = cd->current_speed;
	}

	return 0;
}

int ide_cdrom_get_last_session(struct cdrom_device_info *cdi,
			       struct cdrom_multisession *ms_info)
{
	struct atapi_toc *toc;
	ide_drive_t *drive = cdi->handle;
	struct cdrom_info *info = drive->driver_data;
	struct request_sense sense;
	int ret;

	if ((drive->atapi_flags & IDE_AFLAG_TOC_VALID) == 0 || !info->toc) {
		ret = ide_cd_read_toc(drive, &sense);
		if (ret)
			return ret;
	}

	toc = info->toc;
	ms_info->addr.lba = toc->last_session_lba;
	ms_info->xa_flag = toc->xa_flag;

	return 0;
}

int ide_cdrom_get_mcn(struct cdrom_device_info *cdi,
		      struct cdrom_mcn *mcn_info)
{
	ide_drive_t *drive = cdi->handle;
	int stat, mcnlen;
	char buf[24];
	unsigned char cmd[BLK_MAX_CDB];
	unsigned len = sizeof(buf);

	memset(cmd, 0, BLK_MAX_CDB);

	cmd[0] = GPCMD_READ_SUBCHANNEL;
	cmd[1] = 2;		/* MSF addressing */
	cmd[2] = 0x40;	/* request subQ data */
	cmd[3] = 2;		/* format */
	cmd[8] = len;

	stat = ide_cd_queue_pc(drive, cmd, 0, buf, &len, NULL, 0, 0);
	if (stat)
		return stat;

	mcnlen = sizeof(mcn_info->medium_catalog_number) - 1;
	memcpy(mcn_info->medium_catalog_number, buf + 9, mcnlen);
	mcn_info->medium_catalog_number[mcnlen] = '\0';

	return 0;
}

int ide_cdrom_reset(struct cdrom_device_info *cdi)
{
	ide_drive_t *drive = cdi->handle;
	struct cdrom_info *cd = drive->driver_data;
	struct request_sense sense;
	struct request *rq;
	int ret;

	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd_flags = REQ_QUIET;
	ret = blk_execute_rq(drive->queue, cd->disk, rq, 0);
	blk_put_request(rq);
	/*
	 * A reset will unlock the door. If it was previously locked,
	 * lock it again.
	 */
	if (drive->atapi_flags & IDE_AFLAG_DOOR_LOCKED)
		(void)ide_cd_lockdoor(drive, 1, &sense);

	return ret;
}

static int ide_cd_get_toc_entry(ide_drive_t *drive, int track,
				struct atapi_toc_entry **ent)
{
	struct cdrom_info *info = drive->driver_data;
	struct atapi_toc *toc = info->toc;
	int ntracks;

	/*
	 * don't serve cached data, if the toc isn't valid
	 */
	if ((drive->atapi_flags & IDE_AFLAG_TOC_VALID) == 0)
		return -EINVAL;

	/* Check validity of requested track number. */
	ntracks = toc->hdr.last_track - toc->hdr.first_track + 1;

	if (toc->hdr.first_track == CDROM_LEADOUT)
		ntracks = 0;

	if (track == CDROM_LEADOUT)
		*ent = &toc->ent[ntracks];
	else if (track < toc->hdr.first_track || track > toc->hdr.last_track)
		return -EINVAL;
	else
		*ent = &toc->ent[track - toc->hdr.first_track];

	return 0;
}

static int ide_cd_fake_play_trkind(ide_drive_t *drive, void *arg)
{
	struct cdrom_ti *ti = arg;
	struct atapi_toc_entry *first_toc, *last_toc;
	unsigned long lba_start, lba_end;
	int stat;
	struct request_sense sense;
	unsigned char cmd[BLK_MAX_CDB];

	stat = ide_cd_get_toc_entry(drive, ti->cdti_trk0, &first_toc);
	if (stat)
		return stat;

	stat = ide_cd_get_toc_entry(drive, ti->cdti_trk1, &last_toc);
	if (stat)
		return stat;

	if (ti->cdti_trk1 != CDROM_LEADOUT)
		++last_toc;
	lba_start = first_toc->addr.lba;
	lba_end   = last_toc->addr.lba;

	if (lba_end <= lba_start)
		return -EINVAL;

	memset(cmd, 0, BLK_MAX_CDB);

	cmd[0] = GPCMD_PLAY_AUDIO_MSF;
	lba_to_msf(lba_start,   &cmd[3], &cmd[4], &cmd[5]);
	lba_to_msf(lba_end - 1, &cmd[6], &cmd[7], &cmd[8]);

	return ide_cd_queue_pc(drive, cmd, 0, NULL, NULL, &sense, 0, 0);
}

static int ide_cd_read_tochdr(ide_drive_t *drive, void *arg)
{
	struct cdrom_info *cd = drive->driver_data;
	struct cdrom_tochdr *tochdr = arg;
	struct atapi_toc *toc;
	int stat;

	/* Make sure our saved TOC is valid. */
	stat = ide_cd_read_toc(drive, NULL);
	if (stat)
		return stat;

	toc = cd->toc;
	tochdr->cdth_trk0 = toc->hdr.first_track;
	tochdr->cdth_trk1 = toc->hdr.last_track;

	return 0;
}

static int ide_cd_read_tocentry(ide_drive_t *drive, void *arg)
{
	struct cdrom_tocentry *tocentry = arg;
	struct atapi_toc_entry *toce;
	int stat;

	stat = ide_cd_get_toc_entry(drive, tocentry->cdte_track, &toce);
	if (stat)
		return stat;

	tocentry->cdte_ctrl = toce->control;
	tocentry->cdte_adr  = toce->adr;
	if (tocentry->cdte_format == CDROM_MSF) {
		lba_to_msf(toce->addr.lba,
			   &tocentry->cdte_addr.msf.minute,
			   &tocentry->cdte_addr.msf.second,
			   &tocentry->cdte_addr.msf.frame);
	} else
		tocentry->cdte_addr.lba = toce->addr.lba;

	return 0;
}

int ide_cdrom_audio_ioctl(struct cdrom_device_info *cdi,
			  unsigned int cmd, void *arg)
{
	ide_drive_t *drive = cdi->handle;

	switch (cmd) {
	/*
	 * emulate PLAY_AUDIO_TI command with PLAY_AUDIO_10, since
	 * atapi doesn't support it
	 */
	case CDROMPLAYTRKIND:
		return ide_cd_fake_play_trkind(drive, arg);
	case CDROMREADTOCHDR:
		return ide_cd_read_tochdr(drive, arg);
	case CDROMREADTOCENTRY:
		return ide_cd_read_tocentry(drive, arg);
	default:
		return -EINVAL;
	}
}

/* the generic packet interface to cdrom.c */
int ide_cdrom_packet(struct cdrom_device_info *cdi,
			    struct packet_command *cgc)
{
	ide_drive_t *drive = cdi->handle;
	unsigned int flags = 0;
	unsigned len = cgc->buflen;

	if (cgc->timeout <= 0)
		cgc->timeout = ATAPI_WAIT_PC;

	/* here we queue the commands from the uniform CD-ROM
	   layer. the packet must be complete, as we do not
	   touch it at all. */

	if (cgc->data_direction == CGC_DATA_WRITE)
		flags |= REQ_WRITE;

	if (cgc->sense)
		memset(cgc->sense, 0, sizeof(struct request_sense));

	if (cgc->quiet)
		flags |= REQ_QUIET;

	cgc->stat = ide_cd_queue_pc(drive, cgc->cmd,
				    cgc->data_direction == CGC_DATA_WRITE,
				    cgc->buffer, &len,
				    cgc->sense, cgc->timeout, flags);
	if (!cgc->stat)
		cgc->buflen -= len;
	return cgc->stat;
}
