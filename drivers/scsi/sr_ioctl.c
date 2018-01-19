// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/blkpg.h>
#include <linux/cdrom.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_cmnd.h>

#include "sr.h"

#if 0
#define DEBUG
#endif

/* The sr_is_xa() seems to trigger firmware bugs with some drives :-(
 * It is off by default and can be turned on with this module parameter */
static int xa_test = 0;

module_param(xa_test, int, S_IRUGO | S_IWUSR);

/* primitive to determine whether we need to have GFP_DMA set based on
 * the status of the unchecked_isa_dma flag in the host structure */
#define SR_GFP_DMA(cd) (((cd)->device->host->unchecked_isa_dma) ? GFP_DMA : 0)

static int sr_read_tochdr(struct cdrom_device_info *cdi,
		struct cdrom_tochdr *tochdr)
{
	struct scsi_cd *cd = cdi->handle;
	struct packet_command cgc;
	int result;
	unsigned char *buffer;

	buffer = kmalloc(32, GFP_KERNEL | SR_GFP_DMA(cd));
	if (!buffer)
		return -ENOMEM;

	memset(&cgc, 0, sizeof(struct packet_command));
	cgc.timeout = IOCTL_TIMEOUT;
	cgc.cmd[0] = GPCMD_READ_TOC_PMA_ATIP;
	cgc.cmd[8] = 12;		/* LSB of length */
	cgc.buffer = buffer;
	cgc.buflen = 12;
	cgc.quiet = 1;
	cgc.data_direction = DMA_FROM_DEVICE;

	result = sr_do_ioctl(cd, &cgc);

	tochdr->cdth_trk0 = buffer[2];
	tochdr->cdth_trk1 = buffer[3];

	kfree(buffer);
	return result;
}

static int sr_read_tocentry(struct cdrom_device_info *cdi,
		struct cdrom_tocentry *tocentry)
{
	struct scsi_cd *cd = cdi->handle;
	struct packet_command cgc;
	int result;
	unsigned char *buffer;

	buffer = kmalloc(32, GFP_KERNEL | SR_GFP_DMA(cd));
	if (!buffer)
		return -ENOMEM;

	memset(&cgc, 0, sizeof(struct packet_command));
	cgc.timeout = IOCTL_TIMEOUT;
	cgc.cmd[0] = GPCMD_READ_TOC_PMA_ATIP;
	cgc.cmd[1] |= (tocentry->cdte_format == CDROM_MSF) ? 0x02 : 0;
	cgc.cmd[6] = tocentry->cdte_track;
	cgc.cmd[8] = 12;		/* LSB of length */
	cgc.buffer = buffer;
	cgc.buflen = 12;
	cgc.data_direction = DMA_FROM_DEVICE;

	result = sr_do_ioctl(cd, &cgc);

	tocentry->cdte_ctrl = buffer[5] & 0xf;
	tocentry->cdte_adr = buffer[5] >> 4;
	tocentry->cdte_datamode = (tocentry->cdte_ctrl & 0x04) ? 1 : 0;
	if (tocentry->cdte_format == CDROM_MSF) {
		tocentry->cdte_addr.msf.minute = buffer[9];
		tocentry->cdte_addr.msf.second = buffer[10];
		tocentry->cdte_addr.msf.frame = buffer[11];
	} else
		tocentry->cdte_addr.lba = (((((buffer[8] << 8) + buffer[9]) << 8)
			+ buffer[10]) << 8) + buffer[11];

	kfree(buffer);
	return result;
}

#define IOCTL_RETRIES 3

/* ATAPI drives don't have a SCMD_PLAYAUDIO_TI command.  When these drives
   are emulating a SCSI device via the idescsi module, they need to have
   CDROMPLAYTRKIND commands translated into CDROMPLAYMSF commands for them */

static int sr_fake_playtrkind(struct cdrom_device_info *cdi, struct cdrom_ti *ti)
{
	struct cdrom_tocentry trk0_te, trk1_te;
	struct cdrom_tochdr tochdr;
	struct packet_command cgc;
	int ntracks, ret;

	ret = sr_read_tochdr(cdi, &tochdr);
	if (ret)
		return ret;

	ntracks = tochdr.cdth_trk1 - tochdr.cdth_trk0 + 1;
	
	if (ti->cdti_trk1 == ntracks) 
		ti->cdti_trk1 = CDROM_LEADOUT;
	else if (ti->cdti_trk1 != CDROM_LEADOUT)
		ti->cdti_trk1 ++;

	trk0_te.cdte_track = ti->cdti_trk0;
	trk0_te.cdte_format = CDROM_MSF;
	trk1_te.cdte_track = ti->cdti_trk1;
	trk1_te.cdte_format = CDROM_MSF;
	
	ret = sr_read_tocentry(cdi, &trk0_te);
	if (ret)
		return ret;
	ret = sr_read_tocentry(cdi, &trk1_te);
	if (ret)
		return ret;

	memset(&cgc, 0, sizeof(struct packet_command));
	cgc.cmd[0] = GPCMD_PLAY_AUDIO_MSF;
	cgc.cmd[3] = trk0_te.cdte_addr.msf.minute;
	cgc.cmd[4] = trk0_te.cdte_addr.msf.second;
	cgc.cmd[5] = trk0_te.cdte_addr.msf.frame;
	cgc.cmd[6] = trk1_te.cdte_addr.msf.minute;
	cgc.cmd[7] = trk1_te.cdte_addr.msf.second;
	cgc.cmd[8] = trk1_te.cdte_addr.msf.frame;
	cgc.data_direction = DMA_NONE;
	cgc.timeout = IOCTL_TIMEOUT;
	return sr_do_ioctl(cdi->handle, &cgc);
}

static int sr_play_trkind(struct cdrom_device_info *cdi,
		struct cdrom_ti *ti)

{
	struct scsi_cd *cd = cdi->handle;
	struct packet_command cgc;
	int result;

	memset(&cgc, 0, sizeof(struct packet_command));
	cgc.timeout = IOCTL_TIMEOUT;
	cgc.cmd[0] = GPCMD_PLAYAUDIO_TI;
	cgc.cmd[4] = ti->cdti_trk0;
	cgc.cmd[5] = ti->cdti_ind0;
	cgc.cmd[7] = ti->cdti_trk1;
	cgc.cmd[8] = ti->cdti_ind1;
	cgc.data_direction = DMA_NONE;

	result = sr_do_ioctl(cd, &cgc);
	if (result == -EDRIVE_CANT_DO_THIS)
		result = sr_fake_playtrkind(cdi, ti);

	return result;
}

/* We do our own retries because we want to know what the specific
   error code is.  Normally the UNIT_ATTENTION code will automatically
   clear after one error */

int sr_do_ioctl(Scsi_CD *cd, struct packet_command *cgc)
{
	struct scsi_device *SDev;
	struct scsi_sense_hdr sshdr;
	int result, err = 0, retries = 0;

	SDev = cd->device;

      retry:
	if (!scsi_block_when_processing_errors(SDev)) {
		err = -ENODEV;
		goto out;
	}

	result = scsi_execute(SDev, cgc->cmd, cgc->data_direction,
			      cgc->buffer, cgc->buflen,
			      (unsigned char *)cgc->sense, &sshdr,
			      cgc->timeout, IOCTL_RETRIES, 0, 0, NULL);

	/* Minimal error checking.  Ignore cases we know about, and report the rest. */
	if (driver_byte(result) != 0) {
		switch (sshdr.sense_key) {
		case UNIT_ATTENTION:
			SDev->changed = 1;
			if (!cgc->quiet)
				sr_printk(KERN_INFO, cd,
					  "disc change detected.\n");
			if (retries++ < 10)
				goto retry;
			err = -ENOMEDIUM;
			break;
		case NOT_READY:	/* This happens if there is no disc in drive */
			if (sshdr.asc == 0x04 &&
			    sshdr.ascq == 0x01) {
				/* sense: Logical unit is in process of becoming ready */
				if (!cgc->quiet)
					sr_printk(KERN_INFO, cd,
						  "CDROM not ready yet.\n");
				if (retries++ < 10) {
					/* sleep 2 sec and try again */
					ssleep(2);
					goto retry;
				} else {
					/* 20 secs are enough? */
					err = -ENOMEDIUM;
					break;
				}
			}
			if (!cgc->quiet)
				sr_printk(KERN_INFO, cd,
					  "CDROM not ready.  Make sure there "
					  "is a disc in the drive.\n");
			err = -ENOMEDIUM;
			break;
		case ILLEGAL_REQUEST:
			err = -EIO;
			if (sshdr.asc == 0x20 &&
			    sshdr.ascq == 0x00)
				/* sense: Invalid command operation code */
				err = -EDRIVE_CANT_DO_THIS;
			break;
		default:
			err = -EIO;
		}
	}

	/* Wake up a process waiting for device */
      out:
	cgc->stat = err;
	return err;
}

/* ---------------------------------------------------------------------- */
/* interface to cdrom.c                                                   */

int sr_tray_move(struct cdrom_device_info *cdi, int pos)
{
	Scsi_CD *cd = cdi->handle;
	struct packet_command cgc;

	memset(&cgc, 0, sizeof(struct packet_command));
	cgc.cmd[0] = GPCMD_START_STOP_UNIT;
	cgc.cmd[4] = (pos == 0) ? 0x03 /* close */ : 0x02 /* eject */ ;
	cgc.data_direction = DMA_NONE;
	cgc.timeout = IOCTL_TIMEOUT;
	return sr_do_ioctl(cd, &cgc);
}

int sr_lock_door(struct cdrom_device_info *cdi, int lock)
{
	Scsi_CD *cd = cdi->handle;

	return scsi_set_medium_removal(cd->device, lock ?
		       SCSI_REMOVAL_PREVENT : SCSI_REMOVAL_ALLOW);
}

int sr_drive_status(struct cdrom_device_info *cdi, int slot)
{
	struct scsi_cd *cd = cdi->handle;
	struct scsi_sense_hdr sshdr;
	struct media_event_desc med;

	if (CDSL_CURRENT != slot) {
		/* we have no changer support */
		return -EINVAL;
	}
	if (!scsi_test_unit_ready(cd->device, SR_TIMEOUT, MAX_RETRIES, &sshdr))
		return CDS_DISC_OK;

	/* SK/ASC/ASCQ of 2/4/1 means "unit is becoming ready" */
	if (scsi_sense_valid(&sshdr) && sshdr.sense_key == NOT_READY
			&& sshdr.asc == 0x04 && sshdr.ascq == 0x01)
		return CDS_DRIVE_NOT_READY;

	if (!cdrom_get_media_event(cdi, &med)) {
		if (med.media_present)
			return CDS_DISC_OK;
		else if (med.door_open)
			return CDS_TRAY_OPEN;
		else
			return CDS_NO_DISC;
	}

	/*
	 * SK/ASC/ASCQ of 2/4/2 means "initialization required"
	 * Using CD_TRAY_OPEN results in an START_STOP_UNIT to close
	 * the tray, which resolves the initialization requirement.
	 */
	if (scsi_sense_valid(&sshdr) && sshdr.sense_key == NOT_READY
			&& sshdr.asc == 0x04 && sshdr.ascq == 0x02)
		return CDS_TRAY_OPEN;

	/*
	 * 0x04 is format in progress .. but there must be a disc present!
	 */
	if (sshdr.sense_key == NOT_READY && sshdr.asc == 0x04)
		return CDS_DISC_OK;

	/*
	 * If not using Mt Fuji extended media tray reports,
	 * just return TRAY_OPEN since ATAPI doesn't provide
	 * any other way to detect this...
	 */
	if (scsi_sense_valid(&sshdr) &&
	    /* 0x3a is medium not present */
	    sshdr.asc == 0x3a)
		return CDS_NO_DISC;
	else
		return CDS_TRAY_OPEN;

	return CDS_DRIVE_NOT_READY;
}

int sr_disk_status(struct cdrom_device_info *cdi)
{
	Scsi_CD *cd = cdi->handle;
	struct cdrom_tochdr toc_h;
	struct cdrom_tocentry toc_e;
	int i, rc, have_datatracks = 0;

	/* look for data tracks */
	rc = sr_read_tochdr(cdi, &toc_h);
	if (rc)
		return (rc == -ENOMEDIUM) ? CDS_NO_DISC : CDS_NO_INFO;

	for (i = toc_h.cdth_trk0; i <= toc_h.cdth_trk1; i++) {
		toc_e.cdte_track = i;
		toc_e.cdte_format = CDROM_LBA;
		if (sr_read_tocentry(cdi, &toc_e))
			return CDS_NO_INFO;
		if (toc_e.cdte_ctrl & CDROM_DATA_TRACK) {
			have_datatracks = 1;
			break;
		}
	}
	if (!have_datatracks)
		return CDS_AUDIO;

	if (cd->xa_flag)
		return CDS_XA_2_1;
	else
		return CDS_DATA_1;
}

int sr_get_last_session(struct cdrom_device_info *cdi,
			struct cdrom_multisession *ms_info)
{
	Scsi_CD *cd = cdi->handle;

	ms_info->addr.lba = cd->ms_offset;
	ms_info->xa_flag = cd->xa_flag || cd->ms_offset > 0;

	return 0;
}

int sr_get_mcn(struct cdrom_device_info *cdi, struct cdrom_mcn *mcn)
{
	Scsi_CD *cd = cdi->handle;
	struct packet_command cgc;
	char *buffer = kmalloc(32, GFP_KERNEL | SR_GFP_DMA(cd));
	int result;

	if (!buffer)
		return -ENOMEM;

	memset(&cgc, 0, sizeof(struct packet_command));
	cgc.cmd[0] = GPCMD_READ_SUBCHANNEL;
	cgc.cmd[2] = 0x40;	/* I do want the subchannel info */
	cgc.cmd[3] = 0x02;	/* Give me medium catalog number info */
	cgc.cmd[8] = 24;
	cgc.buffer = buffer;
	cgc.buflen = 24;
	cgc.data_direction = DMA_FROM_DEVICE;
	cgc.timeout = IOCTL_TIMEOUT;
	result = sr_do_ioctl(cd, &cgc);

	memcpy(mcn->medium_catalog_number, buffer + 9, 13);
	mcn->medium_catalog_number[13] = 0;

	kfree(buffer);
	return result;
}

int sr_reset(struct cdrom_device_info *cdi)
{
	return 0;
}

int sr_select_speed(struct cdrom_device_info *cdi, int speed)
{
	Scsi_CD *cd = cdi->handle;
	struct packet_command cgc;

	if (speed == 0)
		speed = 0xffff;	/* set to max */
	else
		speed *= 177;	/* Nx to kbyte/s */

	memset(&cgc, 0, sizeof(struct packet_command));
	cgc.cmd[0] = GPCMD_SET_SPEED;	/* SET CD SPEED */
	cgc.cmd[2] = (speed >> 8) & 0xff;	/* MSB for speed (in kbytes/sec) */
	cgc.cmd[3] = speed & 0xff;	/* LSB */
	cgc.data_direction = DMA_NONE;
	cgc.timeout = IOCTL_TIMEOUT;

	if (sr_do_ioctl(cd, &cgc))
		return -EIO;
	return 0;
}

/* ----------------------------------------------------------------------- */
/* this is called by the generic cdrom driver. arg is a _kernel_ pointer,  */
/* because the generic cdrom driver does the user access stuff for us.     */
/* only cdromreadtochdr and cdromreadtocentry are left - for use with the  */
/* sr_disk_status interface for the generic cdrom driver.                  */

int sr_audio_ioctl(struct cdrom_device_info *cdi, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case CDROMREADTOCHDR:
		return sr_read_tochdr(cdi, arg);
	case CDROMREADTOCENTRY:
		return sr_read_tocentry(cdi, arg);
	case CDROMPLAYTRKIND:
		return sr_play_trkind(cdi, arg);
	default:
		return -EINVAL;
	}
}

/* -----------------------------------------------------------------------
 * a function to read all sorts of funny cdrom sectors using the READ_CD
 * scsi-3 mmc command
 *
 * lba:     linear block address
 * format:  0 = data (anything)
 *          1 = audio
 *          2 = data (mode 1)
 *          3 = data (mode 2)
 *          4 = data (mode 2 form1)
 *          5 = data (mode 2 form2)
 * blksize: 2048 | 2336 | 2340 | 2352
 */

static int sr_read_cd(Scsi_CD *cd, unsigned char *dest, int lba, int format, int blksize)
{
	struct packet_command cgc;

#ifdef DEBUG
	sr_printk(KERN_INFO, cd, "sr_read_cd lba=%d format=%d blksize=%d\n",
		  lba, format, blksize);
#endif

	memset(&cgc, 0, sizeof(struct packet_command));
	cgc.cmd[0] = GPCMD_READ_CD;	/* READ_CD */
	cgc.cmd[1] = ((format & 7) << 2);
	cgc.cmd[2] = (unsigned char) (lba >> 24) & 0xff;
	cgc.cmd[3] = (unsigned char) (lba >> 16) & 0xff;
	cgc.cmd[4] = (unsigned char) (lba >> 8) & 0xff;
	cgc.cmd[5] = (unsigned char) lba & 0xff;
	cgc.cmd[8] = 1;
	switch (blksize) {
	case 2336:
		cgc.cmd[9] = 0x58;
		break;
	case 2340:
		cgc.cmd[9] = 0x78;
		break;
	case 2352:
		cgc.cmd[9] = 0xf8;
		break;
	default:
		cgc.cmd[9] = 0x10;
		break;
	}
	cgc.buffer = dest;
	cgc.buflen = blksize;
	cgc.data_direction = DMA_FROM_DEVICE;
	cgc.timeout = IOCTL_TIMEOUT;
	return sr_do_ioctl(cd, &cgc);
}

/*
 * read sectors with blocksizes other than 2048
 */

static int sr_read_sector(Scsi_CD *cd, int lba, int blksize, unsigned char *dest)
{
	struct packet_command cgc;
	int rc;

	/* we try the READ CD command first... */
	if (cd->readcd_known) {
		rc = sr_read_cd(cd, dest, lba, 0, blksize);
		if (-EDRIVE_CANT_DO_THIS != rc)
			return rc;
		cd->readcd_known = 0;
		sr_printk(KERN_INFO, cd,
			  "CDROM does'nt support READ CD (0xbe) command\n");
		/* fall & retry the other way */
	}
	/* ... if this fails, we switch the blocksize using MODE SELECT */
	if (blksize != cd->device->sector_size) {
		if (0 != (rc = sr_set_blocklength(cd, blksize)))
			return rc;
	}
#ifdef DEBUG
	sr_printk(KERN_INFO, cd, "sr_read_sector lba=%d blksize=%d\n",
		  lba, blksize);
#endif

	memset(&cgc, 0, sizeof(struct packet_command));
	cgc.cmd[0] = GPCMD_READ_10;
	cgc.cmd[2] = (unsigned char) (lba >> 24) & 0xff;
	cgc.cmd[3] = (unsigned char) (lba >> 16) & 0xff;
	cgc.cmd[4] = (unsigned char) (lba >> 8) & 0xff;
	cgc.cmd[5] = (unsigned char) lba & 0xff;
	cgc.cmd[8] = 1;
	cgc.buffer = dest;
	cgc.buflen = blksize;
	cgc.data_direction = DMA_FROM_DEVICE;
	cgc.timeout = IOCTL_TIMEOUT;
	rc = sr_do_ioctl(cd, &cgc);

	return rc;
}

/*
 * read a sector in raw mode to check the sector format
 * ret: 1 == mode2 (XA), 0 == mode1, <0 == error 
 */

int sr_is_xa(Scsi_CD *cd)
{
	unsigned char *raw_sector;
	int is_xa;

	if (!xa_test)
		return 0;

	raw_sector = kmalloc(2048, GFP_KERNEL | SR_GFP_DMA(cd));
	if (!raw_sector)
		return -ENOMEM;
	if (0 == sr_read_sector(cd, cd->ms_offset + 16,
				CD_FRAMESIZE_RAW1, raw_sector)) {
		is_xa = (raw_sector[3] == 0x02) ? 1 : 0;
	} else {
		/* read a raw sector failed for some reason. */
		is_xa = -1;
	}
	kfree(raw_sector);
#ifdef DEBUG
	sr_printk(KERN_INFO, cd, "sr_is_xa: %d\n", is_xa);
#endif
	return is_xa;
}
