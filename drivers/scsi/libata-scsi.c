/*
 *  libata-scsi.c - helper library for ATA
 *
 *  Maintained by:  Jeff Garzik <jgarzik@pobox.com>
 *    		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2004 Jeff Garzik
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 *  Hardware documentation available from
 *  - http://www.t10.org/
 *  - http://www.t13.org/
 *
 */

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <scsi/scsi.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <asm/uaccess.h>

#include "libata.h"

typedef unsigned int (*ata_xlat_func_t)(struct ata_queued_cmd *qc, u8 *scsicmd);
static struct ata_device *
ata_scsi_find_dev(struct ata_port *ap, struct scsi_device *scsidev);


/**
 *	ata_std_bios_param - generic bios head/sector/cylinder calculator used by sd.
 *	@sdev: SCSI device for which BIOS geometry is to be determined
 *	@bdev: block device associated with @sdev
 *	@capacity: capacity of SCSI device
 *	@geom: location to which geometry will be output
 *
 *	Generic bios head/sector/cylinder calculator
 *	used by sd. Most BIOSes nowadays expect a XXX/255/16  (CHS)
 *	mapping. Some situations may arise where the disk is not
 *	bootable if this is not used.
 *
 *	LOCKING:
 *	Defined by the SCSI layer.  We don't really care.
 *
 *	RETURNS:
 *	Zero.
 */
int ata_std_bios_param(struct scsi_device *sdev, struct block_device *bdev,
		       sector_t capacity, int geom[])
{
	geom[0] = 255;
	geom[1] = 63;
	sector_div(capacity, 255*63);
	geom[2] = capacity;

	return 0;
}

int ata_scsi_ioctl(struct scsi_device *scsidev, int cmd, void __user *arg)
{
	struct ata_port *ap;
	struct ata_device *dev;
	int val = -EINVAL, rc = -EINVAL;

	ap = (struct ata_port *) &scsidev->host->hostdata[0];
	if (!ap)
		goto out;

	dev = ata_scsi_find_dev(ap, scsidev);
	if (!dev) {
		rc = -ENODEV;
		goto out;
	}

	switch (cmd) {
	case ATA_IOC_GET_IO32:
		val = 0;
		if (copy_to_user(arg, &val, 1))
			return -EFAULT;
		return 0;

	case ATA_IOC_SET_IO32:
		val = (unsigned long) arg;
		if (val != 0)
			return -EINVAL;
		return 0;

	default:
		rc = -ENOTTY;
		break;
	}

out:
	return rc;
}

/**
 *	ata_scsi_qc_new - acquire new ata_queued_cmd reference
 *	@ap: ATA port to which the new command is attached
 *	@dev: ATA device to which the new command is attached
 *	@cmd: SCSI command that originated this ATA command
 *	@done: SCSI command completion function
 *
 *	Obtain a reference to an unused ata_queued_cmd structure,
 *	which is the basic libata structure representing a single
 *	ATA command sent to the hardware.
 *
 *	If a command was available, fill in the SCSI-specific
 *	portions of the structure with information on the
 *	current command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Command allocated, or %NULL if none available.
 */
struct ata_queued_cmd *ata_scsi_qc_new(struct ata_port *ap,
				       struct ata_device *dev,
				       struct scsi_cmnd *cmd,
				       void (*done)(struct scsi_cmnd *))
{
	struct ata_queued_cmd *qc;

	qc = ata_qc_new_init(ap, dev);
	if (qc) {
		qc->scsicmd = cmd;
		qc->scsidone = done;

		if (cmd->use_sg) {
			qc->sg = (struct scatterlist *) cmd->request_buffer;
			qc->n_elem = cmd->use_sg;
		} else {
			qc->sg = &qc->sgent;
			qc->n_elem = 1;
		}
	} else {
		cmd->result = (DID_OK << 16) | (QUEUE_FULL << 1);
		done(cmd);
	}

	return qc;
}

/**
 *	ata_to_sense_error - convert ATA error to SCSI error
 *	@qc: Command that we are erroring out
 *	@drv_stat: value contained in ATA status register
 *
 *	Converts an ATA error into a SCSI error. While we are at it
 *	we decode and dump the ATA error for the user so that they
 *	have some idea what really happened at the non make-believe
 *	layer.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_to_sense_error(struct ata_queued_cmd *qc, u8 drv_stat)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	u8 err = 0;
	unsigned char *sb = cmd->sense_buffer;
	/* Based on the 3ware driver translation table */
	static unsigned char sense_table[][4] = {
		/* BBD|ECC|ID|MAR */
		{0xd1, 		ABORTED_COMMAND, 0x00, 0x00}, 	// Device busy                  Aborted command
		/* BBD|ECC|ID */
		{0xd0,  	ABORTED_COMMAND, 0x00, 0x00}, 	// Device busy                  Aborted command
		/* ECC|MC|MARK */
		{0x61, 		HARDWARE_ERROR, 0x00, 0x00}, 	// Device fault                 Hardware error
		/* ICRC|ABRT */		/* NB: ICRC & !ABRT is BBD */
		{0x84, 		ABORTED_COMMAND, 0x47, 0x00}, 	// Data CRC error               SCSI parity error
		/* MC|ID|ABRT|TRK0|MARK */
		{0x37, 		NOT_READY, 0x04, 0x00}, 	// Unit offline                 Not ready
		/* MCR|MARK */
		{0x09, 		NOT_READY, 0x04, 0x00}, 	// Unrecovered disk error       Not ready
		/*  Bad address mark */
		{0x01, 		MEDIUM_ERROR, 0x13, 0x00}, 	// Address mark not found       Address mark not found for data field
		/* TRK0 */
		{0x02, 		HARDWARE_ERROR, 0x00, 0x00}, 	// Track 0 not found		  Hardware error
		/* Abort & !ICRC */
		{0x04, 		ABORTED_COMMAND, 0x00, 0x00}, 	// Aborted command              Aborted command
		/* Media change request */
		{0x08, 		NOT_READY, 0x04, 0x00}, 	// Media change request	  FIXME: faking offline
		/* SRV */
		{0x10, 		ABORTED_COMMAND, 0x14, 0x00}, 	// ID not found                 Recorded entity not found
		/* Media change */
		{0x08,  	NOT_READY, 0x04, 0x00}, 	// Media change		  FIXME: faking offline
		/* ECC */
		{0x40, 		MEDIUM_ERROR, 0x11, 0x04}, 	// Uncorrectable ECC error      Unrecovered read error
		/* BBD - block marked bad */
		{0x80, 		MEDIUM_ERROR, 0x11, 0x04}, 	// Block marked bad		  Medium error, unrecovered read error
		{0xFF, 0xFF, 0xFF, 0xFF}, // END mark
	};
	static unsigned char stat_table[][4] = {
		/* Must be first because BUSY means no other bits valid */
		{0x80, 		ABORTED_COMMAND, 0x47, 0x00},	// Busy, fake parity for now
		{0x20, 		HARDWARE_ERROR,  0x00, 0x00}, 	// Device fault
		{0x08, 		ABORTED_COMMAND, 0x47, 0x00},	// Timed out in xfer, fake parity for now
		{0x04, 		RECOVERED_ERROR, 0x11, 0x00},	// Recovered ECC error	  Medium error, recovered
		{0xFF, 0xFF, 0xFF, 0xFF}, // END mark
	};
	int i = 0;

	cmd->result = SAM_STAT_CHECK_CONDITION;

	/*
	 *	Is this an error we can process/parse
	 */

	if(drv_stat & ATA_ERR)
		/* Read the err bits */
		err = ata_chk_err(qc->ap);

	/* Display the ATA level error info */

	printk(KERN_WARNING "ata%u: status=0x%02x { ", qc->ap->id, drv_stat);
	if(drv_stat & 0x80)
	{
		printk("Busy ");
		err = 0;	/* Data is not valid in this case */
	}
	else {
		if(drv_stat & 0x40)	printk("DriveReady ");
		if(drv_stat & 0x20)	printk("DeviceFault ");
		if(drv_stat & 0x10)	printk("SeekComplete ");
		if(drv_stat & 0x08)	printk("DataRequest ");
		if(drv_stat & 0x04)	printk("CorrectedError ");
		if(drv_stat & 0x02)	printk("Index ");
		if(drv_stat & 0x01)	printk("Error ");
	}
	printk("}\n");

	if(err)
	{
		printk(KERN_WARNING "ata%u: error=0x%02x { ", qc->ap->id, err);
		if(err & 0x04)		printk("DriveStatusError ");
		if(err & 0x80)
		{
			if(err & 0x04)
				printk("BadCRC ");
			else
				printk("Sector ");
		}
		if(err & 0x40)		printk("UncorrectableError ");
		if(err & 0x10)		printk("SectorIdNotFound ");
		if(err & 0x02)		printk("TrackZeroNotFound ");
		if(err & 0x01)		printk("AddrMarkNotFound ");
		printk("}\n");

		/* Should we dump sector info here too ?? */
	}


	/* Look for err */
	while(sense_table[i][0] != 0xFF)
	{
		/* Look for best matches first */
		if((sense_table[i][0] & err) == sense_table[i][0])
		{
			sb[0] = 0x70;
			sb[2] = sense_table[i][1];
			sb[7] = 0x0a;
			sb[12] = sense_table[i][2];
			sb[13] = sense_table[i][3];
			return;
		}
		i++;
	}
	/* No immediate match */
	if(err)
		printk(KERN_DEBUG "ata%u: no sense translation for 0x%02x\n", qc->ap->id, err);

	i = 0;
	/* Fall back to interpreting status bits */
	while(stat_table[i][0] != 0xFF)
	{
		if(stat_table[i][0] & drv_stat)
		{
			sb[0] = 0x70;
			sb[2] = stat_table[i][1];
			sb[7] = 0x0a;
			sb[12] = stat_table[i][2];
			sb[13] = stat_table[i][3];
			return;
		}
		i++;
	}
	/* No error ?? */
	printk(KERN_ERR "ata%u: called with no error (%02X)!\n", qc->ap->id, drv_stat);
	/* additional-sense-code[-qualifier] */

	sb[0] = 0x70;
	sb[2] = MEDIUM_ERROR;
	sb[7] = 0x0A;
	if (cmd->sc_data_direction == DMA_FROM_DEVICE) {
		sb[12] = 0x11; /* "unrecovered read error" */
		sb[13] = 0x04;
	} else {
		sb[12] = 0x0C; /* "write error -             */
		sb[13] = 0x02; /*  auto-reallocation failed" */
	}
}

/**
 *	ata_scsi_slave_config - Set SCSI device attributes
 *	@sdev: SCSI device to examine
 *
 *	This is called before we actually start reading
 *	and writing to the device, to configure certain
 *	SCSI mid-layer behaviors.
 *
 *	LOCKING:
 *	Defined by SCSI layer.  We don't really care.
 */

int ata_scsi_slave_config(struct scsi_device *sdev)
{
	sdev->use_10_for_rw = 1;
	sdev->use_10_for_ms = 1;

	blk_queue_max_phys_segments(sdev->request_queue, LIBATA_MAX_PRD);

	if (sdev->id < ATA_MAX_DEVICES) {
		struct ata_port *ap;
		struct ata_device *dev;

		ap = (struct ata_port *) &sdev->host->hostdata[0];
		dev = &ap->device[sdev->id];

		/* TODO: 1024 is an arbitrary number, not the
		 * hardware maximum.  This should be increased to
		 * 65534 when Jens Axboe's patch for dynamically
		 * determining max_sectors is merged.
		 */
		if ((dev->flags & ATA_DFLAG_LBA48) &&
		    ((dev->flags & ATA_DFLAG_LOCK_SECTORS) == 0)) {
			/*
			 * do not overwrite sdev->host->max_sectors, since
			 * other drives on this host may not support LBA48
			 */
			blk_queue_max_sectors(sdev->request_queue, 2048);
		}
	}

	return 0;	/* scsi layer doesn't check return value, sigh */
}

/**
 *	ata_scsi_error - SCSI layer error handler callback
 *	@host: SCSI host on which error occurred
 *
 *	Handles SCSI-layer-thrown error events.
 *
 *	LOCKING:
 *	Inherited from SCSI layer (none, can sleep)
 *
 *	RETURNS:
 *	Zero.
 */

int ata_scsi_error(struct Scsi_Host *host)
{
	struct ata_port *ap;

	DPRINTK("ENTER\n");

	ap = (struct ata_port *) &host->hostdata[0];
	ap->ops->eng_timeout(ap);

	/* TODO: this is per-command; when queueing is supported
	 * this code will either change or move to a more
	 * appropriate place
	 */
	host->host_failed--;
	INIT_LIST_HEAD(&host->eh_cmd_q);

	DPRINTK("EXIT\n");
	return 0;
}

/**
 *	ata_scsi_start_stop_xlat - Translate SCSI START STOP UNIT command
 *	@qc: Storage for translated ATA taskfile
 *	@scsicmd: SCSI command to translate
 *
 *	Sets up an ATA taskfile to issue STANDBY (to stop) or READ VERIFY
 *	(to start). Perhaps these commands should be preceded by
 *	CHECK POWER MODE to see what power mode the device is already in.
 *	[See SAT revision 5 at www.t10.org]
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */

static unsigned int ata_scsi_start_stop_xlat(struct ata_queued_cmd *qc,
					     u8 *scsicmd)
{
	struct ata_taskfile *tf = &qc->tf;

	tf->flags |= ATA_TFLAG_DEVICE | ATA_TFLAG_ISADDR;
	tf->protocol = ATA_PROT_NODATA;
	if (scsicmd[1] & 0x1) {
		;	/* ignore IMMED bit, violates sat-r05 */
	}
	if (scsicmd[4] & 0x2)
		return 1;	/* LOEJ bit set not supported */
	if (((scsicmd[4] >> 4) & 0xf) != 0)
		return 1;	/* power conditions not supported */
	if (scsicmd[4] & 0x1) {
		tf->nsect = 1;	/* 1 sector, lba=0 */
		tf->lbah = 0x0;
		tf->lbam = 0x0;
		tf->lbal = 0x0;
		tf->device |= ATA_LBA;
		tf->command = ATA_CMD_VERIFY;	/* READ VERIFY */
	} else {
		tf->nsect = 0;	/* time period value (0 implies now) */
		tf->command = ATA_CMD_STANDBY;
		/* Consider: ATA STANDBY IMMEDIATE command */
	}
	/*
	 * Standby and Idle condition timers could be implemented but that
	 * would require libata to implement the Power condition mode page
	 * and allow the user to change it. Changing mode pages requires
	 * MODE SELECT to be implemented.
	 */

	return 0;
}


/**
 *	ata_scsi_flush_xlat - Translate SCSI SYNCHRONIZE CACHE command
 *	@qc: Storage for translated ATA taskfile
 *	@scsicmd: SCSI command to translate (ignored)
 *
 *	Sets up an ATA taskfile to issue FLUSH CACHE or
 *	FLUSH CACHE EXT.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */

static unsigned int ata_scsi_flush_xlat(struct ata_queued_cmd *qc, u8 *scsicmd)
{
	struct ata_taskfile *tf = &qc->tf;

	tf->flags |= ATA_TFLAG_DEVICE;
	tf->protocol = ATA_PROT_NODATA;

	if ((tf->flags & ATA_TFLAG_LBA48) &&
	    (ata_id_has_flush_ext(qc->dev->id)))
		tf->command = ATA_CMD_FLUSH_EXT;
	else
		tf->command = ATA_CMD_FLUSH;

	return 0;
}

/**
 *	ata_scsi_verify_xlat - Translate SCSI VERIFY command into an ATA one
 *	@qc: Storage for translated ATA taskfile
 *	@scsicmd: SCSI command to translate
 *
 *	Converts SCSI VERIFY command to an ATA READ VERIFY command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */

static unsigned int ata_scsi_verify_xlat(struct ata_queued_cmd *qc, u8 *scsicmd)
{
	struct ata_taskfile *tf = &qc->tf;
	unsigned int lba48 = tf->flags & ATA_TFLAG_LBA48;
	u64 dev_sectors = qc->dev->n_sectors;
	u64 sect = 0;
	u32 n_sect = 0;

	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf->protocol = ATA_PROT_NODATA;
	tf->device |= ATA_LBA;

	if (scsicmd[0] == VERIFY) {
		sect |= ((u64)scsicmd[2]) << 24;
		sect |= ((u64)scsicmd[3]) << 16;
		sect |= ((u64)scsicmd[4]) << 8;
		sect |= ((u64)scsicmd[5]);

		n_sect |= ((u32)scsicmd[7]) << 8;
		n_sect |= ((u32)scsicmd[8]);
	}

	else if (scsicmd[0] == VERIFY_16) {
		sect |= ((u64)scsicmd[2]) << 56;
		sect |= ((u64)scsicmd[3]) << 48;
		sect |= ((u64)scsicmd[4]) << 40;
		sect |= ((u64)scsicmd[5]) << 32;
		sect |= ((u64)scsicmd[6]) << 24;
		sect |= ((u64)scsicmd[7]) << 16;
		sect |= ((u64)scsicmd[8]) << 8;
		sect |= ((u64)scsicmd[9]);

		n_sect |= ((u32)scsicmd[10]) << 24;
		n_sect |= ((u32)scsicmd[11]) << 16;
		n_sect |= ((u32)scsicmd[12]) << 8;
		n_sect |= ((u32)scsicmd[13]);
	}

	else
		return 1;

	if (!n_sect)
		return 1;
	if (sect >= dev_sectors)
		return 1;
	if ((sect + n_sect) > dev_sectors)
		return 1;
	if (lba48) {
		if (n_sect > (64 * 1024))
			return 1;
	} else {
		if (n_sect > 256)
			return 1;
	}

	if (lba48) {
		tf->command = ATA_CMD_VERIFY_EXT;

		tf->hob_nsect = (n_sect >> 8) & 0xff;

		tf->hob_lbah = (sect >> 40) & 0xff;
		tf->hob_lbam = (sect >> 32) & 0xff;
		tf->hob_lbal = (sect >> 24) & 0xff;
	} else {
		tf->command = ATA_CMD_VERIFY;

		tf->device |= (sect >> 24) & 0xf;
	}

	tf->nsect = n_sect & 0xff;

	tf->lbah = (sect >> 16) & 0xff;
	tf->lbam = (sect >> 8) & 0xff;
	tf->lbal = sect & 0xff;

	return 0;
}

/**
 *	ata_scsi_rw_xlat - Translate SCSI r/w command into an ATA one
 *	@qc: Storage for translated ATA taskfile
 *	@scsicmd: SCSI command to translate
 *
 *	Converts any of six SCSI read/write commands into the
 *	ATA counterpart, including starting sector (LBA),
 *	sector count, and taking into account the device's LBA48
 *	support.
 *
 *	Commands %READ_6, %READ_10, %READ_16, %WRITE_6, %WRITE_10, and
 *	%WRITE_16 are currently supported.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */

static unsigned int ata_scsi_rw_xlat(struct ata_queued_cmd *qc, u8 *scsicmd)
{
	struct ata_taskfile *tf = &qc->tf;
	unsigned int lba48 = tf->flags & ATA_TFLAG_LBA48;

	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf->protocol = qc->dev->xfer_protocol;
	tf->device |= ATA_LBA;

	if (scsicmd[0] == READ_10 || scsicmd[0] == READ_6 ||
	    scsicmd[0] == READ_16) {
		tf->command = qc->dev->read_cmd;
	} else {
		tf->command = qc->dev->write_cmd;
		tf->flags |= ATA_TFLAG_WRITE;
	}

	if (scsicmd[0] == READ_10 || scsicmd[0] == WRITE_10) {
		if (lba48) {
			tf->hob_nsect = scsicmd[7];
			tf->hob_lbal = scsicmd[2];

			qc->nsect = ((unsigned int)scsicmd[7] << 8) |
					scsicmd[8];
		} else {
			/* if we don't support LBA48 addressing, the request
			 * -may- be too large. */
			if ((scsicmd[2] & 0xf0) || scsicmd[7])
				return 1;

			/* stores LBA27:24 in lower 4 bits of device reg */
			tf->device |= scsicmd[2];

			qc->nsect = scsicmd[8];
		}

		tf->nsect = scsicmd[8];
		tf->lbal = scsicmd[5];
		tf->lbam = scsicmd[4];
		tf->lbah = scsicmd[3];

		VPRINTK("ten-byte command\n");
		if (qc->nsect == 0) /* we don't support length==0 cmds */
			return 1;
		return 0;
	}

	if (scsicmd[0] == READ_6 || scsicmd[0] == WRITE_6) {
		qc->nsect = tf->nsect = scsicmd[4];
		if (!qc->nsect) {
			qc->nsect = 256;
			if (lba48)
				tf->hob_nsect = 1;
		}

		tf->lbal = scsicmd[3];
		tf->lbam = scsicmd[2];
		tf->lbah = scsicmd[1] & 0x1f; /* mask out reserved bits */

		VPRINTK("six-byte command\n");
		return 0;
	}

	if (scsicmd[0] == READ_16 || scsicmd[0] == WRITE_16) {
		/* rule out impossible LBAs and sector counts */
		if (scsicmd[2] || scsicmd[3] || scsicmd[10] || scsicmd[11])
			return 1;

		if (lba48) {
			tf->hob_nsect = scsicmd[12];
			tf->hob_lbal = scsicmd[6];
			tf->hob_lbam = scsicmd[5];
			tf->hob_lbah = scsicmd[4];

			qc->nsect = ((unsigned int)scsicmd[12] << 8) |
					scsicmd[13];
		} else {
			/* once again, filter out impossible non-zero values */
			if (scsicmd[4] || scsicmd[5] || scsicmd[12] ||
			    (scsicmd[6] & 0xf0))
				return 1;

			/* stores LBA27:24 in lower 4 bits of device reg */
			tf->device |= scsicmd[6];

			qc->nsect = scsicmd[13];
		}

		tf->nsect = scsicmd[13];
		tf->lbal = scsicmd[9];
		tf->lbam = scsicmd[8];
		tf->lbah = scsicmd[7];

		VPRINTK("sixteen-byte command\n");
		if (qc->nsect == 0) /* we don't support length==0 cmds */
			return 1;
		return 0;
	}

	DPRINTK("no-byte command\n");
	return 1;
}

static int ata_scsi_qc_complete(struct ata_queued_cmd *qc, u8 drv_stat)
{
	struct scsi_cmnd *cmd = qc->scsicmd;

	if (unlikely(drv_stat & (ATA_ERR | ATA_BUSY | ATA_DRQ)))
		ata_to_sense_error(qc, drv_stat);
	else
		cmd->result = SAM_STAT_GOOD;

	qc->scsidone(cmd);

	return 0;
}

/**
 *	ata_scsi_translate - Translate then issue SCSI command to ATA device
 *	@ap: ATA port to which the command is addressed
 *	@dev: ATA device to which the command is addressed
 *	@cmd: SCSI command to execute
 *	@done: SCSI command completion function
 *	@xlat_func: Actor which translates @cmd to an ATA taskfile
 *
 *	Our ->queuecommand() function has decided that the SCSI
 *	command issued can be directly translated into an ATA
 *	command, rather than handled internally.
 *
 *	This function sets up an ata_queued_cmd structure for the
 *	SCSI command, and sends that ata_queued_cmd to the hardware.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_scsi_translate(struct ata_port *ap, struct ata_device *dev,
			      struct scsi_cmnd *cmd,
			      void (*done)(struct scsi_cmnd *),
			      ata_xlat_func_t xlat_func)
{
	struct ata_queued_cmd *qc;
	u8 *scsicmd = cmd->cmnd;

	VPRINTK("ENTER\n");

	qc = ata_scsi_qc_new(ap, dev, cmd, done);
	if (!qc)
		return;

	/* data is present; dma-map it */
	if (cmd->sc_data_direction == DMA_FROM_DEVICE ||
	    cmd->sc_data_direction == DMA_TO_DEVICE) {
		if (unlikely(cmd->request_bufflen < 1)) {
			printk(KERN_WARNING "ata%u(%u): WARNING: zero len r/w req\n",
			       ap->id, dev->devno);
			goto err_out;
		}

		if (cmd->use_sg)
			ata_sg_init(qc, cmd->request_buffer, cmd->use_sg);
		else
			ata_sg_init_one(qc, cmd->request_buffer,
					cmd->request_bufflen);

		qc->dma_dir = cmd->sc_data_direction;
	}

	qc->complete_fn = ata_scsi_qc_complete;

	if (xlat_func(qc, scsicmd))
		goto err_out;

	/* select device, send command to hardware */
	if (ata_qc_issue(qc))
		goto err_out;

	VPRINTK("EXIT\n");
	return;

err_out:
	ata_qc_free(qc);
	ata_bad_cdb(cmd, done);
	DPRINTK("EXIT - badcmd\n");
}

/**
 *	ata_scsi_rbuf_get - Map response buffer.
 *	@cmd: SCSI command containing buffer to be mapped.
 *	@buf_out: Pointer to mapped area.
 *
 *	Maps buffer contained within SCSI command @cmd.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Length of response buffer.
 */

static unsigned int ata_scsi_rbuf_get(struct scsi_cmnd *cmd, u8 **buf_out)
{
	u8 *buf;
	unsigned int buflen;

	if (cmd->use_sg) {
		struct scatterlist *sg;

		sg = (struct scatterlist *) cmd->request_buffer;
		buf = kmap_atomic(sg->page, KM_USER0) + sg->offset;
		buflen = sg->length;
	} else {
		buf = cmd->request_buffer;
		buflen = cmd->request_bufflen;
	}

	*buf_out = buf;
	return buflen;
}

/**
 *	ata_scsi_rbuf_put - Unmap response buffer.
 *	@cmd: SCSI command containing buffer to be unmapped.
 *	@buf: buffer to unmap
 *
 *	Unmaps response buffer contained within @cmd.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static inline void ata_scsi_rbuf_put(struct scsi_cmnd *cmd, u8 *buf)
{
	if (cmd->use_sg) {
		struct scatterlist *sg;

		sg = (struct scatterlist *) cmd->request_buffer;
		kunmap_atomic(buf - sg->offset, KM_USER0);
	}
}

/**
 *	ata_scsi_rbuf_fill - wrapper for SCSI command simulators
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@actor: Callback hook for desired SCSI command simulator
 *
 *	Takes care of the hard work of simulating a SCSI command...
 *	Mapping the response buffer, calling the command's handler,
 *	and handling the handler's return value.  This return value
 *	indicates whether the handler wishes the SCSI command to be
 *	completed successfully, or not.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_scsi_rbuf_fill(struct ata_scsi_args *args,
		        unsigned int (*actor) (struct ata_scsi_args *args,
			     		   u8 *rbuf, unsigned int buflen))
{
	u8 *rbuf;
	unsigned int buflen, rc;
	struct scsi_cmnd *cmd = args->cmd;

	buflen = ata_scsi_rbuf_get(cmd, &rbuf);
	memset(rbuf, 0, buflen);
	rc = actor(args, rbuf, buflen);
	ata_scsi_rbuf_put(cmd, rbuf);

	if (rc)
		ata_bad_cdb(cmd, args->done);
	else {
		cmd->result = SAM_STAT_GOOD;
		args->done(cmd);
	}
}

/**
 *	ata_scsiop_inq_std - Simulate INQUIRY command
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Returns standard device identification data associated
 *	with non-EVPD INQUIRY command output.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_inq_std(struct ata_scsi_args *args, u8 *rbuf,
			       unsigned int buflen)
{
	u8 hdr[] = {
		TYPE_DISK,
		0,
		0x5,	/* claim SPC-3 version compatibility */
		2,
		95 - 4
	};

	/* set scsi removeable (RMB) bit per ata bit */
	if (ata_id_removeable(args->id))
		hdr[1] |= (1 << 7);

	VPRINTK("ENTER\n");

	memcpy(rbuf, hdr, sizeof(hdr));

	if (buflen > 35) {
		memcpy(&rbuf[8], "ATA     ", 8);
		ata_dev_id_string(args->id, &rbuf[16], ATA_ID_PROD_OFS, 16);
		ata_dev_id_string(args->id, &rbuf[32], ATA_ID_FW_REV_OFS, 4);
		if (rbuf[32] == 0 || rbuf[32] == ' ')
			memcpy(&rbuf[32], "n/a ", 4);
	}

	if (buflen > 63) {
		const u8 versions[] = {
			0x60,	/* SAM-3 (no version claimed) */

			0x03,
			0x20,	/* SBC-2 (no version claimed) */

			0x02,
			0x60	/* SPC-3 (no version claimed) */
		};

		memcpy(rbuf + 59, versions, sizeof(versions));
	}

	return 0;
}

/**
 *	ata_scsiop_inq_00 - Simulate INQUIRY EVPD page 0, list of pages
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Returns list of inquiry EVPD pages available.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_inq_00(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen)
{
	const u8 pages[] = {
		0x00,	/* page 0x00, this page */
		0x80,	/* page 0x80, unit serial no page */
		0x83	/* page 0x83, device ident page */
	};
	rbuf[3] = sizeof(pages);	/* number of supported EVPD pages */

	if (buflen > 6)
		memcpy(rbuf + 4, pages, sizeof(pages));

	return 0;
}

/**
 *	ata_scsiop_inq_80 - Simulate INQUIRY EVPD page 80, device serial number
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Returns ATA device serial number.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_inq_80(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen)
{
	const u8 hdr[] = {
		0,
		0x80,			/* this page code */
		0,
		ATA_SERNO_LEN,		/* page len */
	};
	memcpy(rbuf, hdr, sizeof(hdr));

	if (buflen > (ATA_SERNO_LEN + 4 - 1))
		ata_dev_id_string(args->id, (unsigned char *) &rbuf[4],
				  ATA_ID_SERNO_OFS, ATA_SERNO_LEN);

	return 0;
}

static const char *inq_83_str = "Linux ATA-SCSI simulator";

/**
 *	ata_scsiop_inq_83 - Simulate INQUIRY EVPD page 83, device identity
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Returns device identification.  Currently hardcoded to
 *	return "Linux ATA-SCSI simulator".
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_inq_83(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen)
{
	rbuf[1] = 0x83;			/* this page code */
	rbuf[3] = 4 + strlen(inq_83_str);	/* page len */

	/* our one and only identification descriptor (vendor-specific) */
	if (buflen > (strlen(inq_83_str) + 4 + 4 - 1)) {
		rbuf[4 + 0] = 2;	/* code set: ASCII */
		rbuf[4 + 3] = strlen(inq_83_str);
		memcpy(rbuf + 4 + 4, inq_83_str, strlen(inq_83_str));
	}

	return 0;
}

/**
 *	ata_scsiop_noop - Command handler that simply returns success.
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	No operation.  Simply returns success to caller, to indicate
 *	that the caller should successfully complete this SCSI command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_noop(struct ata_scsi_args *args, u8 *rbuf,
			    unsigned int buflen)
{
	VPRINTK("ENTER\n");
	return 0;
}

/**
 *	ata_msense_push - Push data onto MODE SENSE data output buffer
 *	@ptr_io: (input/output) Location to store more output data
 *	@last: End of output data buffer
 *	@buf: Pointer to BLOB being added to output buffer
 *	@buflen: Length of BLOB
 *
 *	Store MODE SENSE data on an output buffer.
 *
 *	LOCKING:
 *	None.
 */

static void ata_msense_push(u8 **ptr_io, const u8 *last,
			    const u8 *buf, unsigned int buflen)
{
	u8 *ptr = *ptr_io;

	if ((ptr + buflen - 1) > last)
		return;

	memcpy(ptr, buf, buflen);

	ptr += buflen;

	*ptr_io = ptr;
}

/**
 *	ata_msense_caching - Simulate MODE SENSE caching info page
 *	@id: device IDENTIFY data
 *	@ptr_io: (input/output) Location to store more output data
 *	@last: End of output data buffer
 *
 *	Generate a caching info page, which conditionally indicates
 *	write caching to the SCSI layer, depending on device
 *	capabilities.
 *
 *	LOCKING:
 *	None.
 */

static unsigned int ata_msense_caching(u16 *id, u8 **ptr_io,
				       const u8 *last)
{
	u8 page[] = {
		0x8,				/* page code */
		0x12,				/* page length */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 10 zeroes */
		0, 0, 0, 0, 0, 0, 0, 0		/* 8 zeroes */
	};

	if (ata_id_wcache_enabled(id))
		page[2] |= (1 << 2);	/* write cache enable */
	if (!ata_id_rahead_enabled(id))
		page[12] |= (1 << 5);	/* disable read ahead */

	ata_msense_push(ptr_io, last, page, sizeof(page));
	return sizeof(page);
}

/**
 *	ata_msense_ctl_mode - Simulate MODE SENSE control mode page
 *	@dev: Device associated with this MODE SENSE command
 *	@ptr_io: (input/output) Location to store more output data
 *	@last: End of output data buffer
 *
 *	Generate a generic MODE SENSE control mode page.
 *
 *	LOCKING:
 *	None.
 */

static unsigned int ata_msense_ctl_mode(u8 **ptr_io, const u8 *last)
{
	const u8 page[] = {0xa, 0xa, 6, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 30};

	/* byte 2: set the descriptor format sense data bit (bit 2)
	 * since we need to support returning this format for SAT
	 * commands and any SCSI commands against a 48b LBA device.
	 */

	ata_msense_push(ptr_io, last, page, sizeof(page));
	return sizeof(page);
}

/**
 *	ata_msense_rw_recovery - Simulate MODE SENSE r/w error recovery page
 *	@dev: Device associated with this MODE SENSE command
 *	@ptr_io: (input/output) Location to store more output data
 *	@last: End of output data buffer
 *
 *	Generate a generic MODE SENSE r/w error recovery page.
 *
 *	LOCKING:
 *	None.
 */

static unsigned int ata_msense_rw_recovery(u8 **ptr_io, const u8 *last)
{
	const u8 page[] = {
		0x1,			  /* page code */
		0xa,			  /* page length */
		(1 << 7) | (1 << 6),	  /* note auto r/w reallocation */
		0, 0, 0, 0, 0, 0, 0, 0, 0 /* 9 zeroes */
	};

	ata_msense_push(ptr_io, last, page, sizeof(page));
	return sizeof(page);
}

/**
 *	ata_scsiop_mode_sense - Simulate MODE SENSE 6, 10 commands
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Simulate MODE SENSE commands.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_mode_sense(struct ata_scsi_args *args, u8 *rbuf,
				  unsigned int buflen)
{
	u8 *scsicmd = args->cmd->cmnd, *p, *last;
	unsigned int page_control, six_byte, output_len;

	VPRINTK("ENTER\n");

	six_byte = (scsicmd[0] == MODE_SENSE);

	/* we only support saved and current values (which we treat
	 * in the same manner)
	 */
	page_control = scsicmd[2] >> 6;
	if ((page_control != 0) && (page_control != 3))
		return 1;

	if (six_byte)
		output_len = 4;
	else
		output_len = 8;

	p = rbuf + output_len;
	last = rbuf + buflen - 1;

	switch(scsicmd[2] & 0x3f) {
	case 0x01:		/* r/w error recovery */
		output_len += ata_msense_rw_recovery(&p, last);
		break;

	case 0x08:		/* caching */
		output_len += ata_msense_caching(args->id, &p, last);
		break;

	case 0x0a: {		/* control mode */
		output_len += ata_msense_ctl_mode(&p, last);
		break;
		}

	case 0x3f:		/* all pages */
		output_len += ata_msense_rw_recovery(&p, last);
		output_len += ata_msense_caching(args->id, &p, last);
		output_len += ata_msense_ctl_mode(&p, last);
		break;

	default:		/* invalid page code */
		return 1;
	}

	if (six_byte) {
		output_len--;
		rbuf[0] = output_len;
	} else {
		output_len -= 2;
		rbuf[0] = output_len >> 8;
		rbuf[1] = output_len;
	}

	return 0;
}

/**
 *	ata_scsiop_read_cap - Simulate READ CAPACITY[ 16] commands
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Simulate READ CAPACITY commands.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_read_cap(struct ata_scsi_args *args, u8 *rbuf,
			        unsigned int buflen)
{
	u64 n_sectors;
	u32 tmp;

	VPRINTK("ENTER\n");

	if (ata_id_has_lba48(args->id))
		n_sectors = ata_id_u64(args->id, 100);
	else
		n_sectors = ata_id_u32(args->id, 60);
	n_sectors--;		/* ATA TotalUserSectors - 1 */

	if (args->cmd->cmnd[0] == READ_CAPACITY) {
		if( n_sectors >= 0xffffffffULL )
			tmp = 0xffffffff ;  /* Return max count on overflow */
		else
			tmp = n_sectors ;

		/* sector count, 32-bit */
		rbuf[0] = tmp >> (8 * 3);
		rbuf[1] = tmp >> (8 * 2);
		rbuf[2] = tmp >> (8 * 1);
		rbuf[3] = tmp;

		/* sector size */
		tmp = ATA_SECT_SIZE;
		rbuf[6] = tmp >> 8;
		rbuf[7] = tmp;

	} else {
		/* sector count, 64-bit */
		tmp = n_sectors >> (8 * 4);
		rbuf[2] = tmp >> (8 * 3);
		rbuf[3] = tmp >> (8 * 2);
		rbuf[4] = tmp >> (8 * 1);
		rbuf[5] = tmp;
		tmp = n_sectors;
		rbuf[6] = tmp >> (8 * 3);
		rbuf[7] = tmp >> (8 * 2);
		rbuf[8] = tmp >> (8 * 1);
		rbuf[9] = tmp;

		/* sector size */
		tmp = ATA_SECT_SIZE;
		rbuf[12] = tmp >> 8;
		rbuf[13] = tmp;
	}

	return 0;
}

/**
 *	ata_scsiop_report_luns - Simulate REPORT LUNS command
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Simulate REPORT LUNS command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_report_luns(struct ata_scsi_args *args, u8 *rbuf,
				   unsigned int buflen)
{
	VPRINTK("ENTER\n");
	rbuf[3] = 8;	/* just one lun, LUN 0, size 8 bytes */

	return 0;
}

/**
 *	ata_scsi_badcmd - End a SCSI request with an error
 *	@cmd: SCSI request to be handled
 *	@done: SCSI command completion function
 *	@asc: SCSI-defined additional sense code
 *	@ascq: SCSI-defined additional sense code qualifier
 *
 *	Helper function that completes a SCSI command with
 *	%SAM_STAT_CHECK_CONDITION, with a sense key %ILLEGAL_REQUEST
 *	and the specified additional sense codes.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_scsi_badcmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *), u8 asc, u8 ascq)
{
	DPRINTK("ENTER\n");
	cmd->result = SAM_STAT_CHECK_CONDITION;

	cmd->sense_buffer[0] = 0x70;
	cmd->sense_buffer[2] = ILLEGAL_REQUEST;
	cmd->sense_buffer[7] = 14 - 8;	/* addnl. sense len. FIXME: correct? */
	cmd->sense_buffer[12] = asc;
	cmd->sense_buffer[13] = ascq;

	done(cmd);
}

static int atapi_qc_complete(struct ata_queued_cmd *qc, u8 drv_stat)
{
	struct scsi_cmnd *cmd = qc->scsicmd;

	if (unlikely(drv_stat & (ATA_ERR | ATA_BUSY | ATA_DRQ))) {
		DPRINTK("request check condition\n");

		cmd->result = SAM_STAT_CHECK_CONDITION;

		qc->scsidone(cmd);

		return 1;
	} else {
		u8 *scsicmd = cmd->cmnd;

		if (scsicmd[0] == INQUIRY) {
			u8 *buf = NULL;
			unsigned int buflen;

			buflen = ata_scsi_rbuf_get(cmd, &buf);
			buf[2] = 0x5;
			buf[3] = (buf[3] & 0xf0) | 2;
			ata_scsi_rbuf_put(cmd, buf);
		}
		cmd->result = SAM_STAT_GOOD;
	}

	qc->scsidone(cmd);

	return 0;
}
/**
 *	atapi_xlat - Initialize PACKET taskfile
 *	@qc: command structure to be initialized
 *	@scsicmd: SCSI CDB associated with this PACKET command
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on failure.
 */

static unsigned int atapi_xlat(struct ata_queued_cmd *qc, u8 *scsicmd)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct ata_device *dev = qc->dev;
	int using_pio = (dev->flags & ATA_DFLAG_PIO);
	int nodata = (cmd->sc_data_direction == DMA_NONE);

	if (!using_pio)
		/* Check whether ATAPI DMA is safe */
		if (ata_check_atapi_dma(qc))
			using_pio = 1;

	memcpy(&qc->cdb, scsicmd, qc->ap->cdb_len);

	qc->complete_fn = atapi_qc_complete;

	qc->tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	if (cmd->sc_data_direction == DMA_TO_DEVICE) {
		qc->tf.flags |= ATA_TFLAG_WRITE;
		DPRINTK("direction: write\n");
	}

	qc->tf.command = ATA_CMD_PACKET;

	/* no data, or PIO data xfer */
	if (using_pio || nodata) {
		if (nodata)
			qc->tf.protocol = ATA_PROT_ATAPI_NODATA;
		else
			qc->tf.protocol = ATA_PROT_ATAPI;
		qc->tf.lbam = (8 * 1024) & 0xff;
		qc->tf.lbah = (8 * 1024) >> 8;
	}

	/* DMA data xfer */
	else {
		qc->tf.protocol = ATA_PROT_ATAPI_DMA;
		qc->tf.feature |= ATAPI_PKT_DMA;

#ifdef ATAPI_ENABLE_DMADIR
		/* some SATA bridges need us to indicate data xfer direction */
		if (cmd->sc_data_direction != DMA_TO_DEVICE)
			qc->tf.feature |= ATAPI_DMADIR;
#endif
	}

	qc->nbytes = cmd->bufflen;

	return 0;
}

/**
 *	ata_scsi_find_dev - lookup ata_device from scsi_cmnd
 *	@ap: ATA port to which the device is attached
 *	@scsidev: SCSI device from which we derive the ATA device
 *
 *	Given various information provided in struct scsi_cmnd,
 *	map that onto an ATA bus, and using that mapping
 *	determine which ata_device is associated with the
 *	SCSI command to be sent.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Associated ATA device, or %NULL if not found.
 */

static struct ata_device *
ata_scsi_find_dev(struct ata_port *ap, struct scsi_device *scsidev)
{
	struct ata_device *dev;

	/* skip commands not addressed to targets we simulate */
	if (likely(scsidev->id < ATA_MAX_DEVICES))
		dev = &ap->device[scsidev->id];
	else
		return NULL;

	if (unlikely((scsidev->channel != 0) ||
		     (scsidev->lun != 0)))
		return NULL;

	if (unlikely(!ata_dev_present(dev)))
		return NULL;

	if (!atapi_enabled) {
		if (unlikely(dev->class == ATA_DEV_ATAPI))
			return NULL;
	}

	return dev;
}

/**
 *	ata_get_xlat_func - check if SCSI to ATA translation is possible
 *	@dev: ATA device
 *	@cmd: SCSI command opcode to consider
 *
 *	Look up the SCSI command given, and determine whether the
 *	SCSI command is to be translated or simulated.
 *
 *	RETURNS:
 *	Pointer to translation function if possible, %NULL if not.
 */

static inline ata_xlat_func_t ata_get_xlat_func(struct ata_device *dev, u8 cmd)
{
	switch (cmd) {
	case READ_6:
	case READ_10:
	case READ_16:

	case WRITE_6:
	case WRITE_10:
	case WRITE_16:
		return ata_scsi_rw_xlat;

	case SYNCHRONIZE_CACHE:
		if (ata_try_flush_cache(dev))
			return ata_scsi_flush_xlat;
		break;

	case VERIFY:
	case VERIFY_16:
		return ata_scsi_verify_xlat;
	case START_STOP:
		return ata_scsi_start_stop_xlat;
	}

	return NULL;
}

/**
 *	ata_scsi_dump_cdb - dump SCSI command contents to dmesg
 *	@ap: ATA port to which the command was being sent
 *	@cmd: SCSI command to dump
 *
 *	Prints the contents of a SCSI command via printk().
 */

static inline void ata_scsi_dump_cdb(struct ata_port *ap,
				     struct scsi_cmnd *cmd)
{
#ifdef ATA_DEBUG
	struct scsi_device *scsidev = cmd->device;
	u8 *scsicmd = cmd->cmnd;

	DPRINTK("CDB (%u:%d,%d,%d) %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		ap->id,
		scsidev->channel, scsidev->id, scsidev->lun,
		scsicmd[0], scsicmd[1], scsicmd[2], scsicmd[3],
		scsicmd[4], scsicmd[5], scsicmd[6], scsicmd[7],
		scsicmd[8]);
#endif
}

/**
 *	ata_scsi_queuecmd - Issue SCSI cdb to libata-managed device
 *	@cmd: SCSI command to be sent
 *	@done: Completion function, called when command is complete
 *
 *	In some cases, this function translates SCSI commands into
 *	ATA taskfiles, and queues the taskfiles to be sent to
 *	hardware.  In other cases, this function simulates a
 *	SCSI device by evaluating and responding to certain
 *	SCSI commands.  This creates the overall effect of
 *	ATA and ATAPI devices appearing as SCSI devices.
 *
 *	LOCKING:
 *	Releases scsi-layer-held lock, and obtains host_set lock.
 *
 *	RETURNS:
 *	Zero.
 */

int ata_scsi_queuecmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	struct ata_port *ap;
	struct ata_device *dev;
	struct scsi_device *scsidev = cmd->device;

	ap = (struct ata_port *) &scsidev->host->hostdata[0];

	ata_scsi_dump_cdb(ap, cmd);

	dev = ata_scsi_find_dev(ap, scsidev);
	if (unlikely(!dev)) {
		cmd->result = (DID_BAD_TARGET << 16);
		done(cmd);
		goto out_unlock;
	}

	if (dev->class == ATA_DEV_ATA) {
		ata_xlat_func_t xlat_func = ata_get_xlat_func(dev,
							      cmd->cmnd[0]);

		if (xlat_func)
			ata_scsi_translate(ap, dev, cmd, done, xlat_func);
		else
			ata_scsi_simulate(dev->id, cmd, done);
	} else
		ata_scsi_translate(ap, dev, cmd, done, atapi_xlat);

out_unlock:
	return 0;
}

/**
 *	ata_scsi_simulate - simulate SCSI command on ATA device
 *	@id: current IDENTIFY data for target device.
 *	@cmd: SCSI command being sent to device.
 *	@done: SCSI command completion function.
 *
 *	Interprets and directly executes a select list of SCSI commands
 *	that can be handled internally.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_scsi_simulate(u16 *id,
		      struct scsi_cmnd *cmd,
		      void (*done)(struct scsi_cmnd *))
{
	struct ata_scsi_args args;
	u8 *scsicmd = cmd->cmnd;

	args.id = id;
	args.cmd = cmd;
	args.done = done;

	switch(scsicmd[0]) {
		/* no-op's, complete with success */
		case SYNCHRONIZE_CACHE:
		case REZERO_UNIT:
		case SEEK_6:
		case SEEK_10:
		case TEST_UNIT_READY:
		case FORMAT_UNIT:		/* FIXME: correct? */
		case SEND_DIAGNOSTIC:		/* FIXME: correct? */
			ata_scsi_rbuf_fill(&args, ata_scsiop_noop);
			break;

		case INQUIRY:
			if (scsicmd[1] & 2)	           /* is CmdDt set?  */
				ata_bad_cdb(cmd, done);
			else if ((scsicmd[1] & 1) == 0)    /* is EVPD clear? */
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_std);
			else if (scsicmd[2] == 0x00)
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_00);
			else if (scsicmd[2] == 0x80)
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_80);
			else if (scsicmd[2] == 0x83)
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_83);
			else
				ata_bad_cdb(cmd, done);
			break;

		case MODE_SENSE:
		case MODE_SENSE_10:
			ata_scsi_rbuf_fill(&args, ata_scsiop_mode_sense);
			break;

		case MODE_SELECT:	/* unconditionally return */
		case MODE_SELECT_10:	/* bad-field-in-cdb */
			ata_bad_cdb(cmd, done);
			break;

		case READ_CAPACITY:
			ata_scsi_rbuf_fill(&args, ata_scsiop_read_cap);
			break;

		case SERVICE_ACTION_IN:
			if ((scsicmd[1] & 0x1f) == SAI_READ_CAPACITY_16)
				ata_scsi_rbuf_fill(&args, ata_scsiop_read_cap);
			else
				ata_bad_cdb(cmd, done);
			break;

		case REPORT_LUNS:
			ata_scsi_rbuf_fill(&args, ata_scsiop_report_luns);
			break;

		/* mandantory commands we haven't implemented yet */
		case REQUEST_SENSE:

		/* all other commands */
		default:
			ata_bad_scsiop(cmd, done);
			break;
	}
}

