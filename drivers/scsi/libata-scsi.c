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
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <linux/libata.h>
#include <linux/hdreg.h>
#include <asm/uaccess.h>

#include "libata.h"

#define SECTOR_SIZE	512

typedef unsigned int (*ata_xlat_func_t)(struct ata_queued_cmd *qc, const u8 *scsicmd);
static struct ata_device *
ata_scsi_find_dev(struct ata_port *ap, const struct scsi_device *scsidev);
static void ata_scsi_error(struct Scsi_Host *host);
enum scsi_eh_timer_return ata_scsi_timed_out(struct scsi_cmnd *cmd);

#define RW_RECOVERY_MPAGE 0x1
#define RW_RECOVERY_MPAGE_LEN 12
#define CACHE_MPAGE 0x8
#define CACHE_MPAGE_LEN 20
#define CONTROL_MPAGE 0xa
#define CONTROL_MPAGE_LEN 12
#define ALL_MPAGES 0x3f
#define ALL_SUB_MPAGES 0xff


static const u8 def_rw_recovery_mpage[] = {
	RW_RECOVERY_MPAGE,
	RW_RECOVERY_MPAGE_LEN - 2,
	(1 << 7) |	/* AWRE, sat-r06 say it shall be 0 */
	    (1 << 6),	/* ARRE (auto read reallocation) */
	0,		/* read retry count */
	0, 0, 0, 0,
	0,		/* write retry count */
	0, 0, 0
};

static const u8 def_cache_mpage[CACHE_MPAGE_LEN] = {
	CACHE_MPAGE,
	CACHE_MPAGE_LEN - 2,
	0,		/* contains WCE, needs to be 0 for logic */
	0, 0, 0, 0, 0, 0, 0, 0, 0,
	0,		/* contains DRA, needs to be 0 for logic */
	0, 0, 0, 0, 0, 0, 0
};

static const u8 def_control_mpage[CONTROL_MPAGE_LEN] = {
	CONTROL_MPAGE,
	CONTROL_MPAGE_LEN - 2,
	2,	/* DSENSE=0, GLTSD=1 */
	0,	/* [QAM+QERR may be 1, see 05-359r1] */
	0, 0, 0, 0, 0xff, 0xff,
	0, 30	/* extended self test time, see 05-359r1 */
};

/*
 * libata transport template.  libata doesn't do real transport stuff.
 * It just needs the eh_timed_out hook.
 */
struct scsi_transport_template ata_scsi_transport_template = {
	.eh_strategy_handler	= ata_scsi_error,
	.eh_timed_out		= ata_scsi_timed_out,
};


static void ata_scsi_invalid_field(struct scsi_cmnd *cmd,
				   void (*done)(struct scsi_cmnd *))
{
	ata_scsi_set_sense(cmd, ILLEGAL_REQUEST, 0x24, 0x0);
	/* "Invalid field in cbd" */
	done(cmd);
}

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

/**
 *	ata_cmd_ioctl - Handler for HDIO_DRIVE_CMD ioctl
 *	@scsidev: Device to which we are issuing command
 *	@arg: User provided data for issuing command
 *
 *	LOCKING:
 *	Defined by the SCSI layer.  We don't really care.
 *
 *	RETURNS:
 *	Zero on success, negative errno on error.
 */

int ata_cmd_ioctl(struct scsi_device *scsidev, void __user *arg)
{
	int rc = 0;
	u8 scsi_cmd[MAX_COMMAND_SIZE];
	u8 args[4], *argbuf = NULL;
	int argsize = 0;
	struct scsi_sense_hdr sshdr;
	enum dma_data_direction data_dir;

	if (arg == NULL)
		return -EINVAL;

	if (copy_from_user(args, arg, sizeof(args)))
		return -EFAULT;

	memset(scsi_cmd, 0, sizeof(scsi_cmd));

	if (args[3]) {
		argsize = SECTOR_SIZE * args[3];
		argbuf = kmalloc(argsize, GFP_KERNEL);
		if (argbuf == NULL) {
			rc = -ENOMEM;
			goto error;
		}

		scsi_cmd[1]  = (4 << 1); /* PIO Data-in */
		scsi_cmd[2]  = 0x0e;     /* no off.line or cc, read from dev,
		                            block count in sector count field */
		data_dir = DMA_FROM_DEVICE;
	} else {
		scsi_cmd[1]  = (3 << 1); /* Non-data */
		/* scsi_cmd[2] is already 0 -- no off.line, cc, or data xfer */
		data_dir = DMA_NONE;
	}

	scsi_cmd[0] = ATA_16;

	scsi_cmd[4] = args[2];
	if (args[0] == WIN_SMART) { /* hack -- ide driver does this too... */
		scsi_cmd[6]  = args[3];
		scsi_cmd[8]  = args[1];
		scsi_cmd[10] = 0x4f;
		scsi_cmd[12] = 0xc2;
	} else {
		scsi_cmd[6]  = args[1];
	}
	scsi_cmd[14] = args[0];

	/* Good values for timeout and retries?  Values below
	   from scsi_ioctl_send_command() for default case... */
	if (scsi_execute_req(scsidev, scsi_cmd, data_dir, argbuf, argsize,
			     &sshdr, (10*HZ), 5)) {
		rc = -EIO;
		goto error;
	}

	/* Need code to retrieve data from check condition? */

	if ((argbuf)
	 && copy_to_user(arg + sizeof(args), argbuf, argsize))
		rc = -EFAULT;
error:
	if (argbuf)
		kfree(argbuf);

	return rc;
}

/**
 *	ata_task_ioctl - Handler for HDIO_DRIVE_TASK ioctl
 *	@scsidev: Device to which we are issuing command
 *	@arg: User provided data for issuing command
 *
 *	LOCKING:
 *	Defined by the SCSI layer.  We don't really care.
 *
 *	RETURNS:
 *	Zero on success, negative errno on error.
 */
int ata_task_ioctl(struct scsi_device *scsidev, void __user *arg)
{
	int rc = 0;
	u8 scsi_cmd[MAX_COMMAND_SIZE];
	u8 args[7];
	struct scsi_sense_hdr sshdr;

	if (arg == NULL)
		return -EINVAL;

	if (copy_from_user(args, arg, sizeof(args)))
		return -EFAULT;

	memset(scsi_cmd, 0, sizeof(scsi_cmd));
	scsi_cmd[0]  = ATA_16;
	scsi_cmd[1]  = (3 << 1); /* Non-data */
	/* scsi_cmd[2] is already 0 -- no off.line, cc, or data xfer */
	scsi_cmd[4]  = args[1];
	scsi_cmd[6]  = args[2];
	scsi_cmd[8]  = args[3];
	scsi_cmd[10] = args[4];
	scsi_cmd[12] = args[5];
	scsi_cmd[14] = args[0];

	/* Good values for timeout and retries?  Values below
	   from scsi_ioctl_send_command() for default case... */
	if (scsi_execute_req(scsidev, scsi_cmd, DMA_NONE, NULL, 0, &sshdr,
			     (10*HZ), 5))
		rc = -EIO;

	/* Need code to retrieve data from check condition? */
	return rc;
}

int ata_scsi_ioctl(struct scsi_device *scsidev, int cmd, void __user *arg)
{
	int val = -EINVAL, rc = -EINVAL;

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

	case HDIO_DRIVE_CMD:
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			return -EACCES;
		return ata_cmd_ioctl(scsidev, arg);

	case HDIO_DRIVE_TASK:
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			return -EACCES;
		return ata_task_ioctl(scsidev, arg);

	default:
		rc = -ENOTTY;
		break;
	}

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
			qc->__sg = (struct scatterlist *) cmd->request_buffer;
			qc->n_elem = cmd->use_sg;
		} else {
			qc->__sg = &qc->sgent;
			qc->n_elem = 1;
		}
	} else {
		cmd->result = (DID_OK << 16) | (QUEUE_FULL << 1);
		done(cmd);
	}

	return qc;
}

/**
 *	ata_dump_status - user friendly display of error info
 *	@id: id of the port in question
 *	@tf: ptr to filled out taskfile
 *
 *	Decode and dump the ATA error/status registers for the user so
 *	that they have some idea what really happened at the non
 *	make-believe layer.
 *
 *	LOCKING:
 *	inherited from caller
 */
void ata_dump_status(unsigned id, struct ata_taskfile *tf)
{
	u8 stat = tf->command, err = tf->feature;

	printk(KERN_WARNING "ata%u: status=0x%02x { ", id, stat);
	if (stat & ATA_BUSY) {
		printk("Busy }\n");	/* Data is not valid in this case */
	} else {
		if (stat & 0x40)	printk("DriveReady ");
		if (stat & 0x20)	printk("DeviceFault ");
		if (stat & 0x10)	printk("SeekComplete ");
		if (stat & 0x08)	printk("DataRequest ");
		if (stat & 0x04)	printk("CorrectedError ");
		if (stat & 0x02)	printk("Index ");
		if (stat & 0x01)	printk("Error ");
		printk("}\n");

		if (err) {
			printk(KERN_WARNING "ata%u: error=0x%02x { ", id, err);
			if (err & 0x04)		printk("DriveStatusError ");
			if (err & 0x80) {
				if (err & 0x04)	printk("BadCRC ");
				else		printk("Sector ");
			}
			if (err & 0x40)		printk("UncorrectableError ");
			if (err & 0x10)		printk("SectorIdNotFound ");
			if (err & 0x02)		printk("TrackZeroNotFound ");
			if (err & 0x01)		printk("AddrMarkNotFound ");
			printk("}\n");
		}
	}
}

int ata_scsi_device_resume(struct scsi_device *sdev)
{
	struct ata_port *ap = (struct ata_port *) &sdev->host->hostdata[0];
	struct ata_device *dev = &ap->device[sdev->id];

	return ata_device_resume(ap, dev);
}

int ata_scsi_device_suspend(struct scsi_device *sdev, pm_message_t state)
{
	struct ata_port *ap = (struct ata_port *) &sdev->host->hostdata[0];
	struct ata_device *dev = &ap->device[sdev->id];

	return ata_device_suspend(ap, dev, state);
}

/**
 *	ata_to_sense_error - convert ATA error to SCSI error
 *	@id: ATA device number
 *	@drv_stat: value contained in ATA status register
 *	@drv_err: value contained in ATA error register
 *	@sk: the sense key we'll fill out
 *	@asc: the additional sense code we'll fill out
 *	@ascq: the additional sense code qualifier we'll fill out
 *
 *	Converts an ATA error into a SCSI error.  Fill out pointers to
 *	SK, ASC, and ASCQ bytes for later use in fixed or descriptor
 *	format sense blocks.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_to_sense_error(unsigned id, u8 drv_stat, u8 drv_err, u8 *sk, u8 *asc,
			u8 *ascq)
{
	int i;

	/* Based on the 3ware driver translation table */
	static const unsigned char sense_table[][4] = {
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
	static const unsigned char stat_table[][4] = {
		/* Must be first because BUSY means no other bits valid */
		{0x80, 		ABORTED_COMMAND, 0x47, 0x00},	// Busy, fake parity for now
		{0x20, 		HARDWARE_ERROR,  0x00, 0x00}, 	// Device fault
		{0x08, 		ABORTED_COMMAND, 0x47, 0x00},	// Timed out in xfer, fake parity for now
		{0x04, 		RECOVERED_ERROR, 0x11, 0x00},	// Recovered ECC error	  Medium error, recovered
		{0xFF, 0xFF, 0xFF, 0xFF}, // END mark
	};

	/*
	 *	Is this an error we can process/parse
	 */
	if (drv_stat & ATA_BUSY) {
		drv_err = 0;	/* Ignore the err bits, they're invalid */
	}

	if (drv_err) {
		/* Look for drv_err */
		for (i = 0; sense_table[i][0] != 0xFF; i++) {
			/* Look for best matches first */
			if ((sense_table[i][0] & drv_err) ==
			    sense_table[i][0]) {
				*sk = sense_table[i][1];
				*asc = sense_table[i][2];
				*ascq = sense_table[i][3];
				goto translate_done;
			}
		}
		/* No immediate match */
		printk(KERN_WARNING "ata%u: no sense translation for "
		       "error 0x%02x\n", id, drv_err);
	}

	/* Fall back to interpreting status bits */
	for (i = 0; stat_table[i][0] != 0xFF; i++) {
		if (stat_table[i][0] & drv_stat) {
			*sk = stat_table[i][1];
			*asc = stat_table[i][2];
			*ascq = stat_table[i][3];
			goto translate_done;
		}
	}
	/* No error?  Undecoded? */
	printk(KERN_WARNING "ata%u: no sense translation for status: 0x%02x\n",
	       id, drv_stat);

	/* We need a sensible error return here, which is tricky, and one
	   that won't cause people to do things like return a disk wrongly */
	*sk = ABORTED_COMMAND;
	*asc = 0x00;
	*ascq = 0x00;

 translate_done:
	printk(KERN_ERR "ata%u: translated ATA stat/err 0x%02x/%02x to "
	       "SCSI SK/ASC/ASCQ 0x%x/%02x/%02x\n", id, drv_stat, drv_err,
	       *sk, *asc, *ascq);
	return;
}

/*
 *	ata_gen_ata_desc_sense - Generate check condition sense block.
 *	@qc: Command that completed.
 *
 *	This function is specific to the ATA descriptor format sense
 *	block specified for the ATA pass through commands.  Regardless
 *	of whether the command errored or not, return a sense
 *	block. Copy all controller registers into the sense
 *	block. Clear sense key, ASC & ASCQ if there is no error.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_gen_ata_desc_sense(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct ata_taskfile *tf = &qc->tf;
	unsigned char *sb = cmd->sense_buffer;
	unsigned char *desc = sb + 8;

	memset(sb, 0, SCSI_SENSE_BUFFERSIZE);

	cmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

	/*
	 * Read the controller registers.
	 */
	WARN_ON(qc->ap->ops->tf_read == NULL);
	qc->ap->ops->tf_read(qc->ap, tf);

	/*
	 * Use ata_to_sense_error() to map status register bits
	 * onto sense key, asc & ascq.
	 */
	if (tf->command & (ATA_BUSY | ATA_DF | ATA_ERR | ATA_DRQ)) {
		ata_to_sense_error(qc->ap->id, tf->command, tf->feature,
				   &sb[1], &sb[2], &sb[3]);
		sb[1] &= 0x0f;
	}

	/*
	 * Sense data is current and format is descriptor.
	 */
	sb[0] = 0x72;

	desc[0] = 0x09;

	/*
	 * Set length of additional sense data.
	 * Since we only populate descriptor 0, the total
	 * length is the same (fixed) length as descriptor 0.
	 */
	desc[1] = sb[7] = 14;

	/*
	 * Copy registers into sense buffer.
	 */
	desc[2] = 0x00;
	desc[3] = tf->feature;	/* == error reg */
	desc[5] = tf->nsect;
	desc[7] = tf->lbal;
	desc[9] = tf->lbam;
	desc[11] = tf->lbah;
	desc[12] = tf->device;
	desc[13] = tf->command; /* == status reg */

	/*
	 * Fill in Extend bit, and the high order bytes
	 * if applicable.
	 */
	if (tf->flags & ATA_TFLAG_LBA48) {
		desc[2] |= 0x01;
		desc[4] = tf->hob_nsect;
		desc[6] = tf->hob_lbal;
		desc[8] = tf->hob_lbam;
		desc[10] = tf->hob_lbah;
	}
}

/**
 *	ata_gen_fixed_sense - generate a SCSI fixed sense block
 *	@qc: Command that we are erroring out
 *
 *	Leverage ata_to_sense_error() to give us the codes.  Fit our
 *	LBA in here if there's room.
 *
 *	LOCKING:
 *	inherited from caller
 */
void ata_gen_fixed_sense(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct ata_taskfile *tf = &qc->tf;
	unsigned char *sb = cmd->sense_buffer;

	memset(sb, 0, SCSI_SENSE_BUFFERSIZE);

	cmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

	/*
	 * Read the controller registers.
	 */
	WARN_ON(qc->ap->ops->tf_read == NULL);
	qc->ap->ops->tf_read(qc->ap, tf);

	/*
	 * Use ata_to_sense_error() to map status register bits
	 * onto sense key, asc & ascq.
	 */
	if (tf->command & (ATA_BUSY | ATA_DF | ATA_ERR | ATA_DRQ)) {
		ata_to_sense_error(qc->ap->id, tf->command, tf->feature,
				   &sb[2], &sb[12], &sb[13]);
		sb[2] &= 0x0f;
	}

	sb[0] = 0x70;
	sb[7] = 0x0a;

	if (tf->flags & ATA_TFLAG_LBA48) {
		/* TODO: find solution for LBA48 descriptors */
	}

	else if (tf->flags & ATA_TFLAG_LBA) {
		/* A small (28b) LBA will fit in the 32b info field */
		sb[0] |= 0x80;		/* set valid bit */
		sb[3] = tf->device & 0x0f;
		sb[4] = tf->lbah;
		sb[5] = tf->lbam;
		sb[6] = tf->lbal;
	}

	else {
		/* TODO: C/H/S */
	}
}

static void ata_scsi_sdev_config(struct scsi_device *sdev)
{
	sdev->use_10_for_rw = 1;
	sdev->use_10_for_ms = 1;
}

static void ata_scsi_dev_config(struct scsi_device *sdev,
				struct ata_device *dev)
{
	unsigned int max_sectors;

	/* TODO: 2048 is an arbitrary number, not the
	 * hardware maximum.  This should be increased to
	 * 65534 when Jens Axboe's patch for dynamically
	 * determining max_sectors is merged.
	 */
	max_sectors = ATA_MAX_SECTORS;
	if (dev->flags & ATA_DFLAG_LBA48)
		max_sectors = 2048;
	if (dev->max_sectors)
		max_sectors = dev->max_sectors;

	blk_queue_max_sectors(sdev->request_queue, max_sectors);

	/*
	 * SATA DMA transfers must be multiples of 4 byte, so
	 * we need to pad ATAPI transfers using an extra sg.
	 * Decrement max hw segments accordingly.
	 */
	if (dev->class == ATA_DEV_ATAPI) {
		request_queue_t *q = sdev->request_queue;
		blk_queue_max_hw_segments(q, q->max_hw_segments - 1);
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
	ata_scsi_sdev_config(sdev);

	blk_queue_max_phys_segments(sdev->request_queue, LIBATA_MAX_PRD);

	if (sdev->id < ATA_MAX_DEVICES) {
		struct ata_port *ap;
		struct ata_device *dev;

		ap = (struct ata_port *) &sdev->host->hostdata[0];
		dev = &ap->device[sdev->id];

		ata_scsi_dev_config(sdev, dev);
	}

	return 0;	/* scsi layer doesn't check return value, sigh */
}

/**
 *	ata_scsi_timed_out - SCSI layer time out callback
 *	@cmd: timed out SCSI command
 *
 *	Handles SCSI layer timeout.  We race with normal completion of
 *	the qc for @cmd.  If the qc is already gone, we lose and let
 *	the scsi command finish (EH_HANDLED).  Otherwise, the qc has
 *	timed out and EH should be invoked.  Prevent ata_qc_complete()
 *	from finishing it by setting EH_SCHEDULED and return
 *	EH_NOT_HANDLED.
 *
 *	LOCKING:
 *	Called from timer context
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
enum scsi_eh_timer_return ata_scsi_timed_out(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct ata_port *ap = (struct ata_port *) &host->hostdata[0];
	unsigned long flags;
	struct ata_queued_cmd *qc;
	enum scsi_eh_timer_return ret = EH_HANDLED;

	DPRINTK("ENTER\n");

	spin_lock_irqsave(&ap->host_set->lock, flags);
	qc = ata_qc_from_tag(ap, ap->active_tag);
	if (qc) {
		WARN_ON(qc->scsicmd != cmd);
		qc->flags |= ATA_QCFLAG_EH_SCHEDULED;
		qc->err_mask |= AC_ERR_TIMEOUT;
		ret = EH_NOT_HANDLED;
	}
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	DPRINTK("EXIT, ret=%d\n", ret);
	return ret;
}

/**
 *	ata_scsi_error - SCSI layer error handler callback
 *	@host: SCSI host on which error occurred
 *
 *	Handles SCSI-layer-thrown error events.
 *
 *	LOCKING:
 *	Inherited from SCSI layer (none, can sleep)
 */

static void ata_scsi_error(struct Scsi_Host *host)
{
	struct ata_port *ap;
	unsigned long flags;

	DPRINTK("ENTER\n");

	ap = (struct ata_port *) &host->hostdata[0];

	spin_lock_irqsave(&ap->host_set->lock, flags);
	WARN_ON(ap->flags & ATA_FLAG_IN_EH);
	ap->flags |= ATA_FLAG_IN_EH;
	WARN_ON(ata_qc_from_tag(ap, ap->active_tag) == NULL);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	ata_port_flush_task(ap);

	ap->ops->eng_timeout(ap);

	WARN_ON(host->host_failed || !list_empty(&host->eh_cmd_q));

	scsi_eh_flush_done_q(&ap->eh_done_q);

	spin_lock_irqsave(&ap->host_set->lock, flags);
	ap->flags &= ~ATA_FLAG_IN_EH;
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	DPRINTK("EXIT\n");
}

static void ata_eh_scsidone(struct scsi_cmnd *scmd)
{
	/* nada */
}

static void __ata_eh_qc_complete(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scsi_cmnd *scmd = qc->scsicmd;
	unsigned long flags;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	qc->scsidone = ata_eh_scsidone;
	__ata_qc_complete(qc);
	WARN_ON(ata_tag_valid(qc->tag));
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	scsi_eh_finish_cmd(scmd, &ap->eh_done_q);
}

/**
 *	ata_eh_qc_complete - Complete an active ATA command from EH
 *	@qc: Command to complete
 *
 *	Indicate to the mid and upper layers that an ATA command has
 *	completed.  To be used from EH.
 */
void ata_eh_qc_complete(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	scmd->retries = scmd->allowed;
	__ata_eh_qc_complete(qc);
}

/**
 *	ata_eh_qc_retry - Tell midlayer to retry an ATA command after EH
 *	@qc: Command to retry
 *
 *	Indicate to the mid and upper layers that an ATA command
 *	should be retried.  To be used from EH.
 *
 *	SCSI midlayer limits the number of retries to scmd->allowed.
 *	This function might need to adjust scmd->retries for commands
 *	which get retried due to unrelated NCQ failures.
 */
void ata_eh_qc_retry(struct ata_queued_cmd *qc)
{
	__ata_eh_qc_complete(qc);
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
					     const u8 *scsicmd)
{
	struct ata_taskfile *tf = &qc->tf;

	tf->flags |= ATA_TFLAG_DEVICE | ATA_TFLAG_ISADDR;
	tf->protocol = ATA_PROT_NODATA;
	if (scsicmd[1] & 0x1) {
		;	/* ignore IMMED bit, violates sat-r05 */
	}
	if (scsicmd[4] & 0x2)
		goto invalid_fld;       /* LOEJ bit set not supported */
	if (((scsicmd[4] >> 4) & 0xf) != 0)
		goto invalid_fld;       /* power conditions not supported */
	if (scsicmd[4] & 0x1) {
		tf->nsect = 1;	/* 1 sector, lba=0 */

		if (qc->dev->flags & ATA_DFLAG_LBA) {
			qc->tf.flags |= ATA_TFLAG_LBA;

			tf->lbah = 0x0;
			tf->lbam = 0x0;
			tf->lbal = 0x0;
			tf->device |= ATA_LBA;
		} else {
			/* CHS */
			tf->lbal = 0x1; /* sect */
			tf->lbam = 0x0; /* cyl low */
			tf->lbah = 0x0; /* cyl high */
		}

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

invalid_fld:
	ata_scsi_set_sense(qc->scsicmd, ILLEGAL_REQUEST, 0x24, 0x0);
	/* "Invalid field in cbd" */
	return 1;
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

static unsigned int ata_scsi_flush_xlat(struct ata_queued_cmd *qc, const u8 *scsicmd)
{
	struct ata_taskfile *tf = &qc->tf;

	tf->flags |= ATA_TFLAG_DEVICE;
	tf->protocol = ATA_PROT_NODATA;

	if ((qc->dev->flags & ATA_DFLAG_LBA48) &&
	    (ata_id_has_flush_ext(qc->dev->id)))
		tf->command = ATA_CMD_FLUSH_EXT;
	else
		tf->command = ATA_CMD_FLUSH;

	return 0;
}

/**
 *	scsi_6_lba_len - Get LBA and transfer length
 *	@scsicmd: SCSI command to translate
 *
 *	Calculate LBA and transfer length for 6-byte commands.
 *
 *	RETURNS:
 *	@plba: the LBA
 *	@plen: the transfer length
 */

static void scsi_6_lba_len(const u8 *scsicmd, u64 *plba, u32 *plen)
{
	u64 lba = 0;
	u32 len = 0;

	VPRINTK("six-byte command\n");

	lba |= ((u64)scsicmd[2]) << 8;
	lba |= ((u64)scsicmd[3]);

	len |= ((u32)scsicmd[4]);

	*plba = lba;
	*plen = len;
}

/**
 *	scsi_10_lba_len - Get LBA and transfer length
 *	@scsicmd: SCSI command to translate
 *
 *	Calculate LBA and transfer length for 10-byte commands.
 *
 *	RETURNS:
 *	@plba: the LBA
 *	@plen: the transfer length
 */

static void scsi_10_lba_len(const u8 *scsicmd, u64 *plba, u32 *plen)
{
	u64 lba = 0;
	u32 len = 0;

	VPRINTK("ten-byte command\n");

	lba |= ((u64)scsicmd[2]) << 24;
	lba |= ((u64)scsicmd[3]) << 16;
	lba |= ((u64)scsicmd[4]) << 8;
	lba |= ((u64)scsicmd[5]);

	len |= ((u32)scsicmd[7]) << 8;
	len |= ((u32)scsicmd[8]);

	*plba = lba;
	*plen = len;
}

/**
 *	scsi_16_lba_len - Get LBA and transfer length
 *	@scsicmd: SCSI command to translate
 *
 *	Calculate LBA and transfer length for 16-byte commands.
 *
 *	RETURNS:
 *	@plba: the LBA
 *	@plen: the transfer length
 */

static void scsi_16_lba_len(const u8 *scsicmd, u64 *plba, u32 *plen)
{
	u64 lba = 0;
	u32 len = 0;

	VPRINTK("sixteen-byte command\n");

	lba |= ((u64)scsicmd[2]) << 56;
	lba |= ((u64)scsicmd[3]) << 48;
	lba |= ((u64)scsicmd[4]) << 40;
	lba |= ((u64)scsicmd[5]) << 32;
	lba |= ((u64)scsicmd[6]) << 24;
	lba |= ((u64)scsicmd[7]) << 16;
	lba |= ((u64)scsicmd[8]) << 8;
	lba |= ((u64)scsicmd[9]);

	len |= ((u32)scsicmd[10]) << 24;
	len |= ((u32)scsicmd[11]) << 16;
	len |= ((u32)scsicmd[12]) << 8;
	len |= ((u32)scsicmd[13]);

	*plba = lba;
	*plen = len;
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

static unsigned int ata_scsi_verify_xlat(struct ata_queued_cmd *qc, const u8 *scsicmd)
{
	struct ata_taskfile *tf = &qc->tf;
	struct ata_device *dev = qc->dev;
	u64 dev_sectors = qc->dev->n_sectors;
	u64 block;
	u32 n_block;

	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf->protocol = ATA_PROT_NODATA;

	if (scsicmd[0] == VERIFY)
		scsi_10_lba_len(scsicmd, &block, &n_block);
	else if (scsicmd[0] == VERIFY_16)
		scsi_16_lba_len(scsicmd, &block, &n_block);
	else
		goto invalid_fld;

	if (!n_block)
		goto nothing_to_do;
	if (block >= dev_sectors)
		goto out_of_range;
	if ((block + n_block) > dev_sectors)
		goto out_of_range;

	if (dev->flags & ATA_DFLAG_LBA) {
		tf->flags |= ATA_TFLAG_LBA;

		if (lba_28_ok(block, n_block)) {
			/* use LBA28 */
			tf->command = ATA_CMD_VERIFY;
			tf->device |= (block >> 24) & 0xf;
		} else if (lba_48_ok(block, n_block)) {
			if (!(dev->flags & ATA_DFLAG_LBA48))
				goto out_of_range;

			/* use LBA48 */
			tf->flags |= ATA_TFLAG_LBA48;
			tf->command = ATA_CMD_VERIFY_EXT;

			tf->hob_nsect = (n_block >> 8) & 0xff;

			tf->hob_lbah = (block >> 40) & 0xff;
			tf->hob_lbam = (block >> 32) & 0xff;
			tf->hob_lbal = (block >> 24) & 0xff;
		} else
			/* request too large even for LBA48 */
			goto out_of_range;

		tf->nsect = n_block & 0xff;

		tf->lbah = (block >> 16) & 0xff;
		tf->lbam = (block >> 8) & 0xff;
		tf->lbal = block & 0xff;

		tf->device |= ATA_LBA;
	} else {
		/* CHS */
		u32 sect, head, cyl, track;

		if (!lba_28_ok(block, n_block))
			goto out_of_range;

		/* Convert LBA to CHS */
		track = (u32)block / dev->sectors;
		cyl   = track / dev->heads;
		head  = track % dev->heads;
		sect  = (u32)block % dev->sectors + 1;

		DPRINTK("block %u track %u cyl %u head %u sect %u\n",
			(u32)block, track, cyl, head, sect);

		/* Check whether the converted CHS can fit.
		   Cylinder: 0-65535
		   Head: 0-15
		   Sector: 1-255*/
		if ((cyl >> 16) || (head >> 4) || (sect >> 8) || (!sect))
			goto out_of_range;

		tf->command = ATA_CMD_VERIFY;
		tf->nsect = n_block & 0xff; /* Sector count 0 means 256 sectors */
		tf->lbal = sect;
		tf->lbam = cyl;
		tf->lbah = cyl >> 8;
		tf->device |= head;
	}

	return 0;

invalid_fld:
	ata_scsi_set_sense(qc->scsicmd, ILLEGAL_REQUEST, 0x24, 0x0);
	/* "Invalid field in cbd" */
	return 1;

out_of_range:
	ata_scsi_set_sense(qc->scsicmd, ILLEGAL_REQUEST, 0x21, 0x0);
	/* "Logical Block Address out of range" */
	return 1;

nothing_to_do:
	qc->scsicmd->result = SAM_STAT_GOOD;
	return 1;
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

static unsigned int ata_scsi_rw_xlat(struct ata_queued_cmd *qc, const u8 *scsicmd)
{
	struct ata_taskfile *tf = &qc->tf;
	struct ata_device *dev = qc->dev;
	u64 block;
	u32 n_block;

	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;

	if (scsicmd[0] == WRITE_10 || scsicmd[0] == WRITE_6 ||
	    scsicmd[0] == WRITE_16)
		tf->flags |= ATA_TFLAG_WRITE;

	/* Calculate the SCSI LBA, transfer length and FUA. */
	switch (scsicmd[0]) {
	case READ_10:
	case WRITE_10:
		scsi_10_lba_len(scsicmd, &block, &n_block);
		if (unlikely(scsicmd[1] & (1 << 3)))
			tf->flags |= ATA_TFLAG_FUA;
		break;
	case READ_6:
	case WRITE_6:
		scsi_6_lba_len(scsicmd, &block, &n_block);

		/* for 6-byte r/w commands, transfer length 0
		 * means 256 blocks of data, not 0 block.
		 */
		if (!n_block)
			n_block = 256;
		break;
	case READ_16:
	case WRITE_16:
		scsi_16_lba_len(scsicmd, &block, &n_block);
		if (unlikely(scsicmd[1] & (1 << 3)))
			tf->flags |= ATA_TFLAG_FUA;
		break;
	default:
		DPRINTK("no-byte command\n");
		goto invalid_fld;
	}

	/* Check and compose ATA command */
	if (!n_block)
		/* For 10-byte and 16-byte SCSI R/W commands, transfer
		 * length 0 means transfer 0 block of data.
		 * However, for ATA R/W commands, sector count 0 means
		 * 256 or 65536 sectors, not 0 sectors as in SCSI.
		 *
		 * WARNING: one or two older ATA drives treat 0 as 0...
		 */
		goto nothing_to_do;

	if (dev->flags & ATA_DFLAG_LBA) {
		tf->flags |= ATA_TFLAG_LBA;

		if (lba_28_ok(block, n_block)) {
			/* use LBA28 */
			tf->device |= (block >> 24) & 0xf;
		} else if (lba_48_ok(block, n_block)) {
			if (!(dev->flags & ATA_DFLAG_LBA48))
				goto out_of_range;

			/* use LBA48 */
			tf->flags |= ATA_TFLAG_LBA48;

			tf->hob_nsect = (n_block >> 8) & 0xff;

			tf->hob_lbah = (block >> 40) & 0xff;
			tf->hob_lbam = (block >> 32) & 0xff;
			tf->hob_lbal = (block >> 24) & 0xff;
		} else
			/* request too large even for LBA48 */
			goto out_of_range;

		if (unlikely(ata_rwcmd_protocol(qc) < 0))
			goto invalid_fld;

		qc->nsect = n_block;
		tf->nsect = n_block & 0xff;

		tf->lbah = (block >> 16) & 0xff;
		tf->lbam = (block >> 8) & 0xff;
		tf->lbal = block & 0xff;

		tf->device |= ATA_LBA;
	} else {
		/* CHS */
		u32 sect, head, cyl, track;

		/* The request -may- be too large for CHS addressing. */
		if (!lba_28_ok(block, n_block))
			goto out_of_range;

		if (unlikely(ata_rwcmd_protocol(qc) < 0))
			goto invalid_fld;

		/* Convert LBA to CHS */
		track = (u32)block / dev->sectors;
		cyl   = track / dev->heads;
		head  = track % dev->heads;
		sect  = (u32)block % dev->sectors + 1;

		DPRINTK("block %u track %u cyl %u head %u sect %u\n",
			(u32)block, track, cyl, head, sect);

		/* Check whether the converted CHS can fit.
		   Cylinder: 0-65535
		   Head: 0-15
		   Sector: 1-255*/
		if ((cyl >> 16) || (head >> 4) || (sect >> 8) || (!sect))
			goto out_of_range;

		qc->nsect = n_block;
		tf->nsect = n_block & 0xff; /* Sector count 0 means 256 sectors */
		tf->lbal = sect;
		tf->lbam = cyl;
		tf->lbah = cyl >> 8;
		tf->device |= head;
	}

	return 0;

invalid_fld:
	ata_scsi_set_sense(qc->scsicmd, ILLEGAL_REQUEST, 0x24, 0x0);
	/* "Invalid field in cbd" */
	return 1;

out_of_range:
	ata_scsi_set_sense(qc->scsicmd, ILLEGAL_REQUEST, 0x21, 0x0);
	/* "Logical Block Address out of range" */
	return 1;

nothing_to_do:
	qc->scsicmd->result = SAM_STAT_GOOD;
	return 1;
}

static void ata_scsi_qc_complete(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	u8 *cdb = cmd->cmnd;
 	int need_sense = (qc->err_mask != 0);

	/* For ATA pass thru (SAT) commands, generate a sense block if
	 * user mandated it or if there's an error.  Note that if we
	 * generate because the user forced us to, a check condition
	 * is generated and the ATA register values are returned
	 * whether the command completed successfully or not. If there
	 * was no error, SK, ASC and ASCQ will all be zero.
	 */
	if (((cdb[0] == ATA_16) || (cdb[0] == ATA_12)) &&
 	    ((cdb[2] & 0x20) || need_sense)) {
 		ata_gen_ata_desc_sense(qc);
	} else {
		if (!need_sense) {
			cmd->result = SAM_STAT_GOOD;
		} else {
			/* TODO: decide which descriptor format to use
			 * for 48b LBA devices and call that here
			 * instead of the fixed desc, which is only
			 * good for smaller LBA (and maybe CHS?)
			 * devices.
			 */
			ata_gen_fixed_sense(qc);
		}
	}

	if (need_sense) {
		/* The ata_gen_..._sense routines fill in tf */
		ata_dump_status(qc->ap->id, &qc->tf);
	}

	qc->scsidone(cmd);

	ata_qc_free(qc);
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
 *	The xlat_func argument (actor) returns 0 if ready to execute
 *	ATA command, else 1 to finish translation. If 1 is returned
 *	then cmd->result (and possibly cmd->sense_buffer) are assumed
 *	to be set reflecting an error condition or clean (early)
 *	termination.
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
		goto err_mem;

	/* data is present; dma-map it */
	if (cmd->sc_data_direction == DMA_FROM_DEVICE ||
	    cmd->sc_data_direction == DMA_TO_DEVICE) {
		if (unlikely(cmd->request_bufflen < 1)) {
			printk(KERN_WARNING "ata%u(%u): WARNING: zero len r/w req\n",
			       ap->id, dev->devno);
			goto err_did;
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
		goto early_finish;

	/* select device, send command to hardware */
	ata_qc_issue(qc);

	VPRINTK("EXIT\n");
	return;

early_finish:
        ata_qc_free(qc);
	done(cmd);
	DPRINTK("EXIT - early finish (good or error)\n");
	return;

err_did:
	ata_qc_free(qc);
err_mem:
	cmd->result = (DID_ERROR << 16);
	done(cmd);
	DPRINTK("EXIT - internal\n");
	return;
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
 *	completed successfully (0), or not (in which case cmd->result
 *	and sense buffer are assumed to be set).
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

	if (rc == 0)
		cmd->result = SAM_STAT_GOOD;
	args->done(cmd);
}

/**
 *	ata_scsiop_inq_std - Simulate INQUIRY command
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Returns standard device identification data associated
 *	with non-VPD INQUIRY command output.
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
		ata_id_string(args->id, &rbuf[16], ATA_ID_PROD_OFS, 16);
		ata_id_string(args->id, &rbuf[32], ATA_ID_FW_REV_OFS, 4);
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
 *	ata_scsiop_inq_00 - Simulate INQUIRY VPD page 0, list of pages
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Returns list of inquiry VPD pages available.
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
	rbuf[3] = sizeof(pages);	/* number of supported VPD pages */

	if (buflen > 6)
		memcpy(rbuf + 4, pages, sizeof(pages));

	return 0;
}

/**
 *	ata_scsiop_inq_80 - Simulate INQUIRY VPD page 80, device serial number
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
		ata_id_string(args->id, (unsigned char *) &rbuf[4],
			      ATA_ID_SERNO_OFS, ATA_SERNO_LEN);

	return 0;
}

/**
 *	ata_scsiop_inq_83 - Simulate INQUIRY VPD page 83, device identity
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Yields two logical unit device identification designators:
 *	 - vendor specific ASCII containing the ATA serial number
 *	 - SAT defined "t10 vendor id based" containing ASCII vendor
 *	   name ("ATA     "), model and serial numbers.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_inq_83(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen)
{
	int num;
	const int sat_model_serial_desc_len = 68;
	const int ata_model_byte_len = 40;

	rbuf[1] = 0x83;			/* this page code */
	num = 4;

	if (buflen > (ATA_SERNO_LEN + num + 3)) {
		/* piv=0, assoc=lu, code_set=ACSII, designator=vendor */
		rbuf[num + 0] = 2;
		rbuf[num + 3] = ATA_SERNO_LEN;
		num += 4;
		ata_id_string(args->id, (unsigned char *) rbuf + num,
			      ATA_ID_SERNO_OFS, ATA_SERNO_LEN);
		num += ATA_SERNO_LEN;
	}
	if (buflen > (sat_model_serial_desc_len + num + 3)) {
		/* SAT defined lu model and serial numbers descriptor */
		/* piv=0, assoc=lu, code_set=ACSII, designator=t10 vendor id */
		rbuf[num + 0] = 2;
		rbuf[num + 1] = 1;
		rbuf[num + 3] = sat_model_serial_desc_len;
		num += 4;
		memcpy(rbuf + num, "ATA     ", 8);
		num += 8;
		ata_id_string(args->id, (unsigned char *) rbuf + num,
			      ATA_ID_PROD_OFS, ata_model_byte_len);
		num += ata_model_byte_len;
		ata_id_string(args->id, (unsigned char *) rbuf + num,
			      ATA_ID_SERNO_OFS, ATA_SERNO_LEN);
		num += ATA_SERNO_LEN;
	}
	rbuf[3] = num - 4;    /* page len (assume less than 256 bytes) */
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
	u8 page[CACHE_MPAGE_LEN];

	memcpy(page, def_cache_mpage, sizeof(page));
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
	ata_msense_push(ptr_io, last, def_control_mpage,
			sizeof(def_control_mpage));
	return sizeof(def_control_mpage);
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

	ata_msense_push(ptr_io, last, def_rw_recovery_mpage,
			sizeof(def_rw_recovery_mpage));
	return sizeof(def_rw_recovery_mpage);
}

/*
 * We can turn this into a real blacklist if it's needed, for now just
 * blacklist any Maxtor BANC1G10 revision firmware
 */
static int ata_dev_supports_fua(u16 *id)
{
	unsigned char model[41], fw[9];

	if (!libata_fua)
		return 0;
	if (!ata_id_has_fua(id))
		return 0;

	ata_id_c_string(id, model, ATA_ID_PROD_OFS, sizeof(model));
	ata_id_c_string(id, fw, ATA_ID_FW_REV_OFS, sizeof(fw));

	if (strcmp(model, "Maxtor"))
		return 1;
	if (strcmp(fw, "BANC1G10"))
		return 1;

	return 0; /* blacklisted */
}

/**
 *	ata_scsiop_mode_sense - Simulate MODE SENSE 6, 10 commands
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Simulate MODE SENSE commands. Assume this is invoked for direct
 *	access devices (e.g. disks) only. There should be no block
 *	descriptor for other device types.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

unsigned int ata_scsiop_mode_sense(struct ata_scsi_args *args, u8 *rbuf,
				  unsigned int buflen)
{
	struct ata_device *dev = args->dev;
	u8 *scsicmd = args->cmd->cmnd, *p, *last;
	const u8 sat_blk_desc[] = {
		0, 0, 0, 0,	/* number of blocks: sat unspecified */
		0,
		0, 0x2, 0x0	/* block length: 512 bytes */
	};
	u8 pg, spg;
	unsigned int ebd, page_control, six_byte, output_len, alloc_len, minlen;
	u8 dpofua;

	VPRINTK("ENTER\n");

	six_byte = (scsicmd[0] == MODE_SENSE);
	ebd = !(scsicmd[1] & 0x8);      /* dbd bit inverted == edb */
	/*
	 * LLBA bit in msense(10) ignored (compliant)
	 */

	page_control = scsicmd[2] >> 6;
	switch (page_control) {
	case 0: /* current */
		break;  /* supported */
	case 3: /* saved */
		goto saving_not_supp;
	case 1: /* changeable */
	case 2: /* defaults */
	default:
		goto invalid_fld;
	}

	if (six_byte) {
		output_len = 4 + (ebd ? 8 : 0);
		alloc_len = scsicmd[4];
	} else {
		output_len = 8 + (ebd ? 8 : 0);
		alloc_len = (scsicmd[7] << 8) + scsicmd[8];
	}
	minlen = (alloc_len < buflen) ? alloc_len : buflen;

	p = rbuf + output_len;
	last = rbuf + minlen - 1;

	pg = scsicmd[2] & 0x3f;
	spg = scsicmd[3];
	/*
	 * No mode subpages supported (yet) but asking for _all_
	 * subpages may be valid
	 */
	if (spg && (spg != ALL_SUB_MPAGES))
		goto invalid_fld;

	switch(pg) {
	case RW_RECOVERY_MPAGE:
		output_len += ata_msense_rw_recovery(&p, last);
		break;

	case CACHE_MPAGE:
		output_len += ata_msense_caching(args->id, &p, last);
		break;

	case CONTROL_MPAGE: {
		output_len += ata_msense_ctl_mode(&p, last);
		break;
		}

	case ALL_MPAGES:
		output_len += ata_msense_rw_recovery(&p, last);
		output_len += ata_msense_caching(args->id, &p, last);
		output_len += ata_msense_ctl_mode(&p, last);
		break;

	default:		/* invalid page code */
		goto invalid_fld;
	}

	if (minlen < 1)
		return 0;

	dpofua = 0;
	if (ata_dev_supports_fua(args->id) && dev->flags & ATA_DFLAG_LBA48 &&
	    (!(dev->flags & ATA_DFLAG_PIO) || dev->multi_count))
		dpofua = 1 << 4;

	if (six_byte) {
		output_len--;
		rbuf[0] = output_len;
		if (minlen > 2)
			rbuf[2] |= dpofua;
		if (ebd) {
			if (minlen > 3)
				rbuf[3] = sizeof(sat_blk_desc);
			if (minlen > 11)
				memcpy(rbuf + 4, sat_blk_desc,
				       sizeof(sat_blk_desc));
		}
	} else {
		output_len -= 2;
		rbuf[0] = output_len >> 8;
		if (minlen > 1)
			rbuf[1] = output_len;
		if (minlen > 3)
			rbuf[3] |= dpofua;
		if (ebd) {
			if (minlen > 7)
				rbuf[7] = sizeof(sat_blk_desc);
			if (minlen > 15)
				memcpy(rbuf + 8, sat_blk_desc,
				       sizeof(sat_blk_desc));
		}
	}
	return 0;

invalid_fld:
	ata_scsi_set_sense(args->cmd, ILLEGAL_REQUEST, 0x24, 0x0);
	/* "Invalid field in cbd" */
	return 1;

saving_not_supp:
	ata_scsi_set_sense(args->cmd, ILLEGAL_REQUEST, 0x39, 0x0);
	 /* "Saving parameters not supported" */
	return 1;
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

	if (ata_id_has_lba(args->id)) {
		if (ata_id_has_lba48(args->id))
			n_sectors = ata_id_u64(args->id, 100);
		else
			n_sectors = ata_id_u32(args->id, 60);
	} else {
		/* CHS default translation */
		n_sectors = args->id[1] * args->id[3] * args->id[6];

		if (ata_id_current_chs_valid(args->id))
			/* CHS current translation */
			n_sectors = ata_id_u32(args->id, 57);
	}

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
 *	ata_scsi_set_sense - Set SCSI sense data and status
 *	@cmd: SCSI request to be handled
 *	@sk: SCSI-defined sense key
 *	@asc: SCSI-defined additional sense code
 *	@ascq: SCSI-defined additional sense code qualifier
 *
 *	Helper function that builds a valid fixed format, current
 *	response code and the given sense key (sk), additional sense
 *	code (asc) and additional sense code qualifier (ascq) with
 *	a SCSI command status of %SAM_STAT_CHECK_CONDITION and
 *	DRIVER_SENSE set in the upper bits of scsi_cmnd::result .
 *
 *	LOCKING:
 *	Not required
 */

void ata_scsi_set_sense(struct scsi_cmnd *cmd, u8 sk, u8 asc, u8 ascq)
{
	cmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

	cmd->sense_buffer[0] = 0x70;	/* fixed format, current */
	cmd->sense_buffer[2] = sk;
	cmd->sense_buffer[7] = 18 - 8;	/* additional sense length */
	cmd->sense_buffer[12] = asc;
	cmd->sense_buffer[13] = ascq;
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
	ata_scsi_set_sense(cmd, ILLEGAL_REQUEST, asc, ascq);

	done(cmd);
}

static void atapi_sense_complete(struct ata_queued_cmd *qc)
{
	if (qc->err_mask && ((qc->err_mask & AC_ERR_DEV) == 0))
		/* FIXME: not quite right; we don't want the
		 * translation of taskfile registers into
		 * a sense descriptors, since that's only
		 * correct for ATA, not ATAPI
		 */
		ata_gen_ata_desc_sense(qc);

	qc->scsidone(qc->scsicmd);
	ata_qc_free(qc);
}

/* is it pointless to prefer PIO for "safety reasons"? */
static inline int ata_pio_use_silly(struct ata_port *ap)
{
	return (ap->flags & ATA_FLAG_PIO_DMA);
}

static void atapi_request_sense(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scsi_cmnd *cmd = qc->scsicmd;

	DPRINTK("ATAPI request sense\n");

	/* FIXME: is this needed? */
	memset(cmd->sense_buffer, 0, sizeof(cmd->sense_buffer));

	ap->ops->tf_read(ap, &qc->tf);

	/* fill these in, for the case where they are -not- overwritten */
	cmd->sense_buffer[0] = 0x70;
	cmd->sense_buffer[2] = qc->tf.feature >> 4;

	ata_qc_reinit(qc);

	ata_sg_init_one(qc, cmd->sense_buffer, sizeof(cmd->sense_buffer));
	qc->dma_dir = DMA_FROM_DEVICE;

	memset(&qc->cdb, 0, qc->dev->cdb_len);
	qc->cdb[0] = REQUEST_SENSE;
	qc->cdb[4] = SCSI_SENSE_BUFFERSIZE;

	qc->tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	qc->tf.command = ATA_CMD_PACKET;

	if (ata_pio_use_silly(ap)) {
		qc->tf.protocol = ATA_PROT_ATAPI_DMA;
		qc->tf.feature |= ATAPI_PKT_DMA;
	} else {
		qc->tf.protocol = ATA_PROT_ATAPI;
		qc->tf.lbam = (8 * 1024) & 0xff;
		qc->tf.lbah = (8 * 1024) >> 8;
	}
	qc->nbytes = SCSI_SENSE_BUFFERSIZE;

	qc->complete_fn = atapi_sense_complete;

	ata_qc_issue(qc);

	DPRINTK("EXIT\n");
}

static void atapi_qc_complete(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	unsigned int err_mask = qc->err_mask;

	VPRINTK("ENTER, err_mask 0x%X\n", err_mask);

	if (unlikely(err_mask & AC_ERR_DEV)) {
		cmd->result = SAM_STAT_CHECK_CONDITION;
		atapi_request_sense(qc);
		return;
	}

	else if (unlikely(err_mask))
		/* FIXME: not quite right; we don't want the
		 * translation of taskfile registers into
		 * a sense descriptors, since that's only
		 * correct for ATA, not ATAPI
		 */
		ata_gen_ata_desc_sense(qc);

	else {
		u8 *scsicmd = cmd->cmnd;

		if ((scsicmd[0] == INQUIRY) && ((scsicmd[1] & 0x03) == 0)) {
			u8 *buf = NULL;
			unsigned int buflen;

			buflen = ata_scsi_rbuf_get(cmd, &buf);

	/* ATAPI devices typically report zero for their SCSI version,
	 * and sometimes deviate from the spec WRT response data
	 * format.  If SCSI version is reported as zero like normal,
	 * then we make the following fixups:  1) Fake MMC-5 version,
	 * to indicate to the Linux scsi midlayer this is a modern
	 * device.  2) Ensure response data format / ATAPI information
	 * are always correct.
	 */
			if (buf[2] == 0) {
				buf[2] = 0x5;
				buf[3] = 0x32;
			}

			ata_scsi_rbuf_put(cmd, buf);
		}

		cmd->result = SAM_STAT_GOOD;
	}

	qc->scsidone(cmd);
	ata_qc_free(qc);
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

static unsigned int atapi_xlat(struct ata_queued_cmd *qc, const u8 *scsicmd)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct ata_device *dev = qc->dev;
	int using_pio = (dev->flags & ATA_DFLAG_PIO);
	int nodata = (cmd->sc_data_direction == DMA_NONE);

	if (!using_pio)
		/* Check whether ATAPI DMA is safe */
		if (ata_check_atapi_dma(qc))
			using_pio = 1;

	memcpy(&qc->cdb, scsicmd, dev->cdb_len);

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

	qc->nbytes = cmd->request_bufflen;

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
ata_scsi_find_dev(struct ata_port *ap, const struct scsi_device *scsidev)
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

	if (!atapi_enabled || (ap->flags & ATA_FLAG_NO_ATAPI)) {
		if (unlikely(dev->class == ATA_DEV_ATAPI)) {
			printk(KERN_WARNING "ata%u(%u): WARNING: ATAPI is %s, device ignored.\n",
			       ap->id, dev->devno, atapi_enabled ? "not supported with this driver" : "disabled");
			return NULL;
		}
	}

	return dev;
}

/*
 *	ata_scsi_map_proto - Map pass-thru protocol value to taskfile value.
 *	@byte1: Byte 1 from pass-thru CDB.
 *
 *	RETURNS:
 *	ATA_PROT_UNKNOWN if mapping failed/unimplemented, protocol otherwise.
 */
static u8
ata_scsi_map_proto(u8 byte1)
{
	switch((byte1 & 0x1e) >> 1) {
		case 3:		/* Non-data */
			return ATA_PROT_NODATA;

		case 6:		/* DMA */
			return ATA_PROT_DMA;

		case 4:		/* PIO Data-in */
		case 5:		/* PIO Data-out */
			return ATA_PROT_PIO;

		case 10:	/* Device Reset */
		case 0:		/* Hard Reset */
		case 1:		/* SRST */
		case 2:		/* Bus Idle */
		case 7:		/* Packet */
		case 8:		/* DMA Queued */
		case 9:		/* Device Diagnostic */
		case 11:	/* UDMA Data-in */
		case 12:	/* UDMA Data-Out */
		case 13:	/* FPDMA */
		default:	/* Reserved */
			break;
	}

	return ATA_PROT_UNKNOWN;
}

/**
 *	ata_scsi_pass_thru - convert ATA pass-thru CDB to taskfile
 *	@qc: command structure to be initialized
 *	@scsicmd: SCSI command to convert
 *
 *	Handles either 12 or 16-byte versions of the CDB.
 *
 *	RETURNS:
 *	Zero on success, non-zero on failure.
 */
static unsigned int
ata_scsi_pass_thru(struct ata_queued_cmd *qc, const u8 *scsicmd)
{
	struct ata_taskfile *tf = &(qc->tf);
	struct scsi_cmnd *cmd = qc->scsicmd;

	if ((tf->protocol = ata_scsi_map_proto(scsicmd[1])) == ATA_PROT_UNKNOWN)
		goto invalid_fld;

	if (scsicmd[1] & 0xe0)
		/* PIO multi not supported yet */
		goto invalid_fld;

	/*
	 * 12 and 16 byte CDBs use different offsets to
	 * provide the various register values.
	 */
	if (scsicmd[0] == ATA_16) {
		/*
		 * 16-byte CDB - may contain extended commands.
		 *
		 * If that is the case, copy the upper byte register values.
		 */
		if (scsicmd[1] & 0x01) {
			tf->hob_feature = scsicmd[3];
			tf->hob_nsect = scsicmd[5];
			tf->hob_lbal = scsicmd[7];
			tf->hob_lbam = scsicmd[9];
			tf->hob_lbah = scsicmd[11];
			tf->flags |= ATA_TFLAG_LBA48;
		} else
			tf->flags &= ~ATA_TFLAG_LBA48;

		/*
		 * Always copy low byte, device and command registers.
		 */
		tf->feature = scsicmd[4];
		tf->nsect = scsicmd[6];
		tf->lbal = scsicmd[8];
		tf->lbam = scsicmd[10];
		tf->lbah = scsicmd[12];
		tf->device = scsicmd[13];
		tf->command = scsicmd[14];
	} else {
		/*
		 * 12-byte CDB - incapable of extended commands.
		 */
		tf->flags &= ~ATA_TFLAG_LBA48;

		tf->feature = scsicmd[3];
		tf->nsect = scsicmd[4];
		tf->lbal = scsicmd[5];
		tf->lbam = scsicmd[6];
		tf->lbah = scsicmd[7];
		tf->device = scsicmd[8];
		tf->command = scsicmd[9];
	}
	/*
	 * If slave is possible, enforce correct master/slave bit
	*/
	if (qc->ap->flags & ATA_FLAG_SLAVE_POSS)
		tf->device = qc->dev->devno ?
			tf->device | ATA_DEV1 : tf->device & ~ATA_DEV1;

	/*
	 * Filter SET_FEATURES - XFER MODE command -- otherwise,
	 * SET_FEATURES - XFER MODE must be preceded/succeeded
	 * by an update to hardware-specific registers for each
	 * controller (i.e. the reason for ->set_piomode(),
	 * ->set_dmamode(), and ->post_set_mode() hooks).
	 */
	if ((tf->command == ATA_CMD_SET_FEATURES)
	 && (tf->feature == SETFEATURES_XFER))
		goto invalid_fld;

	/*
	 * Set flags so that all registers will be written,
	 * and pass on write indication (used for PIO/DMA
	 * setup.)
	 */
	tf->flags |= (ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE);

	if (cmd->sc_data_direction == DMA_TO_DEVICE)
		tf->flags |= ATA_TFLAG_WRITE;

	/*
	 * Set transfer length.
	 *
	 * TODO: find out if we need to do more here to
	 *       cover scatter/gather case.
	 */
	qc->nsect = cmd->request_bufflen / ATA_SECT_SIZE;

	return 0;

 invalid_fld:
	ata_scsi_set_sense(qc->scsicmd, ILLEGAL_REQUEST, 0x24, 0x00);
	/* "Invalid field in cdb" */
	return 1;
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

	case ATA_12:
	case ATA_16:
		return ata_scsi_pass_thru;

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

static inline void __ata_scsi_queuecmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *),
				       struct ata_port *ap, struct ata_device *dev)
{
	if (dev->class == ATA_DEV_ATA) {
		ata_xlat_func_t xlat_func = ata_get_xlat_func(dev,
							      cmd->cmnd[0]);

		if (xlat_func)
			ata_scsi_translate(ap, dev, cmd, done, xlat_func);
		else
			ata_scsi_simulate(ap, dev, cmd, done);
	} else
		ata_scsi_translate(ap, dev, cmd, done, atapi_xlat);
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
	struct Scsi_Host *shost = scsidev->host;

	ap = (struct ata_port *) &shost->hostdata[0];

	spin_unlock(shost->host_lock);
	spin_lock(&ap->host_set->lock);

	ata_scsi_dump_cdb(ap, cmd);

	dev = ata_scsi_find_dev(ap, scsidev);
	if (likely(dev))
		__ata_scsi_queuecmd(cmd, done, ap, dev);
	else {
		cmd->result = (DID_BAD_TARGET << 16);
		done(cmd);
	}

	spin_unlock(&ap->host_set->lock);
	spin_lock(shost->host_lock);
	return 0;
}

/**
 *	ata_scsi_simulate - simulate SCSI command on ATA device
 *	@ap: port the device is connected to
 *	@dev: the target device
 *	@cmd: SCSI command being sent to device.
 *	@done: SCSI command completion function.
 *
 *	Interprets and directly executes a select list of SCSI commands
 *	that can be handled internally.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_scsi_simulate(struct ata_port *ap, struct ata_device *dev,
		      struct scsi_cmnd *cmd,
		      void (*done)(struct scsi_cmnd *))
{
	struct ata_scsi_args args;
	const u8 *scsicmd = cmd->cmnd;

	args.ap = ap;
	args.dev = dev;
	args.id = dev->id;
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
				ata_scsi_invalid_field(cmd, done);
			else if ((scsicmd[1] & 1) == 0)    /* is EVPD clear? */
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_std);
			else if (scsicmd[2] == 0x00)
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_00);
			else if (scsicmd[2] == 0x80)
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_80);
			else if (scsicmd[2] == 0x83)
				ata_scsi_rbuf_fill(&args, ata_scsiop_inq_83);
			else
				ata_scsi_invalid_field(cmd, done);
			break;

		case MODE_SENSE:
		case MODE_SENSE_10:
			ata_scsi_rbuf_fill(&args, ata_scsiop_mode_sense);
			break;

		case MODE_SELECT:	/* unconditionally return */
		case MODE_SELECT_10:	/* bad-field-in-cdb */
			ata_scsi_invalid_field(cmd, done);
			break;

		case READ_CAPACITY:
			ata_scsi_rbuf_fill(&args, ata_scsiop_read_cap);
			break;

		case SERVICE_ACTION_IN:
			if ((scsicmd[1] & 0x1f) == SAI_READ_CAPACITY_16)
				ata_scsi_rbuf_fill(&args, ata_scsiop_read_cap);
			else
				ata_scsi_invalid_field(cmd, done);
			break;

		case REPORT_LUNS:
			ata_scsi_rbuf_fill(&args, ata_scsiop_report_luns);
			break;

		/* mandatory commands we haven't implemented yet */
		case REQUEST_SENSE:

		/* all other commands */
		default:
			ata_scsi_set_sense(cmd, ILLEGAL_REQUEST, 0x20, 0x0);
			/* "Invalid command operation code" */
			done(cmd);
			break;
	}
}

void ata_scsi_scan_host(struct ata_port *ap)
{
	struct ata_device *dev;
	unsigned int i;

	if (ap->flags & ATA_FLAG_PORT_DISABLED)
		return;

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		dev = &ap->device[i];

		if (ata_dev_present(dev))
			scsi_scan_target(&ap->host->shost_gendev, 0, i, 0, 0);
	}
}

