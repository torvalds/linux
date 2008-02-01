/*
 * cdrom.c IOCTLs handling for ide-cd driver.
 *
 * Copyright (C) 1994-1996  Scott Snyder <snyder@fnald0.fnal.gov>
 * Copyright (C) 1996-1998  Erik Andersen <andersee@debian.org>
 * Copyright (C) 1998-2000  Jens Axboe <axboe@suse.de>
 */

#include <linux/kernel.h>
#include <linux/cdrom.h>
#include <linux/ide.h>

#include "ide-cd.h"

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
	struct request rq;
	struct request_sense sense;
	u8 buf[ATAPI_CAPABILITIES_PAGE_SIZE];
	int stat;

	ide_cd_init_rq(drive, &rq);

	rq.sense = &sense;

	if (speed == 0)
		speed = 0xffff; /* set to max */
	else
		speed *= 177;   /* Nx to kbytes/s */

	rq.cmd[0] = GPCMD_SET_SPEED;
	/* Read Drive speed in kbytes/second MSB/LSB */
	rq.cmd[2] = (speed >> 8) & 0xff;
	rq.cmd[3] = speed & 0xff;
	if ((cdi->mask & (CDC_CD_R | CDC_CD_RW | CDC_DVD_R)) !=
	    (CDC_CD_R | CDC_CD_RW | CDC_DVD_R)) {
		/* Write Drive speed in kbytes/second MSB/LSB */
		rq.cmd[4] = (speed >> 8) & 0xff;
		rq.cmd[5] = speed & 0xff;
	}

	stat = ide_cd_queue_pc(drive, &rq);

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

	if ((info->cd_flags & IDE_CD_FLAG_TOC_VALID) == 0 || !info->toc) {
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
	struct request rq;
	char buf[24];

	ide_cd_init_rq(drive, &rq);

	rq.data = buf;
	rq.data_len = sizeof(buf);

	rq.cmd[0] = GPCMD_READ_SUBCHANNEL;
	rq.cmd[1] = 2;		/* MSF addressing */
	rq.cmd[2] = 0x40;	/* request subQ data */
	rq.cmd[3] = 2;		/* format */
	rq.cmd[8] = sizeof(buf);

	stat = ide_cd_queue_pc(drive, &rq);
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
	struct request req;
	int ret;

	ide_cd_init_rq(drive, &req);
	req.cmd_type = REQ_TYPE_SPECIAL;
	req.cmd_flags = REQ_QUIET;
	ret = ide_do_drive_cmd(drive, &req, ide_wait);

	/*
	 * A reset will unlock the door. If it was previously locked,
	 * lock it again.
	 */
	if (cd->cd_flags & IDE_CD_FLAG_DOOR_LOCKED)
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
	if ((info->cd_flags & IDE_CD_FLAG_TOC_VALID) == 0)
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
	struct request rq;
	struct request_sense sense;

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

	ide_cd_init_rq(drive, &rq);

	rq.sense = &sense;
	rq.cmd[0] = GPCMD_PLAY_AUDIO_MSF;
	lba_to_msf(lba_start,   &rq.cmd[3], &rq.cmd[4], &rq.cmd[5]);
	lba_to_msf(lba_end - 1, &rq.cmd[6], &rq.cmd[7], &rq.cmd[8]);

	return ide_cd_queue_pc(drive, &rq);
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
