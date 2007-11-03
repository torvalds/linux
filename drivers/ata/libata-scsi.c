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
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport.h>
#include <linux/libata.h>
#include <linux/hdreg.h>
#include <linux/uaccess.h>

#include "libata.h"

#define SECTOR_SIZE	512

typedef unsigned int (*ata_xlat_func_t)(struct ata_queued_cmd *qc);

static struct ata_device *__ata_scsi_find_dev(struct ata_port *ap,
					const struct scsi_device *scsidev);
static struct ata_device *ata_scsi_find_dev(struct ata_port *ap,
					    const struct scsi_device *scsidev);
static int ata_scsi_user_scan(struct Scsi_Host *shost, unsigned int channel,
			      unsigned int id, unsigned int lun);


#define RW_RECOVERY_MPAGE 0x1
#define RW_RECOVERY_MPAGE_LEN 12
#define CACHE_MPAGE 0x8
#define CACHE_MPAGE_LEN 20
#define CONTROL_MPAGE 0xa
#define CONTROL_MPAGE_LEN 12
#define ALL_MPAGES 0x3f
#define ALL_SUB_MPAGES 0xff


static const u8 def_rw_recovery_mpage[RW_RECOVERY_MPAGE_LEN] = {
	RW_RECOVERY_MPAGE,
	RW_RECOVERY_MPAGE_LEN - 2,
	(1 << 7),	/* AWRE */
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
static struct scsi_transport_template ata_scsi_transport_template = {
	.eh_strategy_handler	= ata_scsi_error,
	.eh_timed_out		= ata_scsi_timed_out,
	.user_scan		= ata_scsi_user_scan,
};


static const struct {
	enum link_pm	value;
	const char	*name;
} link_pm_policy[] = {
	{ NOT_AVAILABLE, "max_performance" },
	{ MIN_POWER, "min_power" },
	{ MAX_PERFORMANCE, "max_performance" },
	{ MEDIUM_POWER, "medium_power" },
};

static const char *ata_scsi_lpm_get(enum link_pm policy)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(link_pm_policy); i++)
		if (link_pm_policy[i].value == policy)
			return link_pm_policy[i].name;

	return NULL;
}

static ssize_t ata_scsi_lpm_put(struct class_device *class_dev,
	const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(class_dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	enum link_pm policy = 0;
	int i;

	/*
	 * we are skipping array location 0 on purpose - this
	 * is because a value of NOT_AVAILABLE is displayed
	 * to the user as max_performance, but when the user
	 * writes "max_performance", they actually want the
	 * value to match MAX_PERFORMANCE.
	 */
	for (i = 1; i < ARRAY_SIZE(link_pm_policy); i++) {
		const int len = strlen(link_pm_policy[i].name);
		if (strncmp(link_pm_policy[i].name, buf, len) == 0 &&
		   buf[len] == '\n') {
			policy = link_pm_policy[i].value;
			break;
		}
	}
	if (!policy)
		return -EINVAL;

	ata_lpm_schedule(ap, policy);
	return count;
}

static ssize_t
ata_scsi_lpm_show(struct class_device *class_dev, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(class_dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	const char *policy =
		ata_scsi_lpm_get(ap->pm_policy);

	if (!policy)
		return -EINVAL;

	return snprintf(buf, 23, "%s\n", policy);
}
CLASS_DEVICE_ATTR(link_power_management_policy, S_IRUGO | S_IWUSR,
		ata_scsi_lpm_show, ata_scsi_lpm_put);
EXPORT_SYMBOL_GPL(class_device_attr_link_power_management_policy);

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
 *	ata_get_identity - Handler for HDIO_GET_IDENTITY ioctl
 *	@sdev: SCSI device to get identify data for
 *	@arg: User buffer area for identify data
 *
 *	LOCKING:
 *	Defined by the SCSI layer.  We don't really care.
 *
 *	RETURNS:
 *	Zero on success, negative errno on error.
 */
static int ata_get_identity(struct scsi_device *sdev, void __user *arg)
{
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev = ata_scsi_find_dev(ap, sdev);
	u16 __user *dst = arg;
	char buf[40];

	if (!dev)
		return -ENOMSG;

	if (copy_to_user(dst, dev->id, ATA_ID_WORDS * sizeof(u16)))
		return -EFAULT;

	ata_id_string(dev->id, buf, ATA_ID_PROD, ATA_ID_PROD_LEN);
	if (copy_to_user(dst + ATA_ID_PROD, buf, ATA_ID_PROD_LEN))
		return -EFAULT;

	ata_id_string(dev->id, buf, ATA_ID_FW_REV, ATA_ID_FW_REV_LEN);
	if (copy_to_user(dst + ATA_ID_FW_REV, buf, ATA_ID_FW_REV_LEN))
		return -EFAULT;

	ata_id_string(dev->id, buf, ATA_ID_SERNO, ATA_ID_SERNO_LEN);
	if (copy_to_user(dst + ATA_ID_SERNO, buf, ATA_ID_SERNO_LEN))
		return -EFAULT;

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
	u8 args[4], *argbuf = NULL, *sensebuf = NULL;
	int argsize = 0;
	enum dma_data_direction data_dir;
	int cmd_result;

	if (arg == NULL)
		return -EINVAL;

	if (copy_from_user(args, arg, sizeof(args)))
		return -EFAULT;

	sensebuf = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_NOIO);
	if (!sensebuf)
		return -ENOMEM;

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
		scsi_cmd[2]  = 0x20;     /* cc but no off.line or data xfer */
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
	cmd_result = scsi_execute(scsidev, scsi_cmd, data_dir, argbuf, argsize,
				  sensebuf, (10*HZ), 5, 0);

	if (driver_byte(cmd_result) == DRIVER_SENSE) {/* sense data available */
		u8 *desc = sensebuf + 8;
		cmd_result &= ~(0xFF<<24); /* DRIVER_SENSE is not an error */

		/* If we set cc then ATA pass-through will cause a
		 * check condition even if no error. Filter that. */
		if (cmd_result & SAM_STAT_CHECK_CONDITION) {
			struct scsi_sense_hdr sshdr;
			scsi_normalize_sense(sensebuf, SCSI_SENSE_BUFFERSIZE,
					     &sshdr);
			if (sshdr.sense_key == 0 &&
			    sshdr.asc == 0 && sshdr.ascq == 0)
				cmd_result &= ~SAM_STAT_CHECK_CONDITION;
		}

		/* Send userspace a few ATA registers (same as drivers/ide) */
		if (sensebuf[0] == 0x72 &&	/* format is "descriptor" */
		    desc[0] == 0x09) {		/* code is "ATA Descriptor" */
			args[0] = desc[13];	/* status */
			args[1] = desc[3];	/* error */
			args[2] = desc[5];	/* sector count (0:7) */
			if (copy_to_user(arg, args, sizeof(args)))
				rc = -EFAULT;
		}
	}


	if (cmd_result) {
		rc = -EIO;
		goto error;
	}

	if ((argbuf)
	 && copy_to_user(arg + sizeof(args), argbuf, argsize))
		rc = -EFAULT;
error:
	kfree(sensebuf);
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
	u8 args[7], *sensebuf = NULL;
	int cmd_result;

	if (arg == NULL)
		return -EINVAL;

	if (copy_from_user(args, arg, sizeof(args)))
		return -EFAULT;

	sensebuf = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_NOIO);
	if (!sensebuf)
		return -ENOMEM;

	memset(scsi_cmd, 0, sizeof(scsi_cmd));
	scsi_cmd[0]  = ATA_16;
	scsi_cmd[1]  = (3 << 1); /* Non-data */
	scsi_cmd[2]  = 0x20;     /* cc but no off.line or data xfer */
	scsi_cmd[4]  = args[1];
	scsi_cmd[6]  = args[2];
	scsi_cmd[8]  = args[3];
	scsi_cmd[10] = args[4];
	scsi_cmd[12] = args[5];
	scsi_cmd[13] = args[6] & 0x4f;
	scsi_cmd[14] = args[0];

	/* Good values for timeout and retries?  Values below
	   from scsi_ioctl_send_command() for default case... */
	cmd_result = scsi_execute(scsidev, scsi_cmd, DMA_NONE, NULL, 0,
				sensebuf, (10*HZ), 5, 0);

	if (driver_byte(cmd_result) == DRIVER_SENSE) {/* sense data available */
		u8 *desc = sensebuf + 8;
		cmd_result &= ~(0xFF<<24); /* DRIVER_SENSE is not an error */

		/* If we set cc then ATA pass-through will cause a
		 * check condition even if no error. Filter that. */
		if (cmd_result & SAM_STAT_CHECK_CONDITION) {
			struct scsi_sense_hdr sshdr;
			scsi_normalize_sense(sensebuf, SCSI_SENSE_BUFFERSIZE,
						&sshdr);
			if (sshdr.sense_key == 0 &&
				sshdr.asc == 0 && sshdr.ascq == 0)
				cmd_result &= ~SAM_STAT_CHECK_CONDITION;
		}

		/* Send userspace ATA registers */
		if (sensebuf[0] == 0x72 &&	/* format is "descriptor" */
				desc[0] == 0x09) {/* code is "ATA Descriptor" */
			args[0] = desc[13];	/* status */
			args[1] = desc[3];	/* error */
			args[2] = desc[5];	/* sector count (0:7) */
			args[3] = desc[7];	/* lbal */
			args[4] = desc[9];	/* lbam */
			args[5] = desc[11];	/* lbah */
			args[6] = desc[12];	/* select */
			if (copy_to_user(arg, args, sizeof(args)))
				rc = -EFAULT;
		}
	}

	if (cmd_result) {
		rc = -EIO;
		goto error;
	}

 error:
	kfree(sensebuf);
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

	case HDIO_GET_IDENTITY:
		return ata_get_identity(scsidev, arg);

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
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Command allocated, or %NULL if none available.
 */
static struct ata_queued_cmd *ata_scsi_qc_new(struct ata_device *dev,
					      struct scsi_cmnd *cmd,
					      void (*done)(struct scsi_cmnd *))
{
	struct ata_queued_cmd *qc;

	qc = ata_qc_new_init(dev);
	if (qc) {
		qc->scsicmd = cmd;
		qc->scsidone = done;

		qc->__sg = scsi_sglist(cmd);
		qc->n_elem = scsi_sg_count(cmd);
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
static void ata_dump_status(unsigned id, struct ata_taskfile *tf)
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

/**
 *	ata_to_sense_error - convert ATA error to SCSI error
 *	@id: ATA device number
 *	@drv_stat: value contained in ATA status register
 *	@drv_err: value contained in ATA error register
 *	@sk: the sense key we'll fill out
 *	@asc: the additional sense code we'll fill out
 *	@ascq: the additional sense code qualifier we'll fill out
 *	@verbose: be verbose
 *
 *	Converts an ATA error into a SCSI error.  Fill out pointers to
 *	SK, ASC, and ASCQ bytes for later use in fixed or descriptor
 *	format sense blocks.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static void ata_to_sense_error(unsigned id, u8 drv_stat, u8 drv_err, u8 *sk,
			       u8 *asc, u8 *ascq, int verbose)
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
		if (verbose)
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
	if (verbose)
		printk(KERN_WARNING "ata%u: no sense translation for "
		       "status: 0x%02x\n", id, drv_stat);

	/* We need a sensible error return here, which is tricky, and one
	   that won't cause people to do things like return a disk wrongly */
	*sk = ABORTED_COMMAND;
	*asc = 0x00;
	*ascq = 0x00;

 translate_done:
	if (verbose)
		printk(KERN_ERR "ata%u: translated ATA stat/err 0x%02x/%02x "
		       "to SCSI SK/ASC/ASCQ 0x%x/%02x/%02x\n",
		       id, drv_stat, drv_err, *sk, *asc, *ascq);
	return;
}

/*
 *	ata_gen_passthru_sense - Generate check condition sense block.
 *	@qc: Command that completed.
 *
 *	This function is specific to the ATA descriptor format sense
 *	block specified for the ATA pass through commands.  Regardless
 *	of whether the command errored or not, return a sense
 *	block. Copy all controller registers into the sense
 *	block. Clear sense key, ASC & ASCQ if there is no error.
 *
 *	LOCKING:
 *	None.
 */
static void ata_gen_passthru_sense(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct ata_taskfile *tf = &qc->result_tf;
	unsigned char *sb = cmd->sense_buffer;
	unsigned char *desc = sb + 8;
	int verbose = qc->ap->ops->error_handler == NULL;

	memset(sb, 0, SCSI_SENSE_BUFFERSIZE);

	cmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

	/*
	 * Use ata_to_sense_error() to map status register bits
	 * onto sense key, asc & ascq.
	 */
	if (qc->err_mask ||
	    tf->command & (ATA_BUSY | ATA_DF | ATA_ERR | ATA_DRQ)) {
		ata_to_sense_error(qc->ap->print_id, tf->command, tf->feature,
				   &sb[1], &sb[2], &sb[3], verbose);
		sb[1] &= 0x0f;
	}

	/*
	 * Sense data is current and format is descriptor.
	 */
	sb[0] = 0x72;

	desc[0] = 0x09;

	/* set length of additional sense data */
	sb[7] = 14;
	desc[1] = 12;

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
 *	ata_gen_ata_sense - generate a SCSI fixed sense block
 *	@qc: Command that we are erroring out
 *
 *	Generate sense block for a failed ATA command @qc.  Descriptor
 *	format is used to accomodate LBA48 block address.
 *
 *	LOCKING:
 *	None.
 */
static void ata_gen_ata_sense(struct ata_queued_cmd *qc)
{
	struct ata_device *dev = qc->dev;
	struct scsi_cmnd *cmd = qc->scsicmd;
	struct ata_taskfile *tf = &qc->result_tf;
	unsigned char *sb = cmd->sense_buffer;
	unsigned char *desc = sb + 8;
	int verbose = qc->ap->ops->error_handler == NULL;
	u64 block;

	memset(sb, 0, SCSI_SENSE_BUFFERSIZE);

	cmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

	/* sense data is current and format is descriptor */
	sb[0] = 0x72;

	/* Use ata_to_sense_error() to map status register bits
	 * onto sense key, asc & ascq.
	 */
	if (qc->err_mask ||
	    tf->command & (ATA_BUSY | ATA_DF | ATA_ERR | ATA_DRQ)) {
		ata_to_sense_error(qc->ap->print_id, tf->command, tf->feature,
				   &sb[1], &sb[2], &sb[3], verbose);
		sb[1] &= 0x0f;
	}

	block = ata_tf_read_block(&qc->result_tf, dev);

	/* information sense data descriptor */
	sb[7] = 12;
	desc[0] = 0x00;
	desc[1] = 10;

	desc[2] |= 0x80;	/* valid */
	desc[6] = block >> 40;
	desc[7] = block >> 32;
	desc[8] = block >> 24;
	desc[9] = block >> 16;
	desc[10] = block >> 8;
	desc[11] = block;
}

static void ata_scsi_sdev_config(struct scsi_device *sdev)
{
	sdev->use_10_for_rw = 1;
	sdev->use_10_for_ms = 1;

	/* Schedule policy is determined by ->qc_defer() callback and
	 * it needs to see every deferred qc.  Set dev_blocked to 1 to
	 * prevent SCSI midlayer from automatically deferring
	 * requests.
	 */
	sdev->max_device_blocked = 1;
}

static void ata_scsi_dev_config(struct scsi_device *sdev,
				struct ata_device *dev)
{
	/* configure max sectors */
	blk_queue_max_sectors(sdev->request_queue, dev->max_sectors);

	/* SATA DMA transfers must be multiples of 4 byte, so
	 * we need to pad ATAPI transfers using an extra sg.
	 * Decrement max hw segments accordingly.
	 */
	if (dev->class == ATA_DEV_ATAPI) {
		struct request_queue *q = sdev->request_queue;
		blk_queue_max_hw_segments(q, q->max_hw_segments - 1);
	}

	if (dev->flags & ATA_DFLAG_NCQ) {
		int depth;

		depth = min(sdev->host->can_queue, ata_id_queue_depth(dev->id));
		depth = min(ATA_MAX_QUEUE - 1, depth);
		scsi_adjust_queue_depth(sdev, MSG_SIMPLE_TAG, depth);
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
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev = __ata_scsi_find_dev(ap, sdev);

	ata_scsi_sdev_config(sdev);

	sdev->manage_start_stop = 1;

	if (dev)
		ata_scsi_dev_config(sdev, dev);

	return 0;	/* scsi layer doesn't check return value, sigh */
}

/**
 *	ata_scsi_slave_destroy - SCSI device is about to be destroyed
 *	@sdev: SCSI device to be destroyed
 *
 *	@sdev is about to be destroyed for hot/warm unplugging.  If
 *	this unplugging was initiated by libata as indicated by NULL
 *	dev->sdev, this function doesn't have to do anything.
 *	Otherwise, SCSI layer initiated warm-unplug is in progress.
 *	Clear dev->sdev, schedule the device for ATA detach and invoke
 *	EH.
 *
 *	LOCKING:
 *	Defined by SCSI layer.  We don't really care.
 */
void ata_scsi_slave_destroy(struct scsi_device *sdev)
{
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	unsigned long flags;
	struct ata_device *dev;

	if (!ap->ops->error_handler)
		return;

	spin_lock_irqsave(ap->lock, flags);
	dev = __ata_scsi_find_dev(ap, sdev);
	if (dev && dev->sdev) {
		/* SCSI device already in CANCEL state, no need to offline it */
		dev->sdev = NULL;
		dev->flags |= ATA_DFLAG_DETACH;
		ata_port_schedule_eh(ap);
	}
	spin_unlock_irqrestore(ap->lock, flags);
}

/**
 *	ata_scsi_change_queue_depth - SCSI callback for queue depth config
 *	@sdev: SCSI device to configure queue depth for
 *	@queue_depth: new queue depth
 *
 *	This is libata standard hostt->change_queue_depth callback.
 *	SCSI will call into this callback when user tries to set queue
 *	depth via sysfs.
 *
 *	LOCKING:
 *	SCSI layer (we don't care)
 *
 *	RETURNS:
 *	Newly configured queue depth.
 */
int ata_scsi_change_queue_depth(struct scsi_device *sdev, int queue_depth)
{
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev;
	unsigned long flags;

	if (queue_depth < 1 || queue_depth == sdev->queue_depth)
		return sdev->queue_depth;

	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev || !ata_dev_enabled(dev))
		return sdev->queue_depth;

	/* NCQ enabled? */
	spin_lock_irqsave(ap->lock, flags);
	dev->flags &= ~ATA_DFLAG_NCQ_OFF;
	if (queue_depth == 1 || !ata_ncq_enabled(dev)) {
		dev->flags |= ATA_DFLAG_NCQ_OFF;
		queue_depth = 1;
	}
	spin_unlock_irqrestore(ap->lock, flags);

	/* limit and apply queue depth */
	queue_depth = min(queue_depth, sdev->host->can_queue);
	queue_depth = min(queue_depth, ata_id_queue_depth(dev->id));
	queue_depth = min(queue_depth, ATA_MAX_QUEUE - 1);

	if (sdev->queue_depth == queue_depth)
		return -EINVAL;

	scsi_adjust_queue_depth(sdev, MSG_SIMPLE_TAG, queue_depth);
	return queue_depth;
}

/* XXX: for spindown warning */
static void ata_delayed_done_timerfn(unsigned long arg)
{
	struct scsi_cmnd *scmd = (void *)arg;

	scmd->scsi_done(scmd);
}

/* XXX: for spindown warning */
static void ata_delayed_done(struct scsi_cmnd *scmd)
{
	static struct timer_list timer;

	setup_timer(&timer, ata_delayed_done_timerfn, (unsigned long)scmd);
	mod_timer(&timer, jiffies + 5 * HZ);
}

/**
 *	ata_scsi_start_stop_xlat - Translate SCSI START STOP UNIT command
 *	@qc: Storage for translated ATA taskfile
 *
 *	Sets up an ATA taskfile to issue STANDBY (to stop) or READ VERIFY
 *	(to start). Perhaps these commands should be preceded by
 *	CHECK POWER MODE to see what power mode the device is already in.
 *	[See SAT revision 5 at www.t10.org]
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */
static unsigned int ata_scsi_start_stop_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct ata_taskfile *tf = &qc->tf;
	const u8 *cdb = scmd->cmnd;

	if (scmd->cmd_len < 5)
		goto invalid_fld;

	tf->flags |= ATA_TFLAG_DEVICE | ATA_TFLAG_ISADDR;
	tf->protocol = ATA_PROT_NODATA;
	if (cdb[1] & 0x1) {
		;	/* ignore IMMED bit, violates sat-r05 */
	}
	if (cdb[4] & 0x2)
		goto invalid_fld;       /* LOEJ bit set not supported */
	if (((cdb[4] >> 4) & 0xf) != 0)
		goto invalid_fld;       /* power conditions not supported */

	if (qc->dev->horkage & ATA_HORKAGE_SKIP_PM) {
		/* the device lacks PM support, finish without doing anything */
		scmd->result = SAM_STAT_GOOD;
		return 1;
	}

	if (cdb[4] & 0x1) {
		tf->nsect = 1;	/* 1 sector, lba=0 */

		if (qc->dev->flags & ATA_DFLAG_LBA) {
			tf->flags |= ATA_TFLAG_LBA;

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
		/* XXX: This is for backward compatibility, will be
		 * removed.  Read Documentation/feature-removal-schedule.txt
		 * for more info.
		 */
		if ((qc->dev->flags & ATA_DFLAG_SPUNDOWN) &&
		    (system_state == SYSTEM_HALT ||
		     system_state == SYSTEM_POWER_OFF)) {
			static unsigned long warned;

			if (!test_and_set_bit(0, &warned)) {
				ata_dev_printk(qc->dev, KERN_WARNING,
					"DISK MIGHT NOT BE SPUN DOWN PROPERLY. "
					"UPDATE SHUTDOWN UTILITY\n");
				ata_dev_printk(qc->dev, KERN_WARNING,
					"For more info, visit "
					"http://linux-ata.org/shutdown.html\n");

				/* ->scsi_done is not used, use it for
				 * delayed completion.
				 */
				scmd->scsi_done = qc->scsidone;
				qc->scsidone = ata_delayed_done;
			}
			scmd->result = SAM_STAT_GOOD;
			return 1;
		}

		/* Issue ATA STANDBY IMMEDIATE command */
		tf->command = ATA_CMD_STANDBYNOW1;
	}

	/*
	 * Standby and Idle condition timers could be implemented but that
	 * would require libata to implement the Power condition mode page
	 * and allow the user to change it. Changing mode pages requires
	 * MODE SELECT to be implemented.
	 */

	return 0;

invalid_fld:
	ata_scsi_set_sense(scmd, ILLEGAL_REQUEST, 0x24, 0x0);
	/* "Invalid field in cbd" */
	return 1;
}


/**
 *	ata_scsi_flush_xlat - Translate SCSI SYNCHRONIZE CACHE command
 *	@qc: Storage for translated ATA taskfile
 *
 *	Sets up an ATA taskfile to issue FLUSH CACHE or
 *	FLUSH CACHE EXT.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */
static unsigned int ata_scsi_flush_xlat(struct ata_queued_cmd *qc)
{
	struct ata_taskfile *tf = &qc->tf;

	tf->flags |= ATA_TFLAG_DEVICE;
	tf->protocol = ATA_PROT_NODATA;

	if (qc->dev->flags & ATA_DFLAG_FLUSH_EXT)
		tf->command = ATA_CMD_FLUSH_EXT;
	else
		tf->command = ATA_CMD_FLUSH;

	/* flush is critical for IO integrity, consider it an IO command */
	qc->flags |= ATA_QCFLAG_IO;

	return 0;
}

/**
 *	scsi_6_lba_len - Get LBA and transfer length
 *	@cdb: SCSI command to translate
 *
 *	Calculate LBA and transfer length for 6-byte commands.
 *
 *	RETURNS:
 *	@plba: the LBA
 *	@plen: the transfer length
 */
static void scsi_6_lba_len(const u8 *cdb, u64 *plba, u32 *plen)
{
	u64 lba = 0;
	u32 len;

	VPRINTK("six-byte command\n");

	lba |= ((u64)(cdb[1] & 0x1f)) << 16;
	lba |= ((u64)cdb[2]) << 8;
	lba |= ((u64)cdb[3]);

	len = cdb[4];

	*plba = lba;
	*plen = len;
}

/**
 *	scsi_10_lba_len - Get LBA and transfer length
 *	@cdb: SCSI command to translate
 *
 *	Calculate LBA and transfer length for 10-byte commands.
 *
 *	RETURNS:
 *	@plba: the LBA
 *	@plen: the transfer length
 */
static void scsi_10_lba_len(const u8 *cdb, u64 *plba, u32 *plen)
{
	u64 lba = 0;
	u32 len = 0;

	VPRINTK("ten-byte command\n");

	lba |= ((u64)cdb[2]) << 24;
	lba |= ((u64)cdb[3]) << 16;
	lba |= ((u64)cdb[4]) << 8;
	lba |= ((u64)cdb[5]);

	len |= ((u32)cdb[7]) << 8;
	len |= ((u32)cdb[8]);

	*plba = lba;
	*plen = len;
}

/**
 *	scsi_16_lba_len - Get LBA and transfer length
 *	@cdb: SCSI command to translate
 *
 *	Calculate LBA and transfer length for 16-byte commands.
 *
 *	RETURNS:
 *	@plba: the LBA
 *	@plen: the transfer length
 */
static void scsi_16_lba_len(const u8 *cdb, u64 *plba, u32 *plen)
{
	u64 lba = 0;
	u32 len = 0;

	VPRINTK("sixteen-byte command\n");

	lba |= ((u64)cdb[2]) << 56;
	lba |= ((u64)cdb[3]) << 48;
	lba |= ((u64)cdb[4]) << 40;
	lba |= ((u64)cdb[5]) << 32;
	lba |= ((u64)cdb[6]) << 24;
	lba |= ((u64)cdb[7]) << 16;
	lba |= ((u64)cdb[8]) << 8;
	lba |= ((u64)cdb[9]);

	len |= ((u32)cdb[10]) << 24;
	len |= ((u32)cdb[11]) << 16;
	len |= ((u32)cdb[12]) << 8;
	len |= ((u32)cdb[13]);

	*plba = lba;
	*plen = len;
}

/**
 *	ata_scsi_verify_xlat - Translate SCSI VERIFY command into an ATA one
 *	@qc: Storage for translated ATA taskfile
 *
 *	Converts SCSI VERIFY command to an ATA READ VERIFY command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */
static unsigned int ata_scsi_verify_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct ata_taskfile *tf = &qc->tf;
	struct ata_device *dev = qc->dev;
	u64 dev_sectors = qc->dev->n_sectors;
	const u8 *cdb = scmd->cmnd;
	u64 block;
	u32 n_block;

	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf->protocol = ATA_PROT_NODATA;

	if (cdb[0] == VERIFY) {
		if (scmd->cmd_len < 10)
			goto invalid_fld;
		scsi_10_lba_len(cdb, &block, &n_block);
	} else if (cdb[0] == VERIFY_16) {
		if (scmd->cmd_len < 16)
			goto invalid_fld;
		scsi_16_lba_len(cdb, &block, &n_block);
	} else
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
	ata_scsi_set_sense(scmd, ILLEGAL_REQUEST, 0x24, 0x0);
	/* "Invalid field in cbd" */
	return 1;

out_of_range:
	ata_scsi_set_sense(scmd, ILLEGAL_REQUEST, 0x21, 0x0);
	/* "Logical Block Address out of range" */
	return 1;

nothing_to_do:
	scmd->result = SAM_STAT_GOOD;
	return 1;
}

/**
 *	ata_scsi_rw_xlat - Translate SCSI r/w command into an ATA one
 *	@qc: Storage for translated ATA taskfile
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
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */
static unsigned int ata_scsi_rw_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	const u8 *cdb = scmd->cmnd;
	unsigned int tf_flags = 0;
	u64 block;
	u32 n_block;
	int rc;

	if (cdb[0] == WRITE_10 || cdb[0] == WRITE_6 || cdb[0] == WRITE_16)
		tf_flags |= ATA_TFLAG_WRITE;

	/* Calculate the SCSI LBA, transfer length and FUA. */
	switch (cdb[0]) {
	case READ_10:
	case WRITE_10:
		if (unlikely(scmd->cmd_len < 10))
			goto invalid_fld;
		scsi_10_lba_len(cdb, &block, &n_block);
		if (unlikely(cdb[1] & (1 << 3)))
			tf_flags |= ATA_TFLAG_FUA;
		break;
	case READ_6:
	case WRITE_6:
		if (unlikely(scmd->cmd_len < 6))
			goto invalid_fld;
		scsi_6_lba_len(cdb, &block, &n_block);

		/* for 6-byte r/w commands, transfer length 0
		 * means 256 blocks of data, not 0 block.
		 */
		if (!n_block)
			n_block = 256;
		break;
	case READ_16:
	case WRITE_16:
		if (unlikely(scmd->cmd_len < 16))
			goto invalid_fld;
		scsi_16_lba_len(cdb, &block, &n_block);
		if (unlikely(cdb[1] & (1 << 3)))
			tf_flags |= ATA_TFLAG_FUA;
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

	qc->flags |= ATA_QCFLAG_IO;
	qc->nbytes = n_block * ATA_SECT_SIZE;

	rc = ata_build_rw_tf(&qc->tf, qc->dev, block, n_block, tf_flags,
			     qc->tag);
	if (likely(rc == 0))
		return 0;

	if (rc == -ERANGE)
		goto out_of_range;
	/* treat all other errors as -EINVAL, fall through */
invalid_fld:
	ata_scsi_set_sense(scmd, ILLEGAL_REQUEST, 0x24, 0x0);
	/* "Invalid field in cbd" */
	return 1;

out_of_range:
	ata_scsi_set_sense(scmd, ILLEGAL_REQUEST, 0x21, 0x0);
	/* "Logical Block Address out of range" */
	return 1;

nothing_to_do:
	scmd->result = SAM_STAT_GOOD;
	return 1;
}

static void ata_scsi_qc_complete(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
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
		ata_gen_passthru_sense(qc);
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
			ata_gen_ata_sense(qc);
		}
	}

	/* XXX: track spindown state for spindown skipping and warning */
	if (unlikely(qc->tf.command == ATA_CMD_STANDBY ||
		     qc->tf.command == ATA_CMD_STANDBYNOW1))
		qc->dev->flags |= ATA_DFLAG_SPUNDOWN;
	else if (likely(system_state != SYSTEM_HALT &&
			system_state != SYSTEM_POWER_OFF))
		qc->dev->flags &= ~ATA_DFLAG_SPUNDOWN;

	if (need_sense && !ap->ops->error_handler)
		ata_dump_status(ap->print_id, &qc->result_tf);

	qc->scsidone(cmd);

	ata_qc_free(qc);
}

/**
 *	ata_scsi_translate - Translate then issue SCSI command to ATA device
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
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	0 on success, SCSI_ML_QUEUE_DEVICE_BUSY if the command
 *	needs to be deferred.
 */
static int ata_scsi_translate(struct ata_device *dev, struct scsi_cmnd *cmd,
			      void (*done)(struct scsi_cmnd *),
			      ata_xlat_func_t xlat_func)
{
	struct ata_port *ap = dev->link->ap;
	struct ata_queued_cmd *qc;
	int rc;

	VPRINTK("ENTER\n");

	qc = ata_scsi_qc_new(dev, cmd, done);
	if (!qc)
		goto err_mem;

	/* data is present; dma-map it */
	if (cmd->sc_data_direction == DMA_FROM_DEVICE ||
	    cmd->sc_data_direction == DMA_TO_DEVICE) {
		if (unlikely(scsi_bufflen(cmd) < 1)) {
			ata_dev_printk(dev, KERN_WARNING,
				       "WARNING: zero len r/w req\n");
			goto err_did;
		}

		ata_sg_init(qc, scsi_sglist(cmd), scsi_sg_count(cmd));

		qc->dma_dir = cmd->sc_data_direction;
	}

	qc->complete_fn = ata_scsi_qc_complete;

	if (xlat_func(qc))
		goto early_finish;

	if (ap->ops->qc_defer) {
		if ((rc = ap->ops->qc_defer(qc)))
			goto defer;
	}

	/* select device, send command to hardware */
	ata_qc_issue(qc);

	VPRINTK("EXIT\n");
	return 0;

early_finish:
	ata_qc_free(qc);
	qc->scsidone(cmd);
	DPRINTK("EXIT - early finish (good or error)\n");
	return 0;

err_did:
	ata_qc_free(qc);
	cmd->result = (DID_ERROR << 16);
	qc->scsidone(cmd);
err_mem:
	DPRINTK("EXIT - internal\n");
	return 0;

defer:
	ata_qc_free(qc);
	DPRINTK("EXIT - defer\n");
	if (rc == ATA_DEFER_LINK)
		return SCSI_MLQUEUE_DEVICE_BUSY;
	else
		return SCSI_MLQUEUE_HOST_BUSY;
}

/**
 *	ata_scsi_rbuf_get - Map response buffer.
 *	@cmd: SCSI command containing buffer to be mapped.
 *	@buf_out: Pointer to mapped area.
 *
 *	Maps buffer contained within SCSI command @cmd.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Length of response buffer.
 */

static unsigned int ata_scsi_rbuf_get(struct scsi_cmnd *cmd, u8 **buf_out)
{
	u8 *buf;
	unsigned int buflen;

	struct scatterlist *sg = scsi_sglist(cmd);

	if (sg) {
		buf = kmap_atomic(sg_page(sg), KM_IRQ0) + sg->offset;
		buflen = sg->length;
	} else {
		buf = NULL;
		buflen = 0;
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
 *	spin_lock_irqsave(host lock)
 */

static inline void ata_scsi_rbuf_put(struct scsi_cmnd *cmd, u8 *buf)
{
	struct scatterlist *sg = scsi_sglist(cmd);
	if (sg)
		kunmap_atomic(buf - sg->offset, KM_IRQ0);
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
 *	spin_lock_irqsave(host lock)
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
 *	ATA_SCSI_RBUF_SET - helper to set values in SCSI response buffer
 *	@idx: byte index into SCSI response buffer
 *	@val: value to set
 *
 *	To be used by SCSI command simulator functions.  This macros
 *	expects two local variables, u8 *rbuf and unsigned int buflen,
 *	are in scope.
 *
 *	LOCKING:
 *	None.
 */
#define ATA_SCSI_RBUF_SET(idx, val) do { \
		if ((idx) < buflen) rbuf[(idx)] = (u8)(val); \
	} while (0)

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
 *	spin_lock_irqsave(host lock)
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
		ata_id_string(args->id, &rbuf[16], ATA_ID_PROD, 16);
		ata_id_string(args->id, &rbuf[32], ATA_ID_FW_REV, 4);
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
 *	spin_lock_irqsave(host lock)
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
 *	spin_lock_irqsave(host lock)
 */

unsigned int ata_scsiop_inq_80(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen)
{
	const u8 hdr[] = {
		0,
		0x80,			/* this page code */
		0,
		ATA_ID_SERNO_LEN,	/* page len */
	};
	memcpy(rbuf, hdr, sizeof(hdr));

	if (buflen > (ATA_ID_SERNO_LEN + 4 - 1))
		ata_id_string(args->id, (unsigned char *) &rbuf[4],
			      ATA_ID_SERNO, ATA_ID_SERNO_LEN);

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
 *	spin_lock_irqsave(host lock)
 */

unsigned int ata_scsiop_inq_83(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen)
{
	int num;
	const int sat_model_serial_desc_len = 68;

	rbuf[1] = 0x83;			/* this page code */
	num = 4;

	if (buflen > (ATA_ID_SERNO_LEN + num + 3)) {
		/* piv=0, assoc=lu, code_set=ACSII, designator=vendor */
		rbuf[num + 0] = 2;
		rbuf[num + 3] = ATA_ID_SERNO_LEN;
		num += 4;
		ata_id_string(args->id, (unsigned char *) rbuf + num,
			      ATA_ID_SERNO, ATA_ID_SERNO_LEN);
		num += ATA_ID_SERNO_LEN;
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
			      ATA_ID_PROD, ATA_ID_PROD_LEN);
		num += ATA_ID_PROD_LEN;
		ata_id_string(args->id, (unsigned char *) rbuf + num,
			      ATA_ID_SERNO, ATA_ID_SERNO_LEN);
		num += ATA_ID_SERNO_LEN;
	}
	rbuf[3] = num - 4;    /* page len (assume less than 256 bytes) */
	return 0;
}

/**
 *	ata_scsiop_inq_89 - Simulate INQUIRY VPD page 89, ATA info
 *	@args: device IDENTIFY data / SCSI command of interest.
 *	@rbuf: Response buffer, to which simulated SCSI cmd output is sent.
 *	@buflen: Response buffer length.
 *
 *	Yields SAT-specified ATA VPD page.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */

unsigned int ata_scsiop_inq_89(struct ata_scsi_args *args, u8 *rbuf,
			      unsigned int buflen)
{
	u8 pbuf[60];
	struct ata_taskfile tf;
	unsigned int i;

	if (!buflen)
		return 0;

	memset(&pbuf, 0, sizeof(pbuf));
	memset(&tf, 0, sizeof(tf));

	pbuf[1] = 0x89;			/* our page code */
	pbuf[2] = (0x238 >> 8);		/* page size fixed at 238h */
	pbuf[3] = (0x238 & 0xff);

	memcpy(&pbuf[8], "linux   ", 8);
	memcpy(&pbuf[16], "libata          ", 16);
	memcpy(&pbuf[32], DRV_VERSION, 4);
	ata_id_string(args->id, &pbuf[32], ATA_ID_FW_REV, 4);

	/* we don't store the ATA device signature, so we fake it */

	tf.command = ATA_DRDY;		/* really, this is Status reg */
	tf.lbal = 0x1;
	tf.nsect = 0x1;

	ata_tf_to_fis(&tf, 0, 1, &pbuf[36]);	/* TODO: PMP? */
	pbuf[36] = 0x34;		/* force D2H Reg FIS (34h) */

	pbuf[56] = ATA_CMD_ID_ATA;

	i = min(buflen, 60U);
	memcpy(rbuf, &pbuf[0], i);
	buflen -= i;

	if (!buflen)
		return 0;

	memcpy(&rbuf[60], &args->id[0], min(buflen, 512U));
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
 *	spin_lock_irqsave(host lock)
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
	unsigned char model[ATA_ID_PROD_LEN + 1], fw[ATA_ID_FW_REV_LEN + 1];

	if (!libata_fua)
		return 0;
	if (!ata_id_has_fua(id))
		return 0;

	ata_id_c_string(id, model, ATA_ID_PROD, sizeof(model));
	ata_id_c_string(id, fw, ATA_ID_FW_REV, sizeof(fw));

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
 *	spin_lock_irqsave(host lock)
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
	if (ata_dev_supports_fua(args->id) && (dev->flags & ATA_DFLAG_LBA48) &&
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
 *	None.
 */
unsigned int ata_scsiop_read_cap(struct ata_scsi_args *args, u8 *rbuf,
				 unsigned int buflen)
{
	u64 last_lba = args->dev->n_sectors - 1; /* LBA of the last block */

	VPRINTK("ENTER\n");

	if (args->cmd->cmnd[0] == READ_CAPACITY) {
		if (last_lba >= 0xffffffffULL)
			last_lba = 0xffffffff;

		/* sector count, 32-bit */
		ATA_SCSI_RBUF_SET(0, last_lba >> (8 * 3));
		ATA_SCSI_RBUF_SET(1, last_lba >> (8 * 2));
		ATA_SCSI_RBUF_SET(2, last_lba >> (8 * 1));
		ATA_SCSI_RBUF_SET(3, last_lba);

		/* sector size */
		ATA_SCSI_RBUF_SET(6, ATA_SECT_SIZE >> 8);
		ATA_SCSI_RBUF_SET(7, ATA_SECT_SIZE);
	} else {
		/* sector count, 64-bit */
		ATA_SCSI_RBUF_SET(0, last_lba >> (8 * 7));
		ATA_SCSI_RBUF_SET(1, last_lba >> (8 * 6));
		ATA_SCSI_RBUF_SET(2, last_lba >> (8 * 5));
		ATA_SCSI_RBUF_SET(3, last_lba >> (8 * 4));
		ATA_SCSI_RBUF_SET(4, last_lba >> (8 * 3));
		ATA_SCSI_RBUF_SET(5, last_lba >> (8 * 2));
		ATA_SCSI_RBUF_SET(6, last_lba >> (8 * 1));
		ATA_SCSI_RBUF_SET(7, last_lba);

		/* sector size */
		ATA_SCSI_RBUF_SET(10, ATA_SECT_SIZE >> 8);
		ATA_SCSI_RBUF_SET(11, ATA_SECT_SIZE);
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
 *	spin_lock_irqsave(host lock)
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
 *	spin_lock_irqsave(host lock)
 */

void ata_scsi_badcmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *), u8 asc, u8 ascq)
{
	DPRINTK("ENTER\n");
	ata_scsi_set_sense(cmd, ILLEGAL_REQUEST, asc, ascq);

	done(cmd);
}

static void atapi_sense_complete(struct ata_queued_cmd *qc)
{
	if (qc->err_mask && ((qc->err_mask & AC_ERR_DEV) == 0)) {
		/* FIXME: not quite right; we don't want the
		 * translation of taskfile registers into
		 * a sense descriptors, since that's only
		 * correct for ATA, not ATAPI
		 */
		ata_gen_passthru_sense(qc);
	}

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
		qc->tf.lbam = SCSI_SENSE_BUFFERSIZE;
		qc->tf.lbah = 0;
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

	/* handle completion from new EH */
	if (unlikely(qc->ap->ops->error_handler &&
		     (err_mask || qc->flags & ATA_QCFLAG_SENSE_VALID))) {

		if (!(qc->flags & ATA_QCFLAG_SENSE_VALID)) {
			/* FIXME: not quite right; we don't want the
			 * translation of taskfile registers into a
			 * sense descriptors, since that's only
			 * correct for ATA, not ATAPI
			 */
			ata_gen_passthru_sense(qc);
		}

		/* SCSI EH automatically locks door if sdev->locked is
		 * set.  Sometimes door lock request continues to
		 * fail, for example, when no media is present.  This
		 * creates a loop - SCSI EH issues door lock which
		 * fails and gets invoked again to acquire sense data
		 * for the failed command.
		 *
		 * If door lock fails, always clear sdev->locked to
		 * avoid this infinite loop.
		 */
		if (qc->cdb[0] == ALLOW_MEDIUM_REMOVAL)
			qc->dev->sdev->locked = 0;

		qc->scsicmd->result = SAM_STAT_CHECK_CONDITION;
		qc->scsidone(cmd);
		ata_qc_free(qc);
		return;
	}

	/* successful completion or old EH failure path */
	if (unlikely(err_mask & AC_ERR_DEV)) {
		cmd->result = SAM_STAT_CHECK_CONDITION;
		atapi_request_sense(qc);
		return;
	} else if (unlikely(err_mask)) {
		/* FIXME: not quite right; we don't want the
		 * translation of taskfile registers into
		 * a sense descriptors, since that's only
		 * correct for ATA, not ATAPI
		 */
		ata_gen_passthru_sense(qc);
	} else {
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
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, non-zero on failure.
 */
static unsigned int atapi_xlat(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct ata_device *dev = qc->dev;
	int using_pio = (dev->flags & ATA_DFLAG_PIO);
	int nodata = (scmd->sc_data_direction == DMA_NONE);
	unsigned int nbytes;

	memset(qc->cdb, 0, dev->cdb_len);
	memcpy(qc->cdb, scmd->cmnd, scmd->cmd_len);

	qc->complete_fn = atapi_qc_complete;

	qc->tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	if (scmd->sc_data_direction == DMA_TO_DEVICE) {
		qc->tf.flags |= ATA_TFLAG_WRITE;
		DPRINTK("direction: write\n");
	}

	qc->tf.command = ATA_CMD_PACKET;
	qc->nbytes = scsi_bufflen(scmd);

	/* check whether ATAPI DMA is safe */
	if (!using_pio && ata_check_atapi_dma(qc))
		using_pio = 1;

	/* Some controller variants snoop this value for Packet transfers
	   to do state machine and FIFO management. Thus we want to set it
	   properly, and for DMA where it is effectively meaningless */
	nbytes = min(qc->nbytes, (unsigned int)63 * 1024);

	qc->tf.lbam = (nbytes & 0xFF);
	qc->tf.lbah = (nbytes >> 8);

	if (using_pio || nodata) {
		/* no data, or PIO data xfer */
		if (nodata)
			qc->tf.protocol = ATA_PROT_ATAPI_NODATA;
		else
			qc->tf.protocol = ATA_PROT_ATAPI;
	} else {
		/* DMA data xfer */
		qc->tf.protocol = ATA_PROT_ATAPI_DMA;
		qc->tf.feature |= ATAPI_PKT_DMA;

		if (atapi_dmadir && (scmd->sc_data_direction != DMA_TO_DEVICE))
			/* some SATA bridges need us to indicate data xfer direction */
			qc->tf.feature |= ATAPI_DMADIR;
	}


	/* FIXME: We need to translate 0x05 READ_BLOCK_LIMITS to a MODE_SENSE
	   as ATAPI tape drives don't get this right otherwise */
	return 0;
}

static struct ata_device *ata_find_dev(struct ata_port *ap, int devno)
{
	if (ap->nr_pmp_links == 0) {
		if (likely(devno < ata_link_max_devices(&ap->link)))
			return &ap->link.device[devno];
	} else {
		if (likely(devno < ap->nr_pmp_links))
			return &ap->pmp_link[devno].device[0];
	}

	return NULL;
}

static struct ata_device *__ata_scsi_find_dev(struct ata_port *ap,
					      const struct scsi_device *scsidev)
{
	int devno;

	/* skip commands not addressed to targets we simulate */
	if (ap->nr_pmp_links == 0) {
		if (unlikely(scsidev->channel || scsidev->lun))
			return NULL;
		devno = scsidev->id;
	} else {
		if (unlikely(scsidev->id || scsidev->lun))
			return NULL;
		devno = scsidev->channel;
	}

	return ata_find_dev(ap, devno);
}

/**
 *	ata_scsi_dev_enabled - determine if device is enabled
 *	@dev: ATA device
 *
 *	Determine if commands should be sent to the specified device.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	0 if commands are not allowed / 1 if commands are allowed
 */

static int ata_scsi_dev_enabled(struct ata_device *dev)
{
	if (unlikely(!ata_dev_enabled(dev)))
		return 0;

	if (!atapi_enabled || (dev->link->ap->flags & ATA_FLAG_NO_ATAPI)) {
		if (unlikely(dev->class == ATA_DEV_ATAPI)) {
			ata_dev_printk(dev, KERN_WARNING,
				       "WARNING: ATAPI is %s, device ignored.\n",
				       atapi_enabled ? "not supported with this driver" : "disabled");
			return 0;
		}
	}

	return 1;
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
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Associated ATA device, or %NULL if not found.
 */
static struct ata_device *
ata_scsi_find_dev(struct ata_port *ap, const struct scsi_device *scsidev)
{
	struct ata_device *dev = __ata_scsi_find_dev(ap, scsidev);

	if (unlikely(!dev || !ata_scsi_dev_enabled(dev)))
		return NULL;

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
	case 10:	/* UDMA Data-in */
	case 11:	/* UDMA Data-Out */
		return ATA_PROT_DMA;

	case 4:		/* PIO Data-in */
	case 5:		/* PIO Data-out */
		return ATA_PROT_PIO;

	case 0:		/* Hard Reset */
	case 1:		/* SRST */
	case 8:		/* Device Diagnostic */
	case 9:		/* Device Reset */
	case 7:		/* DMA Queued */
	case 12:	/* FPDMA */
	case 15:	/* Return Response Info */
	default:	/* Reserved */
		break;
	}

	return ATA_PROT_UNKNOWN;
}

/**
 *	ata_scsi_pass_thru - convert ATA pass-thru CDB to taskfile
 *	@qc: command structure to be initialized
 *
 *	Handles either 12 or 16-byte versions of the CDB.
 *
 *	RETURNS:
 *	Zero on success, non-zero on failure.
 */
static unsigned int ata_scsi_pass_thru(struct ata_queued_cmd *qc)
{
	struct ata_taskfile *tf = &(qc->tf);
	struct scsi_cmnd *scmd = qc->scsicmd;
	struct ata_device *dev = qc->dev;
	const u8 *cdb = scmd->cmnd;

	if ((tf->protocol = ata_scsi_map_proto(cdb[1])) == ATA_PROT_UNKNOWN)
		goto invalid_fld;

	/* We may not issue DMA commands if no DMA mode is set */
	if (tf->protocol == ATA_PROT_DMA && dev->dma_mode == 0)
		goto invalid_fld;

	/*
	 * 12 and 16 byte CDBs use different offsets to
	 * provide the various register values.
	 */
	if (cdb[0] == ATA_16) {
		/*
		 * 16-byte CDB - may contain extended commands.
		 *
		 * If that is the case, copy the upper byte register values.
		 */
		if (cdb[1] & 0x01) {
			tf->hob_feature = cdb[3];
			tf->hob_nsect = cdb[5];
			tf->hob_lbal = cdb[7];
			tf->hob_lbam = cdb[9];
			tf->hob_lbah = cdb[11];
			tf->flags |= ATA_TFLAG_LBA48;
		} else
			tf->flags &= ~ATA_TFLAG_LBA48;

		/*
		 * Always copy low byte, device and command registers.
		 */
		tf->feature = cdb[4];
		tf->nsect = cdb[6];
		tf->lbal = cdb[8];
		tf->lbam = cdb[10];
		tf->lbah = cdb[12];
		tf->device = cdb[13];
		tf->command = cdb[14];
	} else {
		/*
		 * 12-byte CDB - incapable of extended commands.
		 */
		tf->flags &= ~ATA_TFLAG_LBA48;

		tf->feature = cdb[3];
		tf->nsect = cdb[4];
		tf->lbal = cdb[5];
		tf->lbam = cdb[6];
		tf->lbah = cdb[7];
		tf->device = cdb[8];
		tf->command = cdb[9];
	}

	/* enforce correct master/slave bit */
	tf->device = dev->devno ?
		tf->device | ATA_DEV1 : tf->device & ~ATA_DEV1;

	/* sanity check for pio multi commands */
	if ((cdb[1] & 0xe0) && !is_multi_taskfile(tf))
		goto invalid_fld;

	if (is_multi_taskfile(tf)) {
		unsigned int multi_count = 1 << (cdb[1] >> 5);

		/* compare the passed through multi_count
		 * with the cached multi_count of libata
		 */
		if (multi_count != dev->multi_count)
			ata_dev_printk(dev, KERN_WARNING,
				       "invalid multi_count %u ignored\n",
				       multi_count);
	}

	/* READ/WRITE LONG use a non-standard sect_size */
	qc->sect_size = ATA_SECT_SIZE;
	switch (tf->command) {
	case ATA_CMD_READ_LONG:
	case ATA_CMD_READ_LONG_ONCE:
	case ATA_CMD_WRITE_LONG:
	case ATA_CMD_WRITE_LONG_ONCE:
		if (tf->protocol != ATA_PROT_PIO || tf->nsect != 1)
			goto invalid_fld;
		qc->sect_size = scsi_bufflen(scmd);
	}

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

	if (scmd->sc_data_direction == DMA_TO_DEVICE)
		tf->flags |= ATA_TFLAG_WRITE;

	/*
	 * Set transfer length.
	 *
	 * TODO: find out if we need to do more here to
	 *       cover scatter/gather case.
	 */
	qc->nbytes = scsi_bufflen(scmd);

	/* request result TF and be quiet about device error */
	qc->flags |= ATA_QCFLAG_RESULT_TF | ATA_QCFLAG_QUIET;

	return 0;

 invalid_fld:
	ata_scsi_set_sense(scmd, ILLEGAL_REQUEST, 0x24, 0x00);
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
		ap->print_id,
		scsidev->channel, scsidev->id, scsidev->lun,
		scsicmd[0], scsicmd[1], scsicmd[2], scsicmd[3],
		scsicmd[4], scsicmd[5], scsicmd[6], scsicmd[7],
		scsicmd[8]);
#endif
}

static inline int __ata_scsi_queuecmd(struct scsi_cmnd *scmd,
				      void (*done)(struct scsi_cmnd *),
				      struct ata_device *dev)
{
	u8 scsi_op = scmd->cmnd[0];
	ata_xlat_func_t xlat_func;
	int rc = 0;

	if (dev->class == ATA_DEV_ATA) {
		if (unlikely(!scmd->cmd_len || scmd->cmd_len > dev->cdb_len))
			goto bad_cdb_len;

		xlat_func = ata_get_xlat_func(dev, scsi_op);
	} else {
		if (unlikely(!scmd->cmd_len))
			goto bad_cdb_len;

		xlat_func = NULL;
		if (likely((scsi_op != ATA_16) || !atapi_passthru16)) {
			/* relay SCSI command to ATAPI device */
			if (unlikely(scmd->cmd_len > dev->cdb_len))
				goto bad_cdb_len;

			xlat_func = atapi_xlat;
		} else {
			/* ATA_16 passthru, treat as an ATA command */
			if (unlikely(scmd->cmd_len > 16))
				goto bad_cdb_len;

			xlat_func = ata_get_xlat_func(dev, scsi_op);
		}
	}

	if (xlat_func)
		rc = ata_scsi_translate(dev, scmd, done, xlat_func);
	else
		ata_scsi_simulate(dev, scmd, done);

	return rc;

 bad_cdb_len:
	DPRINTK("bad CDB len=%u, scsi_op=0x%02x, max=%u\n",
		scmd->cmd_len, scsi_op, dev->cdb_len);
	scmd->result = DID_ERROR << 16;
	done(scmd);
	return 0;
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
 *	Releases scsi-layer-held lock, and obtains host lock.
 *
 *	RETURNS:
 *	Return value from __ata_scsi_queuecmd() if @cmd can be queued,
 *	0 otherwise.
 */
int ata_scsi_queuecmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	struct ata_port *ap;
	struct ata_device *dev;
	struct scsi_device *scsidev = cmd->device;
	struct Scsi_Host *shost = scsidev->host;
	int rc = 0;

	ap = ata_shost_to_port(shost);

	spin_unlock(shost->host_lock);
	spin_lock(ap->lock);

	ata_scsi_dump_cdb(ap, cmd);

	dev = ata_scsi_find_dev(ap, scsidev);
	if (likely(dev))
		rc = __ata_scsi_queuecmd(cmd, done, dev);
	else {
		cmd->result = (DID_BAD_TARGET << 16);
		done(cmd);
	}

	spin_unlock(ap->lock);
	spin_lock(shost->host_lock);
	return rc;
}

/**
 *	ata_scsi_simulate - simulate SCSI command on ATA device
 *	@dev: the target device
 *	@cmd: SCSI command being sent to device.
 *	@done: SCSI command completion function.
 *
 *	Interprets and directly executes a select list of SCSI commands
 *	that can be handled internally.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */

void ata_scsi_simulate(struct ata_device *dev, struct scsi_cmnd *cmd,
		      void (*done)(struct scsi_cmnd *))
{
	struct ata_scsi_args args;
	const u8 *scsicmd = cmd->cmnd;
	u8 tmp8;

	args.dev = dev;
	args.id = dev->id;
	args.cmd = cmd;
	args.done = done;

	switch(scsicmd[0]) {
	/* TODO: worth improving? */
	case FORMAT_UNIT:
		ata_scsi_invalid_field(cmd, done);
		break;

	case INQUIRY:
		if (scsicmd[1] & 2)	           /* is CmdDt set?  */
			ata_scsi_invalid_field(cmd, done);
		else if ((scsicmd[1] & 1) == 0)    /* is EVPD clear? */
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_std);
		else switch (scsicmd[2]) {
		case 0x00:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_00);
			break;
		case 0x80:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_80);
			break;
		case 0x83:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_83);
			break;
		case 0x89:
			ata_scsi_rbuf_fill(&args, ata_scsiop_inq_89);
			break;
		default:
			ata_scsi_invalid_field(cmd, done);
			break;
		}
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

	case REQUEST_SENSE:
		ata_scsi_set_sense(cmd, 0, 0, 0);
		cmd->result = (DRIVER_SENSE << 24);
		done(cmd);
		break;

	/* if we reach this, then writeback caching is disabled,
	 * turning this into a no-op.
	 */
	case SYNCHRONIZE_CACHE:
		/* fall through */

	/* no-op's, complete with success */
	case REZERO_UNIT:
	case SEEK_6:
	case SEEK_10:
	case TEST_UNIT_READY:
		ata_scsi_rbuf_fill(&args, ata_scsiop_noop);
		break;

	case SEND_DIAGNOSTIC:
		tmp8 = scsicmd[1] & ~(1 << 3);
		if ((tmp8 == 0x4) && (!scsicmd[3]) && (!scsicmd[4]))
			ata_scsi_rbuf_fill(&args, ata_scsiop_noop);
		else
			ata_scsi_invalid_field(cmd, done);
		break;

	/* all other commands */
	default:
		ata_scsi_set_sense(cmd, ILLEGAL_REQUEST, 0x20, 0x0);
		/* "Invalid command operation code" */
		done(cmd);
		break;
	}
}

int ata_scsi_add_hosts(struct ata_host *host, struct scsi_host_template *sht)
{
	int i, rc;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		struct Scsi_Host *shost;

		rc = -ENOMEM;
		shost = scsi_host_alloc(sht, sizeof(struct ata_port *));
		if (!shost)
			goto err_alloc;

		*(struct ata_port **)&shost->hostdata[0] = ap;
		ap->scsi_host = shost;

		shost->transportt = &ata_scsi_transport_template;
		shost->unique_id = ap->print_id;
		shost->max_id = 16;
		shost->max_lun = 1;
		shost->max_channel = 1;
		shost->max_cmd_len = 16;

		/* Schedule policy is determined by ->qc_defer()
		 * callback and it needs to see every deferred qc.
		 * Set host_blocked to 1 to prevent SCSI midlayer from
		 * automatically deferring requests.
		 */
		shost->max_host_blocked = 1;

		rc = scsi_add_host(ap->scsi_host, ap->host->dev);
		if (rc)
			goto err_add;
	}

	return 0;

 err_add:
	scsi_host_put(host->ports[i]->scsi_host);
 err_alloc:
	while (--i >= 0) {
		struct Scsi_Host *shost = host->ports[i]->scsi_host;

		scsi_remove_host(shost);
		scsi_host_put(shost);
	}
	return rc;
}

void ata_scsi_scan_host(struct ata_port *ap, int sync)
{
	int tries = 5;
	struct ata_device *last_failed_dev = NULL;
	struct ata_link *link;
	struct ata_device *dev;

	if (ap->flags & ATA_FLAG_DISABLED)
		return;

 repeat:
	ata_port_for_each_link(link, ap) {
		ata_link_for_each_dev(dev, link) {
			struct scsi_device *sdev;
			int channel = 0, id = 0;

			if (!ata_dev_enabled(dev) || dev->sdev)
				continue;

			if (ata_is_host_link(link))
				id = dev->devno;
			else
				channel = link->pmp;

			sdev = __scsi_add_device(ap->scsi_host, channel, id, 0,
						 NULL);
			if (!IS_ERR(sdev)) {
				dev->sdev = sdev;
				scsi_device_put(sdev);
			}
		}
	}

	/* If we scanned while EH was in progress or allocation
	 * failure occurred, scan would have failed silently.  Check
	 * whether all devices are attached.
	 */
	ata_port_for_each_link(link, ap) {
		ata_link_for_each_dev(dev, link) {
			if (ata_dev_enabled(dev) && !dev->sdev)
				goto exit_loop;
		}
	}
 exit_loop:
	if (!link)
		return;

	/* we're missing some SCSI devices */
	if (sync) {
		/* If caller requested synchrnous scan && we've made
		 * any progress, sleep briefly and repeat.
		 */
		if (dev != last_failed_dev) {
			msleep(100);
			last_failed_dev = dev;
			goto repeat;
		}

		/* We might be failing to detect boot device, give it
		 * a few more chances.
		 */
		if (--tries) {
			msleep(100);
			goto repeat;
		}

		ata_port_printk(ap, KERN_ERR, "WARNING: synchronous SCSI scan "
				"failed without making any progress,\n"
				"                  switching to async\n");
	}

	queue_delayed_work(ata_aux_wq, &ap->hotplug_task,
			   round_jiffies_relative(HZ));
}

/**
 *	ata_scsi_offline_dev - offline attached SCSI device
 *	@dev: ATA device to offline attached SCSI device for
 *
 *	This function is called from ata_eh_hotplug() and responsible
 *	for taking the SCSI device attached to @dev offline.  This
 *	function is called with host lock which protects dev->sdev
 *	against clearing.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	1 if attached SCSI device exists, 0 otherwise.
 */
int ata_scsi_offline_dev(struct ata_device *dev)
{
	if (dev->sdev) {
		scsi_device_set_state(dev->sdev, SDEV_OFFLINE);
		return 1;
	}
	return 0;
}

/**
 *	ata_scsi_remove_dev - remove attached SCSI device
 *	@dev: ATA device to remove attached SCSI device for
 *
 *	This function is called from ata_eh_scsi_hotplug() and
 *	responsible for removing the SCSI device attached to @dev.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
static void ata_scsi_remove_dev(struct ata_device *dev)
{
	struct ata_port *ap = dev->link->ap;
	struct scsi_device *sdev;
	unsigned long flags;

	/* Alas, we need to grab scan_mutex to ensure SCSI device
	 * state doesn't change underneath us and thus
	 * scsi_device_get() always succeeds.  The mutex locking can
	 * be removed if there is __scsi_device_get() interface which
	 * increments reference counts regardless of device state.
	 */
	mutex_lock(&ap->scsi_host->scan_mutex);
	spin_lock_irqsave(ap->lock, flags);

	/* clearing dev->sdev is protected by host lock */
	sdev = dev->sdev;
	dev->sdev = NULL;

	if (sdev) {
		/* If user initiated unplug races with us, sdev can go
		 * away underneath us after the host lock and
		 * scan_mutex are released.  Hold onto it.
		 */
		if (scsi_device_get(sdev) == 0) {
			/* The following ensures the attached sdev is
			 * offline on return from ata_scsi_offline_dev()
			 * regardless it wins or loses the race
			 * against this function.
			 */
			scsi_device_set_state(sdev, SDEV_OFFLINE);
		} else {
			WARN_ON(1);
			sdev = NULL;
		}
	}

	spin_unlock_irqrestore(ap->lock, flags);
	mutex_unlock(&ap->scsi_host->scan_mutex);

	if (sdev) {
		ata_dev_printk(dev, KERN_INFO, "detaching (SCSI %s)\n",
			       sdev->sdev_gendev.bus_id);

		scsi_remove_device(sdev);
		scsi_device_put(sdev);
	}
}

static void ata_scsi_handle_link_detach(struct ata_link *link)
{
	struct ata_port *ap = link->ap;
	struct ata_device *dev;

	ata_link_for_each_dev(dev, link) {
		unsigned long flags;

		if (!(dev->flags & ATA_DFLAG_DETACHED))
			continue;

		spin_lock_irqsave(ap->lock, flags);
		dev->flags &= ~ATA_DFLAG_DETACHED;
		spin_unlock_irqrestore(ap->lock, flags);

		ata_scsi_remove_dev(dev);
	}
}

/**
 *	ata_scsi_media_change_notify - send media change event
 *	@dev: Pointer to the disk device with media change event
 *
 *	Tell the block layer to send a media change notification
 *	event.
 *
 * 	LOCKING:
 * 	spin_lock_irqsave(host lock)
 */
void ata_scsi_media_change_notify(struct ata_device *dev)
{
#ifdef OTHER_AN_PATCHES_HAVE_BEEN_APPLIED
	if (dev->sdev)
		scsi_device_event_notify(dev->sdev, SDEV_MEDIA_CHANGE);
#endif
}

/**
 *	ata_scsi_hotplug - SCSI part of hotplug
 *	@work: Pointer to ATA port to perform SCSI hotplug on
 *
 *	Perform SCSI part of hotplug.  It's executed from a separate
 *	workqueue after EH completes.  This is necessary because SCSI
 *	hot plugging requires working EH and hot unplugging is
 *	synchronized with hot plugging with a mutex.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_scsi_hotplug(struct work_struct *work)
{
	struct ata_port *ap =
		container_of(work, struct ata_port, hotplug_task.work);
	int i;

	if (ap->pflags & ATA_PFLAG_UNLOADING) {
		DPRINTK("ENTER/EXIT - unloading\n");
		return;
	}

	DPRINTK("ENTER\n");

	/* Unplug detached devices.  We cannot use link iterator here
	 * because PMP links have to be scanned even if PMP is
	 * currently not attached.  Iterate manually.
	 */
	ata_scsi_handle_link_detach(&ap->link);
	if (ap->pmp_link)
		for (i = 0; i < SATA_PMP_MAX_PORTS; i++)
			ata_scsi_handle_link_detach(&ap->pmp_link[i]);

	/* scan for new ones */
	ata_scsi_scan_host(ap, 0);

	DPRINTK("EXIT\n");
}

/**
 *	ata_scsi_user_scan - indication for user-initiated bus scan
 *	@shost: SCSI host to scan
 *	@channel: Channel to scan
 *	@id: ID to scan
 *	@lun: LUN to scan
 *
 *	This function is called when user explicitly requests bus
 *	scan.  Set probe pending flag and invoke EH.
 *
 *	LOCKING:
 *	SCSI layer (we don't care)
 *
 *	RETURNS:
 *	Zero.
 */
static int ata_scsi_user_scan(struct Scsi_Host *shost, unsigned int channel,
			      unsigned int id, unsigned int lun)
{
	struct ata_port *ap = ata_shost_to_port(shost);
	unsigned long flags;
	int devno, rc = 0;

	if (!ap->ops->error_handler)
		return -EOPNOTSUPP;

	if (lun != SCAN_WILD_CARD && lun)
		return -EINVAL;

	if (ap->nr_pmp_links == 0) {
		if (channel != SCAN_WILD_CARD && channel)
			return -EINVAL;
		devno = id;
	} else {
		if (id != SCAN_WILD_CARD && id)
			return -EINVAL;
		devno = channel;
	}

	spin_lock_irqsave(ap->lock, flags);

	if (devno == SCAN_WILD_CARD) {
		struct ata_link *link;

		ata_port_for_each_link(link, ap) {
			struct ata_eh_info *ehi = &link->eh_info;
			ehi->probe_mask |= (1 << ata_link_max_devices(link)) - 1;
			ehi->action |= ATA_EH_SOFTRESET;
		}
	} else {
		struct ata_device *dev = ata_find_dev(ap, devno);

		if (dev) {
			struct ata_eh_info *ehi = &dev->link->eh_info;
			ehi->probe_mask |= 1 << dev->devno;
			ehi->action |= ATA_EH_SOFTRESET;
			ehi->flags |= ATA_EHI_RESUME_LINK;
		} else
			rc = -EINVAL;
	}

	if (rc == 0) {
		ata_port_schedule_eh(ap);
		spin_unlock_irqrestore(ap->lock, flags);
		ata_port_wait_eh(ap);
	} else
		spin_unlock_irqrestore(ap->lock, flags);

	return rc;
}

/**
 *	ata_scsi_dev_rescan - initiate scsi_rescan_device()
 *	@work: Pointer to ATA port to perform scsi_rescan_device()
 *
 *	After ATA pass thru (SAT) commands are executed successfully,
 *	libata need to propagate the changes to SCSI layer.  This
 *	function must be executed from ata_aux_wq such that sdev
 *	attach/detach don't race with rescan.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_scsi_dev_rescan(struct work_struct *work)
{
	struct ata_port *ap =
		container_of(work, struct ata_port, scsi_rescan_task);
	struct ata_link *link;
	struct ata_device *dev;
	unsigned long flags;

	spin_lock_irqsave(ap->lock, flags);

	ata_port_for_each_link(link, ap) {
		ata_link_for_each_dev(dev, link) {
			struct scsi_device *sdev = dev->sdev;

			if (!ata_dev_enabled(dev) || !sdev)
				continue;
			if (scsi_device_get(sdev))
				continue;

			spin_unlock_irqrestore(ap->lock, flags);
			scsi_rescan_device(&(sdev->sdev_gendev));
			scsi_device_put(sdev);
			spin_lock_irqsave(ap->lock, flags);
		}
	}

	spin_unlock_irqrestore(ap->lock, flags);
}

/**
 *	ata_sas_port_alloc - Allocate port for a SAS attached SATA device
 *	@host: ATA host container for all SAS ports
 *	@port_info: Information from low-level host driver
 *	@shost: SCSI host that the scsi device is attached to
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	ata_port pointer on success / NULL on failure.
 */

struct ata_port *ata_sas_port_alloc(struct ata_host *host,
				    struct ata_port_info *port_info,
				    struct Scsi_Host *shost)
{
	struct ata_port *ap;

	ap = ata_port_alloc(host);
	if (!ap)
		return NULL;

	ap->port_no = 0;
	ap->lock = shost->host_lock;
	ap->pio_mask = port_info->pio_mask;
	ap->mwdma_mask = port_info->mwdma_mask;
	ap->udma_mask = port_info->udma_mask;
	ap->flags |= port_info->flags;
	ap->ops = port_info->port_ops;
	ap->cbl = ATA_CBL_SATA;

	return ap;
}
EXPORT_SYMBOL_GPL(ata_sas_port_alloc);

/**
 *	ata_sas_port_start - Set port up for dma.
 *	@ap: Port to initialize
 *
 *	Called just after data structures for each port are
 *	initialized.  Allocates DMA pad.
 *
 *	May be used as the port_start() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
int ata_sas_port_start(struct ata_port *ap)
{
	return ata_pad_alloc(ap, ap->dev);
}
EXPORT_SYMBOL_GPL(ata_sas_port_start);

/**
 *	ata_port_stop - Undo ata_sas_port_start()
 *	@ap: Port to shut down
 *
 *	Frees the DMA pad.
 *
 *	May be used as the port_stop() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_sas_port_stop(struct ata_port *ap)
{
	ata_pad_free(ap, ap->dev);
}
EXPORT_SYMBOL_GPL(ata_sas_port_stop);

/**
 *	ata_sas_port_init - Initialize a SATA device
 *	@ap: SATA port to initialize
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	Zero on success, non-zero on error.
 */

int ata_sas_port_init(struct ata_port *ap)
{
	int rc = ap->ops->port_start(ap);

	if (!rc) {
		ap->print_id = ata_print_id++;
		rc = ata_bus_probe(ap);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(ata_sas_port_init);

/**
 *	ata_sas_port_destroy - Destroy a SATA port allocated by ata_sas_port_alloc
 *	@ap: SATA port to destroy
 *
 */

void ata_sas_port_destroy(struct ata_port *ap)
{
	if (ap->ops->port_stop)
		ap->ops->port_stop(ap);
	kfree(ap);
}
EXPORT_SYMBOL_GPL(ata_sas_port_destroy);

/**
 *	ata_sas_slave_configure - Default slave_config routine for libata devices
 *	@sdev: SCSI device to configure
 *	@ap: ATA port to which SCSI device is attached
 *
 *	RETURNS:
 *	Zero.
 */

int ata_sas_slave_configure(struct scsi_device *sdev, struct ata_port *ap)
{
	ata_scsi_sdev_config(sdev);
	ata_scsi_dev_config(sdev, ap->link.device);
	return 0;
}
EXPORT_SYMBOL_GPL(ata_sas_slave_configure);

/**
 *	ata_sas_queuecmd - Issue SCSI cdb to libata-managed device
 *	@cmd: SCSI command to be sent
 *	@done: Completion function, called when command is complete
 *	@ap:	ATA port to which the command is being sent
 *
 *	RETURNS:
 *	Return value from __ata_scsi_queuecmd() if @cmd can be queued,
 *	0 otherwise.
 */

int ata_sas_queuecmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *),
		     struct ata_port *ap)
{
	int rc = 0;

	ata_scsi_dump_cdb(ap, cmd);

	if (likely(ata_scsi_dev_enabled(ap->link.device)))
		rc = __ata_scsi_queuecmd(cmd, done, ap->link.device);
	else {
		cmd->result = (DID_BAD_TARGET << 16);
		done(cmd);
	}
	return rc;
}
EXPORT_SYMBOL_GPL(ata_sas_queuecmd);
