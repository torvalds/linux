/*
 * Target driver for EMC CLARiiON AX/CX-series hardware.
 * Based on code from Lars Marowsky-Bree <lmb@suse.de>
 * and Ed Goggin <egoggin@emc.com>.
 *
 * Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2006 Mike Christie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dh.h>
#include <scsi/scsi_device.h>

#define CLARIION_NAME			"emc_clariion"

#define CLARIION_TRESPASS_PAGE		0x22
#define CLARIION_BUFFER_SIZE		0x80
#define CLARIION_TIMEOUT		(60 * HZ)
#define CLARIION_RETRIES		3
#define CLARIION_UNBOUND_LU		-1

static unsigned char long_trespass[] = {
	0, 0, 0, 0,
	CLARIION_TRESPASS_PAGE,	/* Page code */
	0x09,			/* Page length - 2 */
	0x81,			/* Trespass code + Honor reservation bit */
	0xff, 0xff,		/* Trespass target */
	0, 0, 0, 0, 0, 0	/* Reserved bytes / unknown */
};

static unsigned char long_trespass_hr[] = {
	0, 0, 0, 0,
	CLARIION_TRESPASS_PAGE,	/* Page code */
	0x09,			/* Page length - 2 */
	0x01,			/* Trespass code + Honor reservation bit */
	0xff, 0xff,		/* Trespass target */
	0, 0, 0, 0, 0, 0	/* Reserved bytes / unknown */
};

static unsigned char short_trespass[] = {
	0, 0, 0, 0,
	CLARIION_TRESPASS_PAGE,	/* Page code */
	0x02,			/* Page length - 2 */
	0x81,			/* Trespass code + Honor reservation bit */
	0xff,			/* Trespass target */
};

static unsigned char short_trespass_hr[] = {
	0, 0, 0, 0,
	CLARIION_TRESPASS_PAGE,	/* Page code */
	0x02,			/* Page length - 2 */
	0x01,			/* Trespass code + Honor reservation bit */
	0xff,			/* Trespass target */
};

struct clariion_dh_data {
	/*
	 * Use short trespass command (FC-series) or the long version
	 * (default for AX/CX CLARiiON arrays).
	 */
	unsigned short_trespass;
	/*
	 * Whether or not (default) to honor SCSI reservations when
	 * initiating a switch-over.
	 */
	unsigned hr;
	/* I/O buffer for both MODE_SELECT and INQUIRY commands. */
	char buffer[CLARIION_BUFFER_SIZE];
	/*
	 * SCSI sense buffer for commands -- assumes serial issuance
	 * and completion sequence of all commands for same multipath.
	 */
	unsigned char sense[SCSI_SENSE_BUFFERSIZE];
	/* which SP (A=0,B=1,UNBOUND=-1) is dflt SP for path's mapped dev */
	int default_sp;
	/* which SP (A=0,B=1,UNBOUND=-1) is active for path's mapped dev */
	int current_sp;
};

static inline struct clariion_dh_data
			*get_clariion_data(struct scsi_device *sdev)
{
	struct scsi_dh_data *scsi_dh_data = sdev->scsi_dh_data;
	BUG_ON(scsi_dh_data == NULL);
	return ((struct clariion_dh_data *) scsi_dh_data->buf);
}

/*
 * Parse MODE_SELECT cmd reply.
 */
static int trespass_endio(struct scsi_device *sdev, int result)
{
	int err = SCSI_DH_OK;
	struct scsi_sense_hdr sshdr;
	struct clariion_dh_data *csdev = get_clariion_data(sdev);
	char *sense = csdev->sense;

	if (status_byte(result) == CHECK_CONDITION &&
	    scsi_normalize_sense(sense, SCSI_SENSE_BUFFERSIZE, &sshdr)) {
		sdev_printk(KERN_ERR, sdev, "Found valid sense data 0x%2x, "
			    "0x%2x, 0x%2x while sending CLARiiON trespass "
			    "command.\n", sshdr.sense_key, sshdr.asc,
			     sshdr.ascq);

		if ((sshdr.sense_key == 0x05) && (sshdr.asc == 0x04) &&
		     (sshdr.ascq == 0x00)) {
			/*
			 * Array based copy in progress -- do not send
			 * mode_select or copy will be aborted mid-stream.
			 */
			sdev_printk(KERN_INFO, sdev, "Array Based Copy in "
				    "progress while sending CLARiiON trespass "
				    "command.\n");
			err = SCSI_DH_DEV_TEMP_BUSY;
		} else if ((sshdr.sense_key == 0x02) && (sshdr.asc == 0x04) &&
			    (sshdr.ascq == 0x03)) {
			/*
			 * LUN Not Ready - Manual Intervention Required
			 * indicates in-progress ucode upgrade (NDU).
			 */
			sdev_printk(KERN_INFO, sdev, "Detected in-progress "
				    "ucode upgrade NDU operation while sending "
				    "CLARiiON trespass command.\n");
			err = SCSI_DH_DEV_TEMP_BUSY;
		} else
			err = SCSI_DH_DEV_FAILED;
	} else if (result) {
		sdev_printk(KERN_ERR, sdev, "Error 0x%x while sending "
			    "CLARiiON trespass command.\n", result);
		err = SCSI_DH_IO;
	}

	return err;
}

static int parse_sp_info_reply(struct scsi_device *sdev, int result,
		int *default_sp, int *current_sp, int *new_current_sp)
{
	int err = SCSI_DH_OK;
	struct clariion_dh_data *csdev = get_clariion_data(sdev);

	if (result == 0) {
		/* check for in-progress ucode upgrade (NDU) */
		if (csdev->buffer[48] != 0) {
			sdev_printk(KERN_NOTICE, sdev, "Detected in-progress "
			       "ucode upgrade NDU operation while finding "
			       "current active SP.");
			err = SCSI_DH_DEV_TEMP_BUSY;
		} else {
			*default_sp = csdev->buffer[5];

			if (csdev->buffer[4] == 2)
				/* SP for path is current */
				*current_sp = csdev->buffer[8];
			else {
				if (csdev->buffer[4] == 1)
					/* SP for this path is NOT current */
					if (csdev->buffer[8] == 0)
						*current_sp = 1;
					else
						*current_sp = 0;
				else
					/* unbound LU or LUNZ */
					*current_sp = CLARIION_UNBOUND_LU;
			}
			*new_current_sp =  csdev->buffer[8];
		}
	} else {
		struct scsi_sense_hdr sshdr;

		err = SCSI_DH_IO;

		if (scsi_normalize_sense(csdev->sense, SCSI_SENSE_BUFFERSIZE,
							   &sshdr))
			sdev_printk(KERN_ERR, sdev, "Found valid sense data "
			      "0x%2x, 0x%2x, 0x%2x while finding current "
			      "active SP.", sshdr.sense_key, sshdr.asc,
			      sshdr.ascq);
		else
			sdev_printk(KERN_ERR, sdev, "Error 0x%x finding "
			      "current active SP.", result);
	}

	return err;
}

static int sp_info_endio(struct scsi_device *sdev, int result,
					int mode_select_sent, int *done)
{
	struct clariion_dh_data *csdev = get_clariion_data(sdev);
	int err_flags, default_sp, current_sp, new_current_sp;

	err_flags = parse_sp_info_reply(sdev, result, &default_sp,
					     &current_sp, &new_current_sp);

	if (err_flags != SCSI_DH_OK)
		goto done;

	if (mode_select_sent) {
		csdev->default_sp = default_sp;
		csdev->current_sp = current_sp;
	} else {
		/*
		 * Issue the actual module_selec request IFF either
		 * (1) we do not know the identity of the current SP OR
		 * (2) what we think we know is actually correct.
		 */
		if ((current_sp != CLARIION_UNBOUND_LU) &&
		    (new_current_sp != current_sp)) {

			csdev->default_sp = default_sp;
			csdev->current_sp = current_sp;

			sdev_printk(KERN_INFO, sdev, "Ignoring path group "
			       "switch-over command for CLARiiON SP%s since "
			       " mapped device is already initialized.",
			       current_sp ? "B" : "A");
			if (done)
				*done = 1; /* as good as doing it */
		}
	}
done:
	return err_flags;
}

/*
* Get block request for REQ_BLOCK_PC command issued to path.  Currently
* limited to MODE_SELECT (trespass) and INQUIRY (VPD page 0xC0) commands.
*
* Uses data and sense buffers in hardware handler context structure and
* assumes serial servicing of commands, both issuance and completion.
*/
static struct request *get_req(struct scsi_device *sdev, int cmd)
{
	struct clariion_dh_data *csdev = get_clariion_data(sdev);
	struct request *rq;
	unsigned char *page22;
	int len = 0;

	rq = blk_get_request(sdev->request_queue,
			(cmd == MODE_SELECT) ? WRITE : READ, GFP_ATOMIC);
	if (!rq) {
		sdev_printk(KERN_INFO, sdev, "get_req: blk_get_request failed");
		return NULL;
	}

	memset(&rq->cmd, 0, BLK_MAX_CDB);
	rq->cmd[0] = cmd;
	rq->cmd_len = COMMAND_SIZE(rq->cmd[0]);

	switch (cmd) {
	case MODE_SELECT:
		if (csdev->short_trespass) {
			page22 = csdev->hr ? short_trespass_hr : short_trespass;
			len = sizeof(short_trespass);
		} else {
			page22 = csdev->hr ? long_trespass_hr : long_trespass;
			len = sizeof(long_trespass);
		}
		/*
		 * Can't DMA from kernel BSS -- must copy selected trespass
		 * command mode page contents to context buffer which is
		 * allocated by kmalloc.
		 */
		BUG_ON((len > CLARIION_BUFFER_SIZE));
		memcpy(csdev->buffer, page22, len);
		rq->cmd_flags |= REQ_RW;
		rq->cmd[1] = 0x10;
		break;
	case INQUIRY:
		rq->cmd[1] = 0x1;
		rq->cmd[2] = 0xC0;
		len = CLARIION_BUFFER_SIZE;
		memset(csdev->buffer, 0, CLARIION_BUFFER_SIZE);
		break;
	default:
		BUG_ON(1);
		break;
	}

	rq->cmd[4] = len;
	rq->cmd_type = REQ_TYPE_BLOCK_PC;
	rq->cmd_flags |= REQ_FAILFAST;
	rq->timeout = CLARIION_TIMEOUT;
	rq->retries = CLARIION_RETRIES;

	rq->sense = csdev->sense;
	memset(rq->sense, 0, SCSI_SENSE_BUFFERSIZE);
	rq->sense_len = 0;

	if (blk_rq_map_kern(sdev->request_queue, rq, csdev->buffer,
							len, GFP_ATOMIC)) {
		__blk_put_request(rq->q, rq);
		return NULL;
	}

	return rq;
}

static int send_cmd(struct scsi_device *sdev, int cmd)
{
	struct request *rq = get_req(sdev, cmd);

	if (!rq)
		return SCSI_DH_RES_TEMP_UNAVAIL;

	return blk_execute_rq(sdev->request_queue, NULL, rq, 1);
}

static int clariion_activate(struct scsi_device *sdev)
{
	int result, done = 0;

	result = send_cmd(sdev, INQUIRY);
	result = sp_info_endio(sdev, result, 0, &done);
	if (result || done)
		goto done;

	result = send_cmd(sdev, MODE_SELECT);
	result = trespass_endio(sdev, result);
	if (result)
		goto done;

	result = send_cmd(sdev, INQUIRY);
	result = sp_info_endio(sdev, result, 1, NULL);
done:
	return result;
}

static int clariion_check_sense(struct scsi_device *sdev,
				struct scsi_sense_hdr *sense_hdr)
{
	switch (sense_hdr->sense_key) {
	case NOT_READY:
		if (sense_hdr->asc == 0x04 && sense_hdr->ascq == 0x03)
			/*
			 * LUN Not Ready - Manual Intervention Required
			 * indicates this is a passive path.
			 *
			 * FIXME: However, if this is seen and EVPD C0
			 * indicates that this is due to a NDU in
			 * progress, we should set FAIL_PATH too.
			 * This indicates we might have to do a SCSI
			 * inquiry in the end_io path. Ugh.
			 *
			 * Can return FAILED only when we want the error
			 * recovery process to kick in.
			 */
			return SUCCESS;
		break;
	case ILLEGAL_REQUEST:
		if (sense_hdr->asc == 0x25 && sense_hdr->ascq == 0x01)
			/*
			 * An array based copy is in progress. Do not
			 * fail the path, do not bypass to another PG,
			 * do not retry. Fail the IO immediately.
			 * (Actually this is the same conclusion as in
			 * the default handler, but lets make sure.)
			 *
			 * Can return FAILED only when we want the error
			 * recovery process to kick in.
			 */
			return SUCCESS;
		break;
	case UNIT_ATTENTION:
		if (sense_hdr->asc == 0x29 && sense_hdr->ascq == 0x00)
			/*
			 * Unit Attention Code. This is the first IO
			 * to the new path, so just retry.
			 */
			return NEEDS_RETRY;
		break;
	}

	/* success just means we do not care what scsi-ml does */
	return SUCCESS;
}

static const struct {
	char *vendor;
	char *model;
} clariion_dev_list[] = {
	{"DGC", "RAID"},
	{"DGC", "DISK"},
	{NULL, NULL},
};

static int clariion_bus_notify(struct notifier_block *, unsigned long, void *);

static struct scsi_device_handler clariion_dh = {
	.name		= CLARIION_NAME,
	.module		= THIS_MODULE,
	.nb.notifier_call = clariion_bus_notify,
	.check_sense	= clariion_check_sense,
	.activate	= clariion_activate,
};

/*
 * TODO: need some interface so we can set trespass values
 */
static int clariion_bus_notify(struct notifier_block *nb,
				unsigned long action, void *data)
{
	struct device *dev = data;
	struct scsi_device *sdev = to_scsi_device(dev);
	struct scsi_dh_data *scsi_dh_data;
	struct clariion_dh_data *h;
	int i, found = 0;
	unsigned long flags;

	if (action == BUS_NOTIFY_ADD_DEVICE) {
		for (i = 0; clariion_dev_list[i].vendor; i++) {
			if (!strncmp(sdev->vendor, clariion_dev_list[i].vendor,
				     strlen(clariion_dev_list[i].vendor)) &&
			    !strncmp(sdev->model, clariion_dev_list[i].model,
				     strlen(clariion_dev_list[i].model))) {
				found = 1;
				break;
			}
		}
		if (!found)
			goto out;

		scsi_dh_data = kzalloc(sizeof(struct scsi_device_handler *)
				+ sizeof(*h) , GFP_KERNEL);
		if (!scsi_dh_data) {
			sdev_printk(KERN_ERR, sdev, "Attach failed %s.\n",
				    CLARIION_NAME);
			goto out;
		}

		scsi_dh_data->scsi_dh = &clariion_dh;
		h = (struct clariion_dh_data *) scsi_dh_data->buf;
		h->default_sp = CLARIION_UNBOUND_LU;
		h->current_sp = CLARIION_UNBOUND_LU;

		spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
		sdev->scsi_dh_data = scsi_dh_data;
		spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);

		sdev_printk(KERN_NOTICE, sdev, "Attached %s.\n", CLARIION_NAME);
		try_module_get(THIS_MODULE);

	} else if (action == BUS_NOTIFY_DEL_DEVICE) {
		if (sdev->scsi_dh_data == NULL ||
				sdev->scsi_dh_data->scsi_dh != &clariion_dh)
			goto out;

		spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
		scsi_dh_data = sdev->scsi_dh_data;
		sdev->scsi_dh_data = NULL;
		spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);

		sdev_printk(KERN_NOTICE, sdev, "Dettached %s.\n",
			    CLARIION_NAME);

		kfree(scsi_dh_data);
		module_put(THIS_MODULE);
	}

out:
	return 0;
}

static int __init clariion_init(void)
{
	int r;

	r = scsi_register_device_handler(&clariion_dh);
	if (r != 0)
		printk(KERN_ERR "Failed to register scsi device handler.");
	return r;
}

static void __exit clariion_exit(void)
{
	scsi_unregister_device_handler(&clariion_dh);
}

module_init(clariion_init);
module_exit(clariion_exit);

MODULE_DESCRIPTION("EMC CX/AX/FC-family driver");
MODULE_AUTHOR("Mike Christie <michaelc@cs.wisc.edu>, Chandra Seetharaman <sekharan@us.ibm.com>");
MODULE_LICENSE("GPL");
