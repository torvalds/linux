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

	SCSI_LOG_IOCTL(1, sdev_printk(KERN_INFO, sdev,
				      "Trying ioctl with scsi command %d\n", *cmd));

	result = scsi_execute_req(sdev, cmd, DMA_NONE, NULL, 0,
				  &sshdr, timeout, retries, NULL);

	SCSI_LOG_IOCTL(2, sdev_printk(KERN_INFO, sdev,
				      "Ioctl returned  0x%x\n", result));

	if (driver_byte(result) == DRIVER_SENSE &&
	    scsi_sense_valid(&sshdr)) {
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
			/* FALLTHROUGH */
		case UNIT_ATTENTION:
			if (sdev->removable) {
				sdev->changed = 1;
				result = 0;	/* This is no longer considered an error */
				break;
			}
			/* FALLTHROUGH -- for non-removable media */
		default:
			sdev_printk(KERN_INFO, sdev,
				    "ioctl_internal_command return code = %x\n",
				    result);
			scsi_print_sense_hdr(sdev, NULL, &sshdr);
			break;
		}
	}

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


/**
 * scsi_ioctl - Dispatch ioctl to scsi device
 * @sdev: scsi device receiving ioctl
 * @cmd: which ioctl is it
 * @arg: data associated with ioctl
 *
 * Description: The scsi_ioctl() function differs from most ioctls in that it
 * does not take a major/minor number as the dev field.  Rather, it takes
 * a pointer to a &struct scsi_device.
 */
int scsi_ioctl(struct scsi_device *sdev, int cmd, void __user *arg)
{
	char scsi_cmd[MAX_COMMAND_SIZE];
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
	case SCSI_IOCTL_GET_IDLUN:
		if (!access_ok(arg, sizeof(struct scsi_idlun)))
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
		return sg_scsi_ioctl(sdev->request_queue, NULL, 0, arg);
	case SCSI_IOCTL_DOORLOCK:
		return scsi_set_medium_removal(sdev, SCSI_REMOVAL_PREVENT);
	case SCSI_IOCTL_DOORUNLOCK:
		return scsi_set_medium_removal(sdev, SCSI_REMOVAL_ALLOW);
	case SCSI_IOCTL_TEST_UNIT_READY:
		return scsi_test_unit_ready(sdev, IOCTL_NORMAL_TIMEOUT,
					    NORMAL_RETRIES, &sense_hdr);
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
	case SG_SCSI_RESET:
		return scsi_ioctl_reset(sdev, arg);
	default:
		if (sdev->host->hostt->ioctl)
			return sdev->host->hostt->ioctl(sdev, cmd, arg);
	}
	return -EINVAL;
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
