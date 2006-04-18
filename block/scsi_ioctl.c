/*
 * Copyright (C) 2001 Jens Axboe <axboe@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 *
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/capability.h>
#include <linux/completion.h>
#include <linux/cdrom.h>
#include <linux/slab.h>
#include <linux/times.h>
#include <asm/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_cmnd.h>

/* Command group 3 is reserved and should never be used.  */
const unsigned char scsi_command_size[8] =
{
	6, 10, 10, 12,
	16, 12, 10, 10
};

EXPORT_SYMBOL(scsi_command_size);

#define BLK_DEFAULT_TIMEOUT	(60 * HZ)

#include <scsi/sg.h>

static int sg_get_version(int __user *p)
{
	static const int sg_version_num = 30527;
	return put_user(sg_version_num, p);
}

static int scsi_get_idlun(request_queue_t *q, int __user *p)
{
	return put_user(0, p);
}

static int scsi_get_bus(request_queue_t *q, int __user *p)
{
	return put_user(0, p);
}

static int sg_get_timeout(request_queue_t *q)
{
	return q->sg_timeout / (HZ / USER_HZ);
}

static int sg_set_timeout(request_queue_t *q, int __user *p)
{
	int timeout, err = get_user(timeout, p);

	if (!err)
		q->sg_timeout = timeout * (HZ / USER_HZ);

	return err;
}

static int sg_get_reserved_size(request_queue_t *q, int __user *p)
{
	return put_user(q->sg_reserved_size, p);
}

static int sg_set_reserved_size(request_queue_t *q, int __user *p)
{
	int size, err = get_user(size, p);

	if (err)
		return err;

	if (size < 0)
		return -EINVAL;
	if (size > (q->max_sectors << 9))
		size = q->max_sectors << 9;

	q->sg_reserved_size = size;
	return 0;
}

/*
 * will always return that we are ATAPI even for a real SCSI drive, I'm not
 * so sure this is worth doing anything about (why would you care??)
 */
static int sg_emulated_host(request_queue_t *q, int __user *p)
{
	return put_user(1, p);
}

#define CMD_READ_SAFE	0x01
#define CMD_WRITE_SAFE	0x02
#define CMD_WARNED	0x04
#define safe_for_read(cmd)	[cmd] = CMD_READ_SAFE
#define safe_for_write(cmd)	[cmd] = CMD_WRITE_SAFE

static int verify_command(struct file *file, unsigned char *cmd)
{
	static unsigned char cmd_type[256] = {

		/* Basic read-only commands */
		safe_for_read(TEST_UNIT_READY),
		safe_for_read(REQUEST_SENSE),
		safe_for_read(READ_6),
		safe_for_read(READ_10),
		safe_for_read(READ_12),
		safe_for_read(READ_16),
		safe_for_read(READ_BUFFER),
		safe_for_read(READ_DEFECT_DATA),
		safe_for_read(READ_LONG),
		safe_for_read(INQUIRY),
		safe_for_read(MODE_SENSE),
		safe_for_read(MODE_SENSE_10),
		safe_for_read(LOG_SENSE),
		safe_for_read(START_STOP),
		safe_for_read(GPCMD_VERIFY_10),
		safe_for_read(VERIFY_16),

		/* Audio CD commands */
		safe_for_read(GPCMD_PLAY_CD),
		safe_for_read(GPCMD_PLAY_AUDIO_10),
		safe_for_read(GPCMD_PLAY_AUDIO_MSF),
		safe_for_read(GPCMD_PLAY_AUDIO_TI),
		safe_for_read(GPCMD_PAUSE_RESUME),

		/* CD/DVD data reading */
		safe_for_read(GPCMD_READ_BUFFER_CAPACITY),
		safe_for_read(GPCMD_READ_CD),
		safe_for_read(GPCMD_READ_CD_MSF),
		safe_for_read(GPCMD_READ_DISC_INFO),
		safe_for_read(GPCMD_READ_CDVD_CAPACITY),
		safe_for_read(GPCMD_READ_DVD_STRUCTURE),
		safe_for_read(GPCMD_READ_HEADER),
		safe_for_read(GPCMD_READ_TRACK_RZONE_INFO),
		safe_for_read(GPCMD_READ_SUBCHANNEL),
		safe_for_read(GPCMD_READ_TOC_PMA_ATIP),
		safe_for_read(GPCMD_REPORT_KEY),
		safe_for_read(GPCMD_SCAN),
		safe_for_read(GPCMD_GET_CONFIGURATION),
		safe_for_read(GPCMD_READ_FORMAT_CAPACITIES),
		safe_for_read(GPCMD_GET_EVENT_STATUS_NOTIFICATION),
		safe_for_read(GPCMD_GET_PERFORMANCE),
		safe_for_read(GPCMD_SEEK),
		safe_for_read(GPCMD_STOP_PLAY_SCAN),

		/* Basic writing commands */
		safe_for_write(WRITE_6),
		safe_for_write(WRITE_10),
		safe_for_write(WRITE_VERIFY),
		safe_for_write(WRITE_12),
		safe_for_write(WRITE_VERIFY_12),
		safe_for_write(WRITE_16),
		safe_for_write(WRITE_LONG),
		safe_for_write(WRITE_LONG_2),
		safe_for_write(ERASE),
		safe_for_write(GPCMD_MODE_SELECT_10),
		safe_for_write(MODE_SELECT),
		safe_for_write(LOG_SELECT),
		safe_for_write(GPCMD_BLANK),
		safe_for_write(GPCMD_CLOSE_TRACK),
		safe_for_write(GPCMD_FLUSH_CACHE),
		safe_for_write(GPCMD_FORMAT_UNIT),
		safe_for_write(GPCMD_REPAIR_RZONE_TRACK),
		safe_for_write(GPCMD_RESERVE_RZONE_TRACK),
		safe_for_write(GPCMD_SEND_DVD_STRUCTURE),
		safe_for_write(GPCMD_SEND_EVENT),
		safe_for_write(GPCMD_SEND_KEY),
		safe_for_write(GPCMD_SEND_OPC),
		safe_for_write(GPCMD_SEND_CUE_SHEET),
		safe_for_write(GPCMD_SET_SPEED),
		safe_for_write(GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL),
		safe_for_write(GPCMD_LOAD_UNLOAD),
		safe_for_write(GPCMD_SET_STREAMING),
	};
	unsigned char type = cmd_type[cmd[0]];
	int has_write_perm = 0;

	/* Anybody who can open the device can do a read-safe command */
	if (type & CMD_READ_SAFE)
		return 0;

	/*
	 * file can be NULL from ioctl_by_bdev()...
	 */
	if (file)
		has_write_perm = file->f_mode & FMODE_WRITE;

	/* Write-safe commands just require a writable open.. */
	if ((type & CMD_WRITE_SAFE) && has_write_perm)
		return 0;

	/* And root can do any command.. */
	if (capable(CAP_SYS_RAWIO))
		return 0;

	if (!type) {
		cmd_type[cmd[0]] = CMD_WARNED;
		printk(KERN_WARNING "scsi: unknown opcode 0x%02x\n", cmd[0]);
	}

	/* Otherwise fail it with an "Operation not permitted" */
	return -EPERM;
}

static int sg_io(struct file *file, request_queue_t *q,
		struct gendisk *bd_disk, struct sg_io_hdr *hdr)
{
	unsigned long start_time;
	int writing = 0, ret = 0;
	struct request *rq;
	struct bio *bio;
	char sense[SCSI_SENSE_BUFFERSIZE];
	unsigned char cmd[BLK_MAX_CDB];

	if (hdr->interface_id != 'S')
		return -EINVAL;
	if (hdr->cmd_len > BLK_MAX_CDB)
		return -EINVAL;
	if (copy_from_user(cmd, hdr->cmdp, hdr->cmd_len))
		return -EFAULT;
	if (verify_command(file, cmd))
		return -EPERM;

	if (hdr->dxfer_len > (q->max_hw_sectors << 9))
		return -EIO;

	if (hdr->dxfer_len)
		switch (hdr->dxfer_direction) {
		default:
			return -EINVAL;
		case SG_DXFER_TO_FROM_DEV:
		case SG_DXFER_TO_DEV:
			writing = 1;
			break;
		case SG_DXFER_FROM_DEV:
			break;
		}

	rq = blk_get_request(q, writing ? WRITE : READ, GFP_KERNEL);
	if (!rq)
		return -ENOMEM;

	if (hdr->iovec_count) {
		const int size = sizeof(struct sg_iovec) * hdr->iovec_count;
		struct sg_iovec *iov;

		iov = kmalloc(size, GFP_KERNEL);
		if (!iov) {
			ret = -ENOMEM;
			goto out;
		}

		if (copy_from_user(iov, hdr->dxferp, size)) {
			kfree(iov);
			ret = -EFAULT;
			goto out;
		}

		ret = blk_rq_map_user_iov(q, rq, iov, hdr->iovec_count);
		kfree(iov);
	} else if (hdr->dxfer_len)
		ret = blk_rq_map_user(q, rq, hdr->dxferp, hdr->dxfer_len);

	if (ret)
		goto out;

	/*
	 * fill in request structure
	 */
	rq->cmd_len = hdr->cmd_len;
	memcpy(rq->cmd, cmd, hdr->cmd_len);
	if (sizeof(rq->cmd) != hdr->cmd_len)
		memset(rq->cmd + hdr->cmd_len, 0, sizeof(rq->cmd) - hdr->cmd_len);

	memset(sense, 0, sizeof(sense));
	rq->sense = sense;
	rq->sense_len = 0;

	rq->flags |= REQ_BLOCK_PC;
	bio = rq->bio;

	/*
	 * bounce this after holding a reference to the original bio, it's
	 * needed for proper unmapping
	 */
	if (rq->bio)
		blk_queue_bounce(q, &rq->bio);

	rq->timeout = (hdr->timeout * HZ) / 1000;
	if (!rq->timeout)
		rq->timeout = q->sg_timeout;
	if (!rq->timeout)
		rq->timeout = BLK_DEFAULT_TIMEOUT;

	rq->retries = 0;

	start_time = jiffies;

	/* ignore return value. All information is passed back to caller
	 * (if he doesn't check that is his problem).
	 * N.B. a non-zero SCSI status is _not_ necessarily an error.
	 */
	blk_execute_rq(q, bd_disk, rq, 0);

	/* write to all output members */
	hdr->status = 0xff & rq->errors;
	hdr->masked_status = status_byte(rq->errors);
	hdr->msg_status = msg_byte(rq->errors);
	hdr->host_status = host_byte(rq->errors);
	hdr->driver_status = driver_byte(rq->errors);
	hdr->info = 0;
	if (hdr->masked_status || hdr->host_status || hdr->driver_status)
		hdr->info |= SG_INFO_CHECK;
	hdr->resid = rq->data_len;
	hdr->duration = ((jiffies - start_time) * 1000) / HZ;
	hdr->sb_len_wr = 0;

	if (rq->sense_len && hdr->sbp) {
		int len = min((unsigned int) hdr->mx_sb_len, rq->sense_len);

		if (!copy_to_user(hdr->sbp, rq->sense, len))
			hdr->sb_len_wr = len;
	}

	if (blk_rq_unmap_user(bio, hdr->dxfer_len))
		ret = -EFAULT;

	/* may not have succeeded, but output values written to control
	 * structure (struct sg_io_hdr).  */
out:
	blk_put_request(rq);
	return ret;
}

/**
 * sg_scsi_ioctl  --  handle deprecated SCSI_IOCTL_SEND_COMMAND ioctl
 * @file:	file this ioctl operates on (optional)
 * @q:		request queue to send scsi commands down
 * @disk:	gendisk to operate on (option)
 * @sic:	userspace structure describing the command to perform
 *
 * Send down the scsi command described by @sic to the device below
 * the request queue @q.  If @file is non-NULL it's used to perform
 * fine-grained permission checks that allow users to send down
 * non-destructive SCSI commands.  If the caller has a struct gendisk
 * available it should be passed in as @disk to allow the low level
 * driver to use the information contained in it.  A non-NULL @disk
 * is only allowed if the caller knows that the low level driver doesn't
 * need it (e.g. in the scsi subsystem).
 *
 * Notes:
 *   -  This interface is deprecated - users should use the SG_IO
 *      interface instead, as this is a more flexible approach to
 *      performing SCSI commands on a device.
 *   -  The SCSI command length is determined by examining the 1st byte
 *      of the given command. There is no way to override this.
 *   -  Data transfers are limited to PAGE_SIZE
 *   -  The length (x + y) must be at least OMAX_SB_LEN bytes long to
 *      accommodate the sense buffer when an error occurs.
 *      The sense buffer is truncated to OMAX_SB_LEN (16) bytes so that
 *      old code will not be surprised.
 *   -  If a Unix error occurs (e.g. ENOMEM) then the user will receive
 *      a negative return and the Unix error code in 'errno'.
 *      If the SCSI command succeeds then 0 is returned.
 *      Positive numbers returned are the compacted SCSI error codes (4
 *      bytes in one int) where the lowest byte is the SCSI status.
 */
#define OMAX_SB_LEN 16          /* For backward compatibility */
int sg_scsi_ioctl(struct file *file, struct request_queue *q,
		  struct gendisk *disk, struct scsi_ioctl_command __user *sic)
{
	struct request *rq;
	int err;
	unsigned int in_len, out_len, bytes, opcode, cmdlen;
	char *buffer = NULL, sense[SCSI_SENSE_BUFFERSIZE];

	if (!sic)
		return -EINVAL;

	/*
	 * get in an out lengths, verify they don't exceed a page worth of data
	 */
	if (get_user(in_len, &sic->inlen))
		return -EFAULT;
	if (get_user(out_len, &sic->outlen))
		return -EFAULT;
	if (in_len > PAGE_SIZE || out_len > PAGE_SIZE)
		return -EINVAL;
	if (get_user(opcode, sic->data))
		return -EFAULT;

	bytes = max(in_len, out_len);
	if (bytes) {
		buffer = kmalloc(bytes, q->bounce_gfp | GFP_USER| __GFP_NOWARN);
		if (!buffer)
			return -ENOMEM;

		memset(buffer, 0, bytes);
	}

	rq = blk_get_request(q, in_len ? WRITE : READ, __GFP_WAIT);

	cmdlen = COMMAND_SIZE(opcode);

	/*
	 * get command and data to send to device, if any
	 */
	err = -EFAULT;
	rq->cmd_len = cmdlen;
	if (copy_from_user(rq->cmd, sic->data, cmdlen))
		goto error;

	if (in_len && copy_from_user(buffer, sic->data + cmdlen, in_len))
		goto error;

	err = verify_command(file, rq->cmd);
	if (err)
		goto error;

	/* default.  possible overriden later */
	rq->retries = 5;

	switch (opcode) {
	case SEND_DIAGNOSTIC:
	case FORMAT_UNIT:
		rq->timeout = FORMAT_UNIT_TIMEOUT;
		rq->retries = 1;
		break;
	case START_STOP:
		rq->timeout = START_STOP_TIMEOUT;
		break;
	case MOVE_MEDIUM:
		rq->timeout = MOVE_MEDIUM_TIMEOUT;
		break;
	case READ_ELEMENT_STATUS:
		rq->timeout = READ_ELEMENT_STATUS_TIMEOUT;
		break;
	case READ_DEFECT_DATA:
		rq->timeout = READ_DEFECT_DATA_TIMEOUT;
		rq->retries = 1;
		break;
	default:
		rq->timeout = BLK_DEFAULT_TIMEOUT;
		break;
	}

	if (bytes && blk_rq_map_kern(q, rq, buffer, bytes, __GFP_WAIT)) {
		err = DRIVER_ERROR << 24;
		goto out;
	}

	memset(sense, 0, sizeof(sense));
	rq->sense = sense;
	rq->sense_len = 0;
	rq->flags |= REQ_BLOCK_PC;

	blk_execute_rq(q, disk, rq, 0);

out:
	err = rq->errors & 0xff;	/* only 8 bit SCSI status */
	if (err) {
		if (rq->sense_len && rq->sense) {
			bytes = (OMAX_SB_LEN > rq->sense_len) ?
				rq->sense_len : OMAX_SB_LEN;
			if (copy_to_user(sic->data, rq->sense, bytes))
				err = -EFAULT;
		}
	} else {
		if (copy_to_user(sic->data, buffer, out_len))
			err = -EFAULT;
	}
	
error:
	kfree(buffer);
	blk_put_request(rq);
	return err;
}
EXPORT_SYMBOL_GPL(sg_scsi_ioctl);

/* Send basic block requests */
static int __blk_send_generic(request_queue_t *q, struct gendisk *bd_disk, int cmd, int data)
{
	struct request *rq;
	int err;

	rq = blk_get_request(q, WRITE, __GFP_WAIT);
	rq->flags |= REQ_BLOCK_PC;
	rq->data = NULL;
	rq->data_len = 0;
	rq->timeout = BLK_DEFAULT_TIMEOUT;
	memset(rq->cmd, 0, sizeof(rq->cmd));
	rq->cmd[0] = cmd;
	rq->cmd[4] = data;
	rq->cmd_len = 6;
	err = blk_execute_rq(q, bd_disk, rq, 0);
	blk_put_request(rq);

	return err;
}

static inline int blk_send_start_stop(request_queue_t *q, struct gendisk *bd_disk, int data)
{
	return __blk_send_generic(q, bd_disk, GPCMD_START_STOP_UNIT, data);
}

int scsi_cmd_ioctl(struct file *file, struct gendisk *bd_disk, unsigned int cmd, void __user *arg)
{
	request_queue_t *q;
	int err;

	q = bd_disk->queue;
	if (!q)
		return -ENXIO;

	if (blk_get_queue(q))
		return -ENXIO;

	switch (cmd) {
		/*
		 * new sgv3 interface
		 */
		case SG_GET_VERSION_NUM:
			err = sg_get_version(arg);
			break;
		case SCSI_IOCTL_GET_IDLUN:
			err = scsi_get_idlun(q, arg);
			break;
		case SCSI_IOCTL_GET_BUS_NUMBER:
			err = scsi_get_bus(q, arg);
			break;
		case SG_SET_TIMEOUT:
			err = sg_set_timeout(q, arg);
			break;
		case SG_GET_TIMEOUT:
			err = sg_get_timeout(q);
			break;
		case SG_GET_RESERVED_SIZE:
			err = sg_get_reserved_size(q, arg);
			break;
		case SG_SET_RESERVED_SIZE:
			err = sg_set_reserved_size(q, arg);
			break;
		case SG_EMULATED_HOST:
			err = sg_emulated_host(q, arg);
			break;
		case SG_IO: {
			struct sg_io_hdr hdr;

			err = -EFAULT;
			if (copy_from_user(&hdr, arg, sizeof(hdr)))
				break;
			err = sg_io(file, q, bd_disk, &hdr);
			if (err == -EFAULT)
				break;

			if (copy_to_user(arg, &hdr, sizeof(hdr)))
				err = -EFAULT;
			break;
		}
		case CDROM_SEND_PACKET: {
			struct cdrom_generic_command cgc;
			struct sg_io_hdr hdr;

			err = -EFAULT;
			if (copy_from_user(&cgc, arg, sizeof(cgc)))
				break;
			cgc.timeout = clock_t_to_jiffies(cgc.timeout);
			memset(&hdr, 0, sizeof(hdr));
			hdr.interface_id = 'S';
			hdr.cmd_len = sizeof(cgc.cmd);
			hdr.dxfer_len = cgc.buflen;
			err = 0;
			switch (cgc.data_direction) {
				case CGC_DATA_UNKNOWN:
					hdr.dxfer_direction = SG_DXFER_UNKNOWN;
					break;
				case CGC_DATA_WRITE:
					hdr.dxfer_direction = SG_DXFER_TO_DEV;
					break;
				case CGC_DATA_READ:
					hdr.dxfer_direction = SG_DXFER_FROM_DEV;
					break;
				case CGC_DATA_NONE:
					hdr.dxfer_direction = SG_DXFER_NONE;
					break;
				default:
					err = -EINVAL;
			}
			if (err)
				break;

			hdr.dxferp = cgc.buffer;
			hdr.sbp = cgc.sense;
			if (hdr.sbp)
				hdr.mx_sb_len = sizeof(struct request_sense);
			hdr.timeout = cgc.timeout;
			hdr.cmdp = ((struct cdrom_generic_command __user*) arg)->cmd;
			hdr.cmd_len = sizeof(cgc.cmd);

			err = sg_io(file, q, bd_disk, &hdr);
			if (err == -EFAULT)
				break;

			if (hdr.status)
				err = -EIO;

			cgc.stat = err;
			cgc.buflen = hdr.resid;
			if (copy_to_user(arg, &cgc, sizeof(cgc)))
				err = -EFAULT;

			break;
		}

		/*
		 * old junk scsi send command ioctl
		 */
		case SCSI_IOCTL_SEND_COMMAND:
			printk(KERN_WARNING "program %s is using a deprecated SCSI ioctl, please convert it to SG_IO\n", current->comm);
			err = -EINVAL;
			if (!arg)
				break;

			err = sg_scsi_ioctl(file, q, bd_disk, arg);
			break;
		case CDROMCLOSETRAY:
			err = blk_send_start_stop(q, bd_disk, 0x03);
			break;
		case CDROMEJECT:
			err = blk_send_start_stop(q, bd_disk, 0x02);
			break;
		default:
			err = -ENOTTY;
	}

	blk_put_queue(q);
	return err;
}

EXPORT_SYMBOL(scsi_cmd_ioctl);
