// SPDX-License-Identifier: GPL-2.0-only
/*
 * Changes:
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br> 08/23/2000
 * - get rid of some verify_areas and use __copy*user and __get/put_user
 *   for the ones that remain
 */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/cdrom.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>
#include <scsi/scsi_dbg.h>

#include "scsi_logging.h"

#define NORMAL_RETRIES			5
#define IOCTL_NORMAL_TIMEOUT			(10 * HZ)

#define MAX_BUF PAGE_SIZE

/**
 * ioctl_probe  --  return host identification
 * @host:	host to identify
 * @buffer:	userspace buffer for identification
 *
 * Return an identifying string at @buffer, if @buffer is non-NULL, filling
 * to the length stored at * (int *) @buffer.
 */
static int ioctl_probe(struct Scsi_Host *host, void __user *buffer)
{
	unsigned int len, slen;
	const char *string;

	if (buffer) {
		if (get_user(len, (unsigned int __user *) buffer))
			return -EFAULT;

		if (host->hostt->info)
			string = host->hostt->info(host);
		else
			string = host->hostt->name;
		if (string) {
			slen = strlen(string);
			if (len > slen)
				len = slen + 1;
			if (copy_to_user(buffer, string, len))
				return -EFAULT;
		}
	}
	return 1;
}

static int ioctl_internal_command(struct scsi_device *sdev, char *cmd,
				  int timeout, int retries)
{
	int result;
	struct scsi_sense_hdr sshdr;

	SCSI_LOG_IOCTL(1, sdev_printk(KERN_INFO, sdev,
				      "Trying ioctl with scsi command %d\n", *cmd));

	result = scsi_execute_req(sdev, cmd, DMA_NONE, NULL, 0,
				  &sshdr, timeout, retries, NULL);

	SCSI_LOG_IOCTL(2, sdev_printk(KERN_INFO, sdev,
				      "Ioctl returned  0x%x\n", result));

	if (result < 0)
		goto out;
	if (scsi_sense_valid(&sshdr)) {
		switch (sshdr.sense_key) {
		case ILLEGAL_REQUEST:
			if (cmd[0] == ALLOW_MEDIUM_REMOVAL)
				sdev->lockable = 0;
			else
				sdev_printk(KERN_INFO, sdev,
					    "ioctl_internal_command: "
					    "ILLEGAL REQUEST "
					    "asc=0x%x ascq=0x%x\n",
					    sshdr.asc, sshdr.ascq);
			break;
		case NOT_READY:	/* This happens if there is no disc in drive */
			if (sdev->removable)
				break;
			fallthrough;
		case UNIT_ATTENTION:
			if (sdev->removable) {
				sdev->changed = 1;
				result = 0;	/* This is no longer considered an error */
				break;
			}
			fallthrough;	/* for non-removable media */
		default:
			sdev_printk(KERN_INFO, sdev,
				    "ioctl_internal_command return code = %x\n",
				    result);
			scsi_print_sense_hdr(sdev, NULL, &sshdr);
			break;
		}
	}
out:
	SCSI_LOG_IOCTL(2, sdev_printk(KERN_INFO, sdev,
				      "IOCTL Releasing command\n"));
	return result;
}

int scsi_set_medium_removal(struct scsi_device *sdev, char state)
{
	char scsi_cmd[MAX_COMMAND_SIZE];
	int ret;

	if (!sdev->removable || !sdev->lockable)
	       return 0;

	scsi_cmd[0] = ALLOW_MEDIUM_REMOVAL;
	scsi_cmd[1] = 0;
	scsi_cmd[2] = 0;
	scsi_cmd[3] = 0;
	scsi_cmd[4] = state;
	scsi_cmd[5] = 0;

	ret = ioctl_internal_command(sdev, scsi_cmd,
			IOCTL_NORMAL_TIMEOUT, NORMAL_RETRIES);
	if (ret == 0)
		sdev->locked = (state == SCSI_REMOVAL_PREVENT);
	return ret;
}
EXPORT_SYMBOL(scsi_set_medium_removal);

/*
 * The scsi_ioctl_get_pci() function places into arg the value
 * pci_dev::slot_name (8 characters) for the PCI device (if any).
 * Returns: 0 on success
 *          -ENXIO if there isn't a PCI device pointer
 *                 (could be because the SCSI driver hasn't been
 *                  updated yet, or because it isn't a SCSI
 *                  device)
 *          any copy_to_user() error on failure there
 */
static int scsi_ioctl_get_pci(struct scsi_device *sdev, void __user *arg)
{
	struct device *dev = scsi_get_device(sdev->host);
	const char *name;

        if (!dev)
		return -ENXIO;

	name = dev_name(dev);

	/* compatibility with old ioctl which only returned
	 * 20 characters */
        return copy_to_user(arg, name, min(strlen(name), (size_t)20))
		? -EFAULT: 0;
}

static int sg_get_version(int __user *p)
{
	static const int sg_version_num = 30527;
	return put_user(sg_version_num, p);
}

static int sg_set_timeout(struct scsi_device *sdev, int __user *p)
{
	int timeout, err = get_user(timeout, p);

	if (!err)
		sdev->sg_timeout = clock_t_to_jiffies(timeout);

	return err;
}

static int sg_get_reserved_size(struct scsi_device *sdev, int __user *p)
{
	int val = min(sdev->sg_reserved_size,
		      queue_max_bytes(sdev->request_queue));

	return put_user(val, p);
}

static int sg_set_reserved_size(struct scsi_device *sdev, int __user *p)
{
	int size, err = get_user(size, p);

	if (err)
		return err;

	if (size < 0)
		return -EINVAL;

	sdev->sg_reserved_size = min_t(unsigned int, size,
				       queue_max_bytes(sdev->request_queue));
	return 0;
}

/*
 * will always return that we are ATAPI even for a real SCSI drive, I'm not
 * so sure this is worth doing anything about (why would you care??)
 */
static int sg_emulated_host(struct request_queue *q, int __user *p)
{
	return put_user(1, p);
}

static int scsi_get_idlun(struct scsi_device *sdev, void __user *argp)
{
	struct scsi_idlun v = {
		.dev_id = (sdev->id & 0xff) +
			((sdev->lun & 0xff) << 8) +
			((sdev->channel & 0xff) << 16) +
			((sdev->host->host_no & 0xff) << 24),
		.host_unique_id = sdev->host->unique_id
	};
	if (copy_to_user(argp, &v, sizeof(struct scsi_idlun)))
		return -EFAULT;
	return 0;
}

static int scsi_send_start_stop(struct scsi_device *sdev, int data)
{
	u8 cdb[MAX_COMMAND_SIZE] = { };

	cdb[0] = START_STOP;
	cdb[4] = data;
	return ioctl_internal_command(sdev, cdb, START_STOP_TIMEOUT,
				      NORMAL_RETRIES);
}

/*
 * Check if the given command is allowed.
 *
 * Only a subset of commands are allowed for unprivileged users. Commands used
 * to format the media, update the firmware, etc. are not permitted.
 */
bool scsi_cmd_allowed(unsigned char *cmd, fmode_t mode)
{
	/* root can do any command. */
	if (capable(CAP_SYS_RAWIO))
		return true;

	/* Anybody who can open the device can do a read-safe command */
	switch (cmd[0]) {
	/* Basic read-only commands */
	case TEST_UNIT_READY:
	case REQUEST_SENSE:
	case READ_6:
	case READ_10:
	case READ_12:
	case READ_16:
	case READ_BUFFER:
	case READ_DEFECT_DATA:
	case READ_CAPACITY: /* also GPCMD_READ_CDVD_CAPACITY */
	case READ_LONG:
	case INQUIRY:
	case MODE_SENSE:
	case MODE_SENSE_10:
	case LOG_SENSE:
	case START_STOP:
	case GPCMD_VERIFY_10:
	case VERIFY_16:
	case REPORT_LUNS:
	case SERVICE_ACTION_IN_16:
	case RECEIVE_DIAGNOSTIC:
	case MAINTENANCE_IN: /* also GPCMD_SEND_KEY, which is a write command */
	case GPCMD_READ_BUFFER_CAPACITY:
	/* Audio CD commands */
	case GPCMD_PLAY_CD:
	case GPCMD_PLAY_AUDIO_10:
	case GPCMD_PLAY_AUDIO_MSF:
	case GPCMD_PLAY_AUDIO_TI:
	case GPCMD_PAUSE_RESUME:
	/* CD/DVD data reading */
	case GPCMD_READ_CD:
	case GPCMD_READ_CD_MSF:
	case GPCMD_READ_DISC_INFO:
	case GPCMD_READ_DVD_STRUCTURE:
	case GPCMD_READ_HEADER:
	case GPCMD_READ_TRACK_RZONE_INFO:
	case GPCMD_READ_SUBCHANNEL:
	case GPCMD_READ_TOC_PMA_ATIP:
	case GPCMD_REPORT_KEY:
	case GPCMD_SCAN:
	case GPCMD_GET_CONFIGURATION:
	case GPCMD_READ_FORMAT_CAPACITIES:
	case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
	case GPCMD_GET_PERFORMANCE:
	case GPCMD_SEEK:
	case GPCMD_STOP_PLAY_SCAN:
	/* ZBC */
	case ZBC_IN:
		return true;
	/* Basic writing commands */
	case WRITE_6:
	case WRITE_10:
	case WRITE_VERIFY:
	case WRITE_12:
	case WRITE_VERIFY_12:
	case WRITE_16:
	case WRITE_LONG:
	case WRITE_LONG_2:
	case WRITE_SAME:
	case WRITE_SAME_16:
	case WRITE_SAME_32:
	case ERASE:
	case GPCMD_MODE_SELECT_10:
	case MODE_SELECT:
	case LOG_SELECT:
	case GPCMD_BLANK:
	case GPCMD_CLOSE_TRACK:
	case GPCMD_FLUSH_CACHE:
	case GPCMD_FORMAT_UNIT:
	case GPCMD_REPAIR_RZONE_TRACK:
	case GPCMD_RESERVE_RZONE_TRACK:
	case GPCMD_SEND_DVD_STRUCTURE:
	case GPCMD_SEND_EVENT:
	case GPCMD_SEND_OPC:
	case GPCMD_SEND_CUE_SHEET:
	case GPCMD_SET_SPEED:
	case GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
	case GPCMD_LOAD_UNLOAD:
	case GPCMD_SET_STREAMING:
	case GPCMD_SET_READ_AHEAD:
	/* ZBC */
	case ZBC_OUT:
		return (mode & FMODE_WRITE);
	default:
		return false;
	}
}
EXPORT_SYMBOL(scsi_cmd_allowed);

static int scsi_fill_sghdr_rq(struct scsi_device *sdev, struct request *rq,
		struct sg_io_hdr *hdr, fmode_t mode)
{
	struct scsi_cmnd *scmd = blk_mq_rq_to_pdu(rq);

	if (hdr->cmd_len < 6)
		return -EMSGSIZE;
	if (copy_from_user(scmd->cmnd, hdr->cmdp, hdr->cmd_len))
		return -EFAULT;
	if (!scsi_cmd_allowed(scmd->cmnd, mode))
		return -EPERM;
	scmd->cmd_len = hdr->cmd_len;

	rq->timeout = msecs_to_jiffies(hdr->timeout);
	if (!rq->timeout)
		rq->timeout = sdev->sg_timeout;
	if (!rq->timeout)
		rq->timeout = BLK_DEFAULT_SG_TIMEOUT;
	if (rq->timeout < BLK_MIN_SG_TIMEOUT)
		rq->timeout = BLK_MIN_SG_TIMEOUT;

	return 0;
}

static int scsi_complete_sghdr_rq(struct request *rq, struct sg_io_hdr *hdr,
		struct bio *bio)
{
	struct scsi_cmnd *scmd = blk_mq_rq_to_pdu(rq);
	int r, ret = 0;

	/*
	 * fill in all the output members
	 */
	hdr->status = scmd->result & 0xff;
	hdr->masked_status = sg_status_byte(scmd->result);
	hdr->msg_status = COMMAND_COMPLETE;
	hdr->host_status = host_byte(scmd->result);
	hdr->driver_status = 0;
	if (scsi_status_is_check_condition(hdr->status))
		hdr->driver_status = DRIVER_SENSE;
	hdr->info = 0;
	if (hdr->masked_status || hdr->host_status || hdr->driver_status)
		hdr->info |= SG_INFO_CHECK;
	hdr->resid = scmd->resid_len;
	hdr->sb_len_wr = 0;

	if (scmd->sense_len && hdr->sbp) {
		int len = min((unsigned int) hdr->mx_sb_len, scmd->sense_len);

		if (!copy_to_user(hdr->sbp, scmd->sense_buffer, len))
			hdr->sb_len_wr = len;
		else
			ret = -EFAULT;
	}

	r = blk_rq_unmap_user(bio);
	if (!ret)
		ret = r;

	return ret;
}

static int sg_io(struct scsi_device *sdev, struct sg_io_hdr *hdr, fmode_t mode)
{
	unsigned long start_time;
	ssize_t ret = 0;
	int writing = 0;
	int at_head = 0;
	struct request *rq;
	struct scsi_cmnd *scmd;
	struct bio *bio;

	if (hdr->interface_id != 'S')
		return -EINVAL;

	if (hdr->dxfer_len > (queue_max_hw_sectors(sdev->request_queue) << 9))
		return -EIO;

	if (hdr->dxfer_len)
		switch (hdr->dxfer_direction) {
		default:
			return -EINVAL;
		case SG_DXFER_TO_DEV:
			writing = 1;
			break;
		case SG_DXFER_TO_FROM_DEV:
		case SG_DXFER_FROM_DEV:
			break;
		}
	if (hdr->flags & SG_FLAG_Q_AT_HEAD)
		at_head = 1;

	rq = scsi_alloc_request(sdev->request_queue, writing ?
			     REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(rq))
		return PTR_ERR(rq);
	scmd = blk_mq_rq_to_pdu(rq);

	if (hdr->cmd_len > sizeof(scmd->cmnd)) {
		ret = -EINVAL;
		goto out_put_request;
	}

	ret = scsi_fill_sghdr_rq(sdev, rq, hdr, mode);
	if (ret < 0)
		goto out_put_request;

	ret = blk_rq_map_user_io(rq, NULL, hdr->dxferp, hdr->dxfer_len,
			GFP_KERNEL, hdr->iovec_count && hdr->dxfer_len,
			hdr->iovec_count, 0, rq_data_dir(rq));
	if (ret)
		goto out_put_request;

	bio = rq->bio;
	scmd->allowed = 0;

	start_time = jiffies;

	blk_execute_rq(rq, at_head);

	hdr->duration = jiffies_to_msecs(jiffies - start_time);

	ret = scsi_complete_sghdr_rq(rq, hdr, bio);

out_put_request:
	blk_mq_free_request(rq);
	return ret;
}

/**
 * sg_scsi_ioctl  --  handle deprecated SCSI_IOCTL_SEND_COMMAND ioctl
 * @q:		request queue to send scsi commands down
 * @mode:	mode used to open the file through which the ioctl has been
 *		submitted
 * @sic:	userspace structure describing the command to perform
 *
 * Send down the scsi command described by @sic to the device below
 * the request queue @q.
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
static int sg_scsi_ioctl(struct request_queue *q, fmode_t mode,
		struct scsi_ioctl_command __user *sic)
{
	struct request *rq;
	int err;
	unsigned int in_len, out_len, bytes, opcode, cmdlen;
	struct scsi_cmnd *scmd;
	char *buffer = NULL;

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
	if (get_user(opcode, &sic->data[0]))
		return -EFAULT;

	bytes = max(in_len, out_len);
	if (bytes) {
		buffer = kzalloc(bytes, GFP_NOIO | GFP_USER | __GFP_NOWARN);
		if (!buffer)
			return -ENOMEM;

	}

	rq = scsi_alloc_request(q, in_len ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto error_free_buffer;
	}
	scmd = blk_mq_rq_to_pdu(rq);

	cmdlen = COMMAND_SIZE(opcode);

	/*
	 * get command and data to send to device, if any
	 */
	err = -EFAULT;
	scmd->cmd_len = cmdlen;
	if (copy_from_user(scmd->cmnd, sic->data, cmdlen))
		goto error;

	if (in_len && copy_from_user(buffer, sic->data + cmdlen, in_len))
		goto error;

	err = -EPERM;
	if (!scsi_cmd_allowed(scmd->cmnd, mode))
		goto error;

	/* default.  possible overridden later */
	scmd->allowed = 5;

	switch (opcode) {
	case SEND_DIAGNOSTIC:
	case FORMAT_UNIT:
		rq->timeout = FORMAT_UNIT_TIMEOUT;
		scmd->allowed = 1;
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
		scmd->allowed = 1;
		break;
	default:
		rq->timeout = BLK_DEFAULT_SG_TIMEOUT;
		break;
	}

	if (bytes) {
		err = blk_rq_map_kern(q, rq, buffer, bytes, GFP_NOIO);
		if (err)
			goto error;
	}

	blk_execute_rq(rq, false);

	err = scmd->result & 0xff;	/* only 8 bit SCSI status */
	if (err) {
		if (scmd->sense_len && scmd->sense_buffer) {
			/* limit sense len for backward compatibility */
			if (copy_to_user(sic->data, scmd->sense_buffer,
					 min(scmd->sense_len, 16U)))
				err = -EFAULT;
		}
	} else {
		if (copy_to_user(sic->data, buffer, out_len))
			err = -EFAULT;
	}

error:
	blk_mq_free_request(rq);

error_free_buffer:
	kfree(buffer);

	return err;
}

int put_sg_io_hdr(const struct sg_io_hdr *hdr, void __user *argp)
{
#ifdef CONFIG_COMPAT
	if (in_compat_syscall()) {
		struct compat_sg_io_hdr hdr32 =  {
			.interface_id	 = hdr->interface_id,
			.dxfer_direction = hdr->dxfer_direction,
			.cmd_len	 = hdr->cmd_len,
			.mx_sb_len	 = hdr->mx_sb_len,
			.iovec_count	 = hdr->iovec_count,
			.dxfer_len	 = hdr->dxfer_len,
			.dxferp		 = (uintptr_t)hdr->dxferp,
			.cmdp		 = (uintptr_t)hdr->cmdp,
			.sbp		 = (uintptr_t)hdr->sbp,
			.timeout	 = hdr->timeout,
			.flags		 = hdr->flags,
			.pack_id	 = hdr->pack_id,
			.usr_ptr	 = (uintptr_t)hdr->usr_ptr,
			.status		 = hdr->status,
			.masked_status	 = hdr->masked_status,
			.msg_status	 = hdr->msg_status,
			.sb_len_wr	 = hdr->sb_len_wr,
			.host_status	 = hdr->host_status,
			.driver_status	 = hdr->driver_status,
			.resid		 = hdr->resid,
			.duration	 = hdr->duration,
			.info		 = hdr->info,
		};

		if (copy_to_user(argp, &hdr32, sizeof(hdr32)))
			return -EFAULT;

		return 0;
	}
#endif

	if (copy_to_user(argp, hdr, sizeof(*hdr)))
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL(put_sg_io_hdr);

int get_sg_io_hdr(struct sg_io_hdr *hdr, const void __user *argp)
{
#ifdef CONFIG_COMPAT
	struct compat_sg_io_hdr hdr32;

	if (in_compat_syscall()) {
		if (copy_from_user(&hdr32, argp, sizeof(hdr32)))
			return -EFAULT;

		*hdr = (struct sg_io_hdr) {
			.interface_id	 = hdr32.interface_id,
			.dxfer_direction = hdr32.dxfer_direction,
			.cmd_len	 = hdr32.cmd_len,
			.mx_sb_len	 = hdr32.mx_sb_len,
			.iovec_count	 = hdr32.iovec_count,
			.dxfer_len	 = hdr32.dxfer_len,
			.dxferp		 = compat_ptr(hdr32.dxferp),
			.cmdp		 = compat_ptr(hdr32.cmdp),
			.sbp		 = compat_ptr(hdr32.sbp),
			.timeout	 = hdr32.timeout,
			.flags		 = hdr32.flags,
			.pack_id	 = hdr32.pack_id,
			.usr_ptr	 = compat_ptr(hdr32.usr_ptr),
			.status		 = hdr32.status,
			.masked_status	 = hdr32.masked_status,
			.msg_status	 = hdr32.msg_status,
			.sb_len_wr	 = hdr32.sb_len_wr,
			.host_status	 = hdr32.host_status,
			.driver_status	 = hdr32.driver_status,
			.resid		 = hdr32.resid,
			.duration	 = hdr32.duration,
			.info		 = hdr32.info,
		};

		return 0;
	}
#endif

	if (copy_from_user(hdr, argp, sizeof(*hdr)))
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL(get_sg_io_hdr);

#ifdef CONFIG_COMPAT
struct compat_cdrom_generic_command {
	unsigned char	cmd[CDROM_PACKET_SIZE];
	compat_caddr_t	buffer;
	compat_uint_t	buflen;
	compat_int_t	stat;
	compat_caddr_t	sense;
	unsigned char	data_direction;
	unsigned char	pad[3];
	compat_int_t	quiet;
	compat_int_t	timeout;
	compat_caddr_t	unused;
};
#endif

static int scsi_get_cdrom_generic_arg(struct cdrom_generic_command *cgc,
				      const void __user *arg)
{
#ifdef CONFIG_COMPAT
	if (in_compat_syscall()) {
		struct compat_cdrom_generic_command cgc32;

		if (copy_from_user(&cgc32, arg, sizeof(cgc32)))
			return -EFAULT;

		*cgc = (struct cdrom_generic_command) {
			.buffer		= compat_ptr(cgc32.buffer),
			.buflen		= cgc32.buflen,
			.stat		= cgc32.stat,
			.sense		= compat_ptr(cgc32.sense),
			.data_direction	= cgc32.data_direction,
			.quiet		= cgc32.quiet,
			.timeout	= cgc32.timeout,
			.unused		= compat_ptr(cgc32.unused),
		};
		memcpy(&cgc->cmd, &cgc32.cmd, CDROM_PACKET_SIZE);
		return 0;
	}
#endif
	if (copy_from_user(cgc, arg, sizeof(*cgc)))
		return -EFAULT;

	return 0;
}

static int scsi_put_cdrom_generic_arg(const struct cdrom_generic_command *cgc,
				      void __user *arg)
{
#ifdef CONFIG_COMPAT
	if (in_compat_syscall()) {
		struct compat_cdrom_generic_command cgc32 = {
			.buffer		= (uintptr_t)(cgc->buffer),
			.buflen		= cgc->buflen,
			.stat		= cgc->stat,
			.sense		= (uintptr_t)(cgc->sense),
			.data_direction	= cgc->data_direction,
			.quiet		= cgc->quiet,
			.timeout	= cgc->timeout,
			.unused		= (uintptr_t)(cgc->unused),
		};
		memcpy(&cgc32.cmd, &cgc->cmd, CDROM_PACKET_SIZE);

		if (copy_to_user(arg, &cgc32, sizeof(cgc32)))
			return -EFAULT;

		return 0;
	}
#endif
	if (copy_to_user(arg, cgc, sizeof(*cgc)))
		return -EFAULT;

	return 0;
}

static int scsi_cdrom_send_packet(struct scsi_device *sdev, fmode_t mode,
		void __user *arg)
{
	struct cdrom_generic_command cgc;
	struct sg_io_hdr hdr;
	int err;

	err = scsi_get_cdrom_generic_arg(&cgc, arg);
	if (err)
		return err;

	cgc.timeout = clock_t_to_jiffies(cgc.timeout);
	memset(&hdr, 0, sizeof(hdr));
	hdr.interface_id = 'S';
	hdr.cmd_len = sizeof(cgc.cmd);
	hdr.dxfer_len = cgc.buflen;
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
		return -EINVAL;
	}

	hdr.dxferp = cgc.buffer;
	hdr.sbp = cgc.sense;
	if (hdr.sbp)
		hdr.mx_sb_len = sizeof(struct request_sense);
	hdr.timeout = jiffies_to_msecs(cgc.timeout);
	hdr.cmdp = ((struct cdrom_generic_command __user *) arg)->cmd;
	hdr.cmd_len = sizeof(cgc.cmd);

	err = sg_io(sdev, &hdr, mode);
	if (err == -EFAULT)
		return -EFAULT;

	if (hdr.status)
		return -EIO;

	cgc.stat = err;
	cgc.buflen = hdr.resid;
	if (scsi_put_cdrom_generic_arg(&cgc, arg))
		return -EFAULT;

	return err;
}

static int scsi_ioctl_sg_io(struct scsi_device *sdev, fmode_t mode,
		void __user *argp)
{
	struct sg_io_hdr hdr;
	int error;

	error = get_sg_io_hdr(&hdr, argp);
	if (error)
		return error;
	error = sg_io(sdev, &hdr, mode);
	if (error == -EFAULT)
		return error;
	if (put_sg_io_hdr(&hdr, argp))
		return -EFAULT;
	return error;
}

/**
 * scsi_ioctl - Dispatch ioctl to scsi device
 * @sdev: scsi device receiving ioctl
 * @mode: mode the block/char device is opened with
 * @cmd: which ioctl is it
 * @arg: data associated with ioctl
 *
 * Description: The scsi_ioctl() function differs from most ioctls in that it
 * does not take a major/minor number as the dev field.  Rather, it takes
 * a pointer to a &struct scsi_device.
 */
int scsi_ioctl(struct scsi_device *sdev, fmode_t mode, int cmd,
		void __user *arg)
{
	struct request_queue *q = sdev->request_queue;
	struct scsi_sense_hdr sense_hdr;

	/* Check for deprecated ioctls ... all the ioctls which don't
	 * follow the new unique numbering scheme are deprecated */
	switch (cmd) {
	case SCSI_IOCTL_SEND_COMMAND:
	case SCSI_IOCTL_TEST_UNIT_READY:
	case SCSI_IOCTL_BENCHMARK_COMMAND:
	case SCSI_IOCTL_SYNC:
	case SCSI_IOCTL_START_UNIT:
	case SCSI_IOCTL_STOP_UNIT:
		printk(KERN_WARNING "program %s is using a deprecated SCSI "
		       "ioctl, please convert it to SG_IO\n", current->comm);
		break;
	default:
		break;
	}

	switch (cmd) {
	case SG_GET_VERSION_NUM:
		return sg_get_version(arg);
	case SG_SET_TIMEOUT:
		return sg_set_timeout(sdev, arg);
	case SG_GET_TIMEOUT:
		return jiffies_to_clock_t(sdev->sg_timeout);
	case SG_GET_RESERVED_SIZE:
		return sg_get_reserved_size(sdev, arg);
	case SG_SET_RESERVED_SIZE:
		return sg_set_reserved_size(sdev, arg);
	case SG_EMULATED_HOST:
		return sg_emulated_host(q, arg);
	case SG_IO:
		return scsi_ioctl_sg_io(sdev, mode, arg);
	case SCSI_IOCTL_SEND_COMMAND:
		return sg_scsi_ioctl(q, mode, arg);
	case CDROM_SEND_PACKET:
		return scsi_cdrom_send_packet(sdev, mode, arg);
	case CDROMCLOSETRAY:
		return scsi_send_start_stop(sdev, 3);
	case CDROMEJECT:
		return scsi_send_start_stop(sdev, 2);
	case SCSI_IOCTL_GET_IDLUN:
		return scsi_get_idlun(sdev, arg);
	case SCSI_IOCTL_GET_BUS_NUMBER:
		return put_user(sdev->host->host_no, (int __user *)arg);
	case SCSI_IOCTL_PROBE_HOST:
		return ioctl_probe(sdev->host, arg);
	case SCSI_IOCTL_DOORLOCK:
		return scsi_set_medium_removal(sdev, SCSI_REMOVAL_PREVENT);
	case SCSI_IOCTL_DOORUNLOCK:
		return scsi_set_medium_removal(sdev, SCSI_REMOVAL_ALLOW);
	case SCSI_IOCTL_TEST_UNIT_READY:
		return scsi_test_unit_ready(sdev, IOCTL_NORMAL_TIMEOUT,
					    NORMAL_RETRIES, &sense_hdr);
	case SCSI_IOCTL_START_UNIT:
		return scsi_send_start_stop(sdev, 1);
	case SCSI_IOCTL_STOP_UNIT:
		return scsi_send_start_stop(sdev, 0);
        case SCSI_IOCTL_GET_PCI:
                return scsi_ioctl_get_pci(sdev, arg);
	case SG_SCSI_RESET:
		return scsi_ioctl_reset(sdev, arg);
	}

#ifdef CONFIG_COMPAT
	if (in_compat_syscall()) {
		if (!sdev->host->hostt->compat_ioctl)
			return -EINVAL;
		return sdev->host->hostt->compat_ioctl(sdev, cmd, arg);
	}
#endif
	if (!sdev->host->hostt->ioctl)
		return -EINVAL;
	return sdev->host->hostt->ioctl(sdev, cmd, arg);
}
EXPORT_SYMBOL(scsi_ioctl);

/*
 * We can process a reset even when a device isn't fully operable.
 */
int scsi_ioctl_block_when_processing_errors(struct scsi_device *sdev, int cmd,
		bool ndelay)
{
	if (cmd == SG_SCSI_RESET && ndelay) {
		if (scsi_host_in_recovery(sdev->host))
			return -EAGAIN;
	} else {
		if (!scsi_block_when_processing_errors(sdev))
			return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(scsi_ioctl_block_when_processing_errors);
