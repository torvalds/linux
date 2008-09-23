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

#define CLARIION_NAME			"emc"

#define CLARIION_TRESPASS_PAGE		0x22
#define CLARIION_BUFFER_SIZE		0xFC
#define CLARIION_TIMEOUT		(60 * HZ)
#define CLARIION_RETRIES		3
#define CLARIION_UNBOUND_LU		-1
#define CLARIION_SP_A			0
#define CLARIION_SP_B			1

/* Flags */
#define CLARIION_SHORT_TRESPASS		1
#define CLARIION_HONOR_RESERVATIONS	2

/* LUN states */
#define CLARIION_LUN_UNINITIALIZED	-1
#define CLARIION_LUN_UNBOUND		0
#define CLARIION_LUN_BOUND		1
#define CLARIION_LUN_OWNED		2

static unsigned char long_trespass[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	CLARIION_TRESPASS_PAGE,	/* Page code */
	0x09,			/* Page length - 2 */
	0x01,			/* Trespass code */
	0xff, 0xff,		/* Trespass target */
	0, 0, 0, 0, 0, 0	/* Reserved bytes / unknown */
};

static unsigned char short_trespass[] = {
	0, 0, 0, 0,
	CLARIION_TRESPASS_PAGE,	/* Page code */
	0x02,			/* Page length - 2 */
	0x01,			/* Trespass code */
	0xff,			/* Trespass target */
};

static const char * lun_state[] =
{
    "not bound",
    "bound",
    "owned",
};

struct clariion_dh_data {
	/*
	 * Flags:
	 *  CLARIION_SHORT_TRESPASS
	 * Use short trespass command (FC-series) or the long version
	 * (default for AX/CX CLARiiON arrays).
	 *
	 *  CLARIION_HONOR_RESERVATIONS
	 * Whether or not (default) to honor SCSI reservations when
	 * initiating a switch-over.
	 */
	unsigned flags;
	/*
	 * I/O buffer for both MODE_SELECT and INQUIRY commands.
	 */
	unsigned char buffer[CLARIION_BUFFER_SIZE];
	/*
	 * SCSI sense buffer for commands -- assumes serial issuance
	 * and completion sequence of all commands for same multipath.
	 */
	unsigned char sense[SCSI_SENSE_BUFFERSIZE];
	unsigned int senselen;
	/*
	 * LUN state
	 */
	int lun_state;
	/*
	 * SP Port number
	 */
	int port;
	/*
	 * which SP (A=0,B=1,UNBOUND=-1) is the default SP for this
	 * path's mapped LUN
	 */
	int default_sp;
	/*
	 * which SP (A=0,B=1,UNBOUND=-1) is the active SP for this
	 * path's mapped LUN
	 */
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
static int trespass_endio(struct scsi_device *sdev, char *sense)
{
	int err = SCSI_DH_IO;
	struct scsi_sense_hdr sshdr;

	if (!scsi_normalize_sense(sense, SCSI_SENSE_BUFFERSIZE, &sshdr)) {
		sdev_printk(KERN_ERR, sdev, "%s: Found valid sense data 0x%2x, "
			    "0x%2x, 0x%2x while sending CLARiiON trespass "
			    "command.\n", CLARIION_NAME, sshdr.sense_key,
			    sshdr.asc, sshdr.ascq);

		if ((sshdr.sense_key == 0x05) && (sshdr.asc == 0x04) &&
		     (sshdr.ascq == 0x00)) {
			/*
			 * Array based copy in progress -- do not send
			 * mode_select or copy will be aborted mid-stream.
			 */
			sdev_printk(KERN_INFO, sdev, "%s: Array Based Copy in "
				    "progress while sending CLARiiON trespass "
				    "command.\n", CLARIION_NAME);
			err = SCSI_DH_DEV_TEMP_BUSY;
		} else if ((sshdr.sense_key == 0x02) && (sshdr.asc == 0x04) &&
			    (sshdr.ascq == 0x03)) {
			/*
			 * LUN Not Ready - Manual Intervention Required
			 * indicates in-progress ucode upgrade (NDU).
			 */
			sdev_printk(KERN_INFO, sdev, "%s: Detected in-progress "
				    "ucode upgrade NDU operation while sending "
				    "CLARiiON trespass command.\n", CLARIION_NAME);
			err = SCSI_DH_DEV_TEMP_BUSY;
		} else
			err = SCSI_DH_DEV_FAILED;
	} else {
		sdev_printk(KERN_INFO, sdev,
			    "%s: failed to send MODE SELECT, no sense available\n",
			    CLARIION_NAME);
	}
	return err;
}

static int parse_sp_info_reply(struct scsi_device *sdev,
			       struct clariion_dh_data *csdev)
{
	int err = SCSI_DH_OK;

	/* check for in-progress ucode upgrade (NDU) */
	if (csdev->buffer[48] != 0) {
		sdev_printk(KERN_NOTICE, sdev, "%s: Detected in-progress "
			    "ucode upgrade NDU operation while finding "
			    "current active SP.", CLARIION_NAME);
		err = SCSI_DH_DEV_TEMP_BUSY;
		goto out;
	}
	if (csdev->buffer[4] > 2) {
		/* Invalid buffer format */
		sdev_printk(KERN_NOTICE, sdev,
			    "%s: invalid VPD page 0xC0 format\n",
			    CLARIION_NAME);
		err = SCSI_DH_NOSYS;
		goto out;
	}
	switch (csdev->buffer[28] & 0x0f) {
	case 6:
		sdev_printk(KERN_NOTICE, sdev,
			    "%s: ALUA failover mode detected\n",
			    CLARIION_NAME);
		break;
	case 4:
		/* Linux failover */
		break;
	default:
		sdev_printk(KERN_WARNING, sdev,
			    "%s: Invalid failover mode %d\n",
			    CLARIION_NAME, csdev->buffer[28] & 0x0f);
		err = SCSI_DH_NOSYS;
		goto out;
	}

	csdev->default_sp = csdev->buffer[5];
	csdev->lun_state = csdev->buffer[4];
	csdev->current_sp = csdev->buffer[8];
	csdev->port = csdev->buffer[7];

out:
	return err;
}

#define emc_default_str "FC (Legacy)"

static char * parse_sp_model(struct scsi_device *sdev, unsigned char *buffer)
{
	unsigned char len = buffer[4] + 5;
	char *sp_model = NULL;
	unsigned char sp_len, serial_len;

	if (len < 160) {
		sdev_printk(KERN_WARNING, sdev,
			    "%s: Invalid information section length %d\n",
			    CLARIION_NAME, len);
		/* Check for old FC arrays */
		if (!strncmp(buffer + 8, "DGC", 3)) {
			/* Old FC array, not supporting extended information */
			sp_model = emc_default_str;
		}
		goto out;
	}

	/*
	 * Parse extended information for SP model number
	 */
	serial_len = buffer[160];
	if (serial_len == 0 || serial_len + 161 > len) {
		sdev_printk(KERN_WARNING, sdev,
			    "%s: Invalid array serial number length %d\n",
			    CLARIION_NAME, serial_len);
		goto out;
	}
	sp_len = buffer[99];
	if (sp_len == 0 || serial_len + sp_len + 161 > len) {
		sdev_printk(KERN_WARNING, sdev,
			    "%s: Invalid model number length %d\n",
			    CLARIION_NAME, sp_len);
		goto out;
	}
	sp_model = &buffer[serial_len + 161];
	/* Strip whitespace at the end */
	while (sp_len > 1 && sp_model[sp_len - 1] == ' ')
		sp_len--;

	sp_model[sp_len] = '\0';

out:
	return sp_model;
}

/*
 * Get block request for REQ_BLOCK_PC command issued to path.  Currently
 * limited to MODE_SELECT (trespass) and INQUIRY (VPD page 0xC0) commands.
 *
 * Uses data and sense buffers in hardware handler context structure and
 * assumes serial servicing of commands, both issuance and completion.
 */
static struct request *get_req(struct scsi_device *sdev, int cmd,
				unsigned char *buffer)
{
	struct request *rq;
	int len = 0;

	rq = blk_get_request(sdev->request_queue,
			(cmd == MODE_SELECT) ? WRITE : READ, GFP_NOIO);
	if (!rq) {
		sdev_printk(KERN_INFO, sdev, "get_req: blk_get_request failed");
		return NULL;
	}

	rq->cmd_len = COMMAND_SIZE(cmd);
	rq->cmd[0] = cmd;

	switch (cmd) {
	case MODE_SELECT:
		len = sizeof(short_trespass);
		rq->cmd_flags |= REQ_RW;
		rq->cmd[1] = 0x10;
		break;
	case MODE_SELECT_10:
		len = sizeof(long_trespass);
		rq->cmd_flags |= REQ_RW;
		rq->cmd[1] = 0x10;
		break;
	case INQUIRY:
		len = CLARIION_BUFFER_SIZE;
		memset(buffer, 0, len);
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

	if (blk_rq_map_kern(rq->q, rq, buffer, len, GFP_NOIO)) {
		blk_put_request(rq);
		return NULL;
	}

	return rq;
}

static int send_inquiry_cmd(struct scsi_device *sdev, int page,
			    struct clariion_dh_data *csdev)
{
	struct request *rq = get_req(sdev, INQUIRY, csdev->buffer);
	int err;

	if (!rq)
		return SCSI_DH_RES_TEMP_UNAVAIL;

	rq->sense = csdev->sense;
	memset(rq->sense, 0, SCSI_SENSE_BUFFERSIZE);
	rq->sense_len = csdev->senselen = 0;

	rq->cmd[0] = INQUIRY;
	if (page != 0) {
		rq->cmd[1] = 1;
		rq->cmd[2] = page;
	}
	err = blk_execute_rq(sdev->request_queue, NULL, rq, 1);
	if (err == -EIO) {
		sdev_printk(KERN_INFO, sdev,
			    "%s: failed to send %s INQUIRY: %x\n",
			    CLARIION_NAME, page?"EVPD":"standard",
			    rq->errors);
		csdev->senselen = rq->sense_len;
		err = SCSI_DH_IO;
	}

	blk_put_request(rq);

	return err;
}

static int send_trespass_cmd(struct scsi_device *sdev,
			    struct clariion_dh_data *csdev)
{
	struct request *rq;
	unsigned char *page22;
	int err, len, cmd;

	if (csdev->flags & CLARIION_SHORT_TRESPASS) {
		page22 = short_trespass;
		if (!(csdev->flags & CLARIION_HONOR_RESERVATIONS))
			/* Set Honor Reservations bit */
			page22[6] |= 0x80;
		len = sizeof(short_trespass);
		cmd = MODE_SELECT;
	} else {
		page22 = long_trespass;
		if (!(csdev->flags & CLARIION_HONOR_RESERVATIONS))
			/* Set Honor Reservations bit */
			page22[10] |= 0x80;
		len = sizeof(long_trespass);
		cmd = MODE_SELECT_10;
	}
	BUG_ON((len > CLARIION_BUFFER_SIZE));
	memcpy(csdev->buffer, page22, len);

	rq = get_req(sdev, cmd, csdev->buffer);
	if (!rq)
		return SCSI_DH_RES_TEMP_UNAVAIL;

	rq->sense = csdev->sense;
	memset(rq->sense, 0, SCSI_SENSE_BUFFERSIZE);
	rq->sense_len = csdev->senselen = 0;

	err = blk_execute_rq(sdev->request_queue, NULL, rq, 1);
	if (err == -EIO) {
		if (rq->sense_len) {
			err = trespass_endio(sdev, csdev->sense);
		} else {
			sdev_printk(KERN_INFO, sdev,
				    "%s: failed to send MODE SELECT: %x\n",
				    CLARIION_NAME, rq->errors);
		}
	}

	blk_put_request(rq);

	return err;
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
			return ADD_TO_MLQUEUE;
		break;
	}

	return SCSI_RETURN_NOT_HANDLED;
}

static int clariion_prep_fn(struct scsi_device *sdev, struct request *req)
{
	struct clariion_dh_data *h = get_clariion_data(sdev);
	int ret = BLKPREP_OK;

	if (h->lun_state != CLARIION_LUN_OWNED) {
		ret = BLKPREP_KILL;
		req->cmd_flags |= REQ_QUIET;
	}
	return ret;

}

static int clariion_std_inquiry(struct scsi_device *sdev,
				struct clariion_dh_data *csdev)
{
	int err;
	char *sp_model;

	err = send_inquiry_cmd(sdev, 0, csdev);
	if (err != SCSI_DH_OK && csdev->senselen) {
		struct scsi_sense_hdr sshdr;

		if (scsi_normalize_sense(csdev->sense, SCSI_SENSE_BUFFERSIZE,
					 &sshdr)) {
			sdev_printk(KERN_ERR, sdev, "%s: INQUIRY sense code "
				    "%02x/%02x/%02x\n", CLARIION_NAME,
				    sshdr.sense_key, sshdr.asc, sshdr.ascq);
		}
		err = SCSI_DH_IO;
		goto out;
	}

	sp_model = parse_sp_model(sdev, csdev->buffer);
	if (!sp_model) {
		err = SCSI_DH_DEV_UNSUPP;
		goto out;
	}

	/*
	 * FC Series arrays do not support long trespass
	 */
	if (!strlen(sp_model) || !strncmp(sp_model, "FC",2))
		csdev->flags |= CLARIION_SHORT_TRESPASS;

	sdev_printk(KERN_INFO, sdev,
		    "%s: detected Clariion %s, flags %x\n",
		    CLARIION_NAME, sp_model, csdev->flags);
out:
	return err;
}

static int clariion_send_inquiry(struct scsi_device *sdev,
				 struct clariion_dh_data *csdev)
{
	int err, retry = CLARIION_RETRIES;

retry:
	err = send_inquiry_cmd(sdev, 0xC0, csdev);
	if (err != SCSI_DH_OK && csdev->senselen) {
		struct scsi_sense_hdr sshdr;

		err = scsi_normalize_sense(csdev->sense, SCSI_SENSE_BUFFERSIZE,
					   &sshdr);
		if (!err)
			return SCSI_DH_IO;

		err = clariion_check_sense(sdev, &sshdr);
		if (retry > 0 && err == ADD_TO_MLQUEUE) {
			retry--;
			goto retry;
		}
		sdev_printk(KERN_ERR, sdev, "%s: INQUIRY sense code "
			    "%02x/%02x/%02x\n", CLARIION_NAME,
			      sshdr.sense_key, sshdr.asc, sshdr.ascq);
		err = SCSI_DH_IO;
	} else {
		err = parse_sp_info_reply(sdev, csdev);
	}
	return err;
}

static int clariion_activate(struct scsi_device *sdev)
{
	struct clariion_dh_data *csdev = get_clariion_data(sdev);
	int result;

	result = clariion_send_inquiry(sdev, csdev);
	if (result != SCSI_DH_OK)
		goto done;

	if (csdev->lun_state == CLARIION_LUN_OWNED)
		goto done;

	result = send_trespass_cmd(sdev, csdev);
	if (result != SCSI_DH_OK)
		goto done;
	sdev_printk(KERN_INFO, sdev,"%s: %s trespass command sent\n",
		    CLARIION_NAME,
		    csdev->flags&CLARIION_SHORT_TRESPASS?"short":"long" );

	/* Update status */
	result = clariion_send_inquiry(sdev, csdev);
	if (result != SCSI_DH_OK)
		goto done;

done:
	sdev_printk(KERN_INFO, sdev,
		    "%s: at SP %c Port %d (%s, default SP %c)\n",
		    CLARIION_NAME, csdev->current_sp + 'A',
		    csdev->port, lun_state[csdev->lun_state],
		    csdev->default_sp + 'A');

	return result;
}

static const struct scsi_dh_devlist clariion_dev_list[] = {
	{"DGC", "RAID"},
	{"DGC", "DISK"},
	{"DGC", "VRAID"},
	{NULL, NULL},
};

static int clariion_bus_attach(struct scsi_device *sdev);
static void clariion_bus_detach(struct scsi_device *sdev);

static struct scsi_device_handler clariion_dh = {
	.name		= CLARIION_NAME,
	.module		= THIS_MODULE,
	.devlist	= clariion_dev_list,
	.attach		= clariion_bus_attach,
	.detach		= clariion_bus_detach,
	.check_sense	= clariion_check_sense,
	.activate	= clariion_activate,
	.prep_fn	= clariion_prep_fn,
};

/*
 * TODO: need some interface so we can set trespass values
 */
static int clariion_bus_attach(struct scsi_device *sdev)
{
	struct scsi_dh_data *scsi_dh_data;
	struct clariion_dh_data *h;
	unsigned long flags;
	int err;

	scsi_dh_data = kzalloc(sizeof(struct scsi_device_handler *)
			       + sizeof(*h) , GFP_KERNEL);
	if (!scsi_dh_data) {
		sdev_printk(KERN_ERR, sdev, "%s: Attach failed\n",
			    CLARIION_NAME);
		return -ENOMEM;
	}

	scsi_dh_data->scsi_dh = &clariion_dh;
	h = (struct clariion_dh_data *) scsi_dh_data->buf;
	h->lun_state = CLARIION_LUN_UNINITIALIZED;
	h->default_sp = CLARIION_UNBOUND_LU;
	h->current_sp = CLARIION_UNBOUND_LU;

	err = clariion_std_inquiry(sdev, h);
	if (err != SCSI_DH_OK)
		goto failed;

	err = clariion_send_inquiry(sdev, h);
	if (err != SCSI_DH_OK)
		goto failed;

	if (!try_module_get(THIS_MODULE))
		goto failed;

	spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
	sdev->scsi_dh_data = scsi_dh_data;
	spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);

	sdev_printk(KERN_INFO, sdev,
		    "%s: connected to SP %c Port %d (%s, default SP %c)\n",
		    CLARIION_NAME, h->current_sp + 'A',
		    h->port, lun_state[h->lun_state],
		    h->default_sp + 'A');

	return 0;

failed:
	kfree(scsi_dh_data);
	sdev_printk(KERN_ERR, sdev, "%s: not attached\n",
		    CLARIION_NAME);
	return -EINVAL;
}

static void clariion_bus_detach(struct scsi_device *sdev)
{
	struct scsi_dh_data *scsi_dh_data;
	unsigned long flags;

	spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
	scsi_dh_data = sdev->scsi_dh_data;
	sdev->scsi_dh_data = NULL;
	spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);

	sdev_printk(KERN_NOTICE, sdev, "%s: Detached\n",
		    CLARIION_NAME);

	kfree(scsi_dh_data);
	module_put(THIS_MODULE);
}

static int __init clariion_init(void)
{
	int r;

	r = scsi_register_device_handler(&clariion_dh);
	if (r != 0)
		printk(KERN_ERR "%s: Failed to register scsi device handler.",
			CLARIION_NAME);
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
