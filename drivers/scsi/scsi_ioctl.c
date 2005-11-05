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
#include <asm/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_request.h>
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

/*

 * The SCSI_IOCTL_SEND_COMMAND ioctl sends a command out to the SCSI host.
 * The IOCTL_NORMAL_TIMEOUT and NORMAL_RETRIES  variables are used.  
 * 
 * dev is the SCSI device struct ptr, *(int *) arg is the length of the
 * input data, if any, not including the command string & counts, 
 * *((int *)arg + 1) is the output buffer size in bytes.
 * 
 * *(char *) ((int *) arg)[2] the actual command byte.   
 * 
 * Note that if more than MAX_BUF bytes are requested to be transferred,
 * the ioctl will fail with error EINVAL.
 * 
 * This size *does not* include the initial lengths that were passed.
 * 
 * The SCSI command is read from the memory location immediately after the
 * length words, and the input data is right after the command.  The SCSI
 * routines know the command size based on the opcode decode.  
 * 
 * The output area is then filled in starting from the command byte. 
 */

static int ioctl_internal_command(struct scsi_device *sdev, char *cmd,
				  int timeout, int retries)
{
	int result;
	struct scsi_sense_hdr sshdr;

	SCSI_LOG_IOCTL(1, printk("Trying ioctl with scsi command %d\n", *cmd));

	result = scsi_execute_req(sdev, cmd, DMA_NONE, NULL, 0,
				  &sshdr, timeout, retries);

	SCSI_LOG_IOCTL(2, printk("Ioctl returned  0x%x\n", result));

	if ((driver_byte(result) & DRIVER_SENSE) &&
	    (scsi_sense_valid(&sshdr))) {
		switch (sshdr.sense_key) {
		case ILLEGAL_REQUEST:
			if (cmd[0] == ALLOW_MEDIUM_REMOVAL)
				sdev->lockable = 0;
			else
				printk(KERN_INFO "ioctl_internal_command: "
				       "ILLEGAL REQUEST asc=0x%x ascq=0x%x\n",
				       sshdr.asc, sshdr.ascq);
			break;
		case NOT_READY:	/* This happens if there is no disc in drive */
			if (sdev->removable && (cmd[0] != TEST_UNIT_READY)) {
				printk(KERN_INFO "Device not ready. Make sure"
				       " there is a disc in the drive.\n");
				break;
			}
		case UNIT_ATTENTION:
			if (sdev->removable) {
				sdev->changed = 1;
				result = 0;	/* This is no longer considered an error */
				break;
			}
		default:	/* Fall through for non-removable media */
			sdev_printk(KERN_INFO, sdev,
				    "ioctl_internal_command return code = %x\n",
				    result);
			scsi_print_sense_hdr("   ", &sshdr);
			break;
		}
	}

	SCSI_LOG_IOCTL(2, printk("IOCTL Releasing command\n"));
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
 * This interface is deprecated - users should use the scsi generic (sg)
 * interface instead, as this is a more flexible approach to performing
 * generic SCSI commands on a device.
 *
 * The structure that we are passed should look like:
 *
 * struct sdata {
 *  unsigned int inlen;      [i] Length of data to be written to device 
 *  unsigned int outlen;     [i] Length of data to be read from device 
 *  unsigned char cmd[x];    [i] SCSI command (6 <= x <= 12).
 *                           [o] Data read from device starts here.
 *                           [o] On error, sense buffer starts here.
 *  unsigned char wdata[y];  [i] Data written to device starts here.
 * };
 * Notes:
 *   -  The SCSI command length is determined by examining the 1st byte
 *      of the given command. There is no way to override this.
 *   -  Data transfers are limited to PAGE_SIZE (4K on i386, 8K on alpha).
 *   -  The length (x + y) must be at least OMAX_SB_LEN bytes long to
 *      accommodate the sense buffer when an error occurs.
 *      The sense buffer is truncated to OMAX_SB_LEN (16) bytes so that
 *      old code will not be surprised.
 *   -  If a Unix error occurs (e.g. ENOMEM) then the user will receive
 *      a negative return and the Unix error code in 'errno'. 
 *      If the SCSI command succeeds then 0 is returned.
 *      Positive numbers returned are the compacted SCSI error codes (4 
 *      bytes in one int) where the lowest byte is the SCSI status.
 *      See the drivers/scsi/scsi.h file for more information on this.
 *
 */
#define OMAX_SB_LEN 16		/* Old sense buffer length */

int scsi_ioctl_send_command(struct scsi_device *sdev,
			    struct scsi_ioctl_command __user *sic)
{
	char *buf;
	unsigned char cmd[MAX_COMMAND_SIZE];
	unsigned char sense[SCSI_SENSE_BUFFERSIZE];
	char __user *cmd_in;
	unsigned char opcode;
	unsigned int inlen, outlen, cmdlen;
	unsigned int needed, buf_needed;
	int timeout, retries, result;
	int data_direction;
	gfp_t gfp_mask = GFP_KERNEL;

	if (!sic)
		return -EINVAL;

	if (sdev->host->unchecked_isa_dma)
		gfp_mask |= GFP_DMA;

	/*
	 * Verify that we can read at least this much.
	 */
	if (!access_ok(VERIFY_READ, sic, sizeof(Scsi_Ioctl_Command)))
		return -EFAULT;

	if(__get_user(inlen, &sic->inlen))
		return -EFAULT;
		
	if(__get_user(outlen, &sic->outlen))
		return -EFAULT;

	/*
	 * We do not transfer more than MAX_BUF with this interface.
	 * If the user needs to transfer more data than this, they
	 * should use scsi_generics (sg) instead.
	 */
	if (inlen > MAX_BUF)
		return -EINVAL;
	if (outlen > MAX_BUF)
		return -EINVAL;

	cmd_in = sic->data;
	if(get_user(opcode, cmd_in))
		return -EFAULT;

	needed = buf_needed = (inlen > outlen ? inlen : outlen);
	if (buf_needed) {
		buf_needed = (buf_needed + 511) & ~511;
		if (buf_needed > MAX_BUF)
			buf_needed = MAX_BUF;
		buf = kmalloc(buf_needed, gfp_mask);
		if (!buf)
			return -ENOMEM;
		memset(buf, 0, buf_needed);
		if (inlen == 0) {
			data_direction = DMA_FROM_DEVICE;
		} else if (outlen == 0 ) {
			data_direction = DMA_TO_DEVICE;
		} else {
			/*
			 * Can this ever happen?
			 */
			data_direction = DMA_BIDIRECTIONAL;
		}

	} else {
		buf = NULL;
		data_direction = DMA_NONE;
	}

	/*
	 * Obtain the command from the user's address space.
	 */
	cmdlen = COMMAND_SIZE(opcode);
	
	result = -EFAULT;

	if (!access_ok(VERIFY_READ, cmd_in, cmdlen + inlen))
		goto error;

	if(__copy_from_user(cmd, cmd_in, cmdlen))
		goto error;

	/*
	 * Obtain the data to be sent to the device (if any).
	 */

	if(inlen && copy_from_user(buf, cmd_in + cmdlen, inlen))
		goto error;

	switch (opcode) {
	case SEND_DIAGNOSTIC:
	case FORMAT_UNIT:
		timeout = FORMAT_UNIT_TIMEOUT;
		retries = 1;
		break;
	case START_STOP:
		timeout = START_STOP_TIMEOUT;
		retries = NORMAL_RETRIES;
		break;
	case MOVE_MEDIUM:
		timeout = MOVE_MEDIUM_TIMEOUT;
		retries = NORMAL_RETRIES;
		break;
	case READ_ELEMENT_STATUS:
		timeout = READ_ELEMENT_STATUS_TIMEOUT;
		retries = NORMAL_RETRIES;
		break;
	case READ_DEFECT_DATA:
		timeout = READ_DEFECT_DATA_TIMEOUT;
		retries = 1;
		break;
	default:
		timeout = IOCTL_NORMAL_TIMEOUT;
		retries = NORMAL_RETRIES;
		break;
	}

	result = scsi_execute(sdev, cmd, data_direction, buf, needed,
			      sense, timeout, retries, 0);

	/* 
	 * If there was an error condition, pass the info back to the user. 
	 */
	if (result) {
		int sb_len = sizeof(*sense);

		sb_len = (sb_len > OMAX_SB_LEN) ? OMAX_SB_LEN : sb_len;
		if (copy_to_user(cmd_in, sense, sb_len))
			result = -EFAULT;
	} else {
		if (outlen && copy_to_user(cmd_in, buf, outlen))
			result = -EFAULT;
	}	

error:
	kfree(buf);
	return result;
}
EXPORT_SYMBOL(scsi_ioctl_send_command);

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

        if (!dev)
		return -ENXIO;
        return copy_to_user(arg, dev->bus_id, sizeof(dev->bus_id))? -EFAULT: 0;
}


/*
 * the scsi_ioctl() function differs from most ioctls in that it does
 * not take a major/minor number as the dev field.  Rather, it takes
 * a pointer to a scsi_devices[] element, a structure. 
 */
int scsi_ioctl(struct scsi_device *sdev, int cmd, void __user *arg)
{
	char scsi_cmd[MAX_COMMAND_SIZE];

	/* No idea how this happens.... */
	if (!sdev)
		return -ENXIO;

	/*
	 * If we are in the middle of error recovery, don't let anyone
	 * else try and use this device.  Also, if error recovery fails, it
	 * may try and take the device offline, in which case all further
	 * access to the device is prohibited.
	 */
	if (!scsi_block_when_processing_errors(sdev))
		return -ENODEV;

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
	case SCSI_IOCTL_GET_IDLUN:
		if (!access_ok(VERIFY_WRITE, arg, sizeof(struct scsi_idlun)))
			return -EFAULT;

		__put_user((sdev->id & 0xff)
			 + ((sdev->lun & 0xff) << 8)
			 + ((sdev->channel & 0xff) << 16)
			 + ((sdev->host->host_no & 0xff) << 24),
			 &((struct scsi_idlun __user *)arg)->dev_id);
		__put_user(sdev->host->unique_id,
			 &((struct scsi_idlun __user *)arg)->host_unique_id);
		return 0;
	case SCSI_IOCTL_GET_BUS_NUMBER:
		return put_user(sdev->host->host_no, (int __user *)arg);
	case SCSI_IOCTL_PROBE_HOST:
		return ioctl_probe(sdev->host, arg);
	case SCSI_IOCTL_SEND_COMMAND:
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			return -EACCES;
		return scsi_ioctl_send_command(sdev, arg);
	case SCSI_IOCTL_DOORLOCK:
		return scsi_set_medium_removal(sdev, SCSI_REMOVAL_PREVENT);
	case SCSI_IOCTL_DOORUNLOCK:
		return scsi_set_medium_removal(sdev, SCSI_REMOVAL_ALLOW);
	case SCSI_IOCTL_TEST_UNIT_READY:
		return scsi_test_unit_ready(sdev, IOCTL_NORMAL_TIMEOUT,
					    NORMAL_RETRIES);
	case SCSI_IOCTL_START_UNIT:
		scsi_cmd[0] = START_STOP;
		scsi_cmd[1] = 0;
		scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[5] = 0;
		scsi_cmd[4] = 1;
		return ioctl_internal_command(sdev, scsi_cmd,
				     START_STOP_TIMEOUT, NORMAL_RETRIES);
	case SCSI_IOCTL_STOP_UNIT:
		scsi_cmd[0] = START_STOP;
		scsi_cmd[1] = 0;
		scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[5] = 0;
		scsi_cmd[4] = 0;
		return ioctl_internal_command(sdev, scsi_cmd,
				     START_STOP_TIMEOUT, NORMAL_RETRIES);
        case SCSI_IOCTL_GET_PCI:
                return scsi_ioctl_get_pci(sdev, arg);
	default:
		if (sdev->host->hostt->ioctl)
			return sdev->host->hostt->ioctl(sdev, cmd, arg);
	}
	return -EINVAL;
}
EXPORT_SYMBOL(scsi_ioctl);

/*
 * the scsi_nonblock_ioctl() function is designed for ioctls which may
 * be executed even if the device is in recovery.
 */
int scsi_nonblockable_ioctl(struct scsi_device *sdev, int cmd,
			    void __user *arg, struct file *filp)
{
	int val, result;

	/* The first set of iocts may be executed even if we're doing
	 * error processing, as long as the device was opened
	 * non-blocking */
	if (filp && filp->f_flags & O_NONBLOCK) {
		if (scsi_host_in_recovery(sdev->host))
			return -ENODEV;
	} else if (!scsi_block_when_processing_errors(sdev))
		return -ENODEV;

	switch (cmd) {
	case SG_SCSI_RESET:
		result = get_user(val, (int __user *)arg);
		if (result)
			return result;
		if (val == SG_SCSI_RESET_NOTHING)
			return 0;
		switch (val) {
		case SG_SCSI_RESET_DEVICE:
			val = SCSI_TRY_RESET_DEVICE;
			break;
		case SG_SCSI_RESET_BUS:
			val = SCSI_TRY_RESET_BUS;
			break;
		case SG_SCSI_RESET_HOST:
			val = SCSI_TRY_RESET_HOST;
			break;
		default:
			return -EINVAL;
		}
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			return -EACCES;
		return (scsi_reset_provider(sdev, val) ==
			SUCCESS) ? 0 : -EIO;
	}
	return -ENODEV;
}
EXPORT_SYMBOL(scsi_nonblockable_ioctl);
