/*
 * Generic SCSI-3 ALUA SCSI Device Handler
 *
 * Copyright (C) 2007, 2008 Hannes Reinecke, SUSE Linux Products GmbH.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dh.h>

#define ALUA_DH_NAME "alua"
#define ALUA_DH_VER "1.2"

#define TPGS_STATE_OPTIMIZED		0x0
#define TPGS_STATE_NONOPTIMIZED		0x1
#define TPGS_STATE_STANDBY		0x2
#define TPGS_STATE_UNAVAILABLE		0x3
#define TPGS_STATE_OFFLINE		0xe
#define TPGS_STATE_TRANSITIONING	0xf

#define TPGS_SUPPORT_NONE		0x00
#define TPGS_SUPPORT_OPTIMIZED		0x01
#define TPGS_SUPPORT_NONOPTIMIZED	0x02
#define TPGS_SUPPORT_STANDBY		0x04
#define TPGS_SUPPORT_UNAVAILABLE	0x08
#define TPGS_SUPPORT_OFFLINE		0x40
#define TPGS_SUPPORT_TRANSITION		0x80

#define TPGS_MODE_UNINITIALIZED		 -1
#define TPGS_MODE_NONE			0x0
#define TPGS_MODE_IMPLICIT		0x1
#define TPGS_MODE_EXPLICIT		0x2

#define ALUA_INQUIRY_SIZE		36
#define ALUA_FAILOVER_TIMEOUT		(60 * HZ)
#define ALUA_FAILOVER_RETRIES		5

struct alua_dh_data {
	int			group_id;
	int			rel_port;
	int			tpgs;
	int			state;
	unsigned char		inq[ALUA_INQUIRY_SIZE];
	unsigned char		*buff;
	int			bufflen;
	unsigned char		sense[SCSI_SENSE_BUFFERSIZE];
	int			senselen;
};

#define ALUA_POLICY_SWITCH_CURRENT	0
#define ALUA_POLICY_SWITCH_ALL		1

static inline struct alua_dh_data *get_alua_data(struct scsi_device *sdev)
{
	struct scsi_dh_data *scsi_dh_data = sdev->scsi_dh_data;
	BUG_ON(scsi_dh_data == NULL);
	return ((struct alua_dh_data *) scsi_dh_data->buf);
}

static int realloc_buffer(struct alua_dh_data *h, unsigned len)
{
	if (h->buff && h->buff != h->inq)
		kfree(h->buff);

	h->buff = kmalloc(len, GFP_NOIO);
	if (!h->buff) {
		h->buff = h->inq;
		h->bufflen = ALUA_INQUIRY_SIZE;
		return 1;
	}
	h->bufflen = len;
	return 0;
}

static struct request *get_alua_req(struct scsi_device *sdev,
				    void *buffer, unsigned buflen, int rw)
{
	struct request *rq;
	struct request_queue *q = sdev->request_queue;

	rq = blk_get_request(q, rw, GFP_NOIO);

	if (!rq) {
		sdev_printk(KERN_INFO, sdev,
			    "%s: blk_get_request failed\n", __func__);
		return NULL;
	}

	if (buflen && blk_rq_map_kern(q, rq, buffer, buflen, GFP_NOIO)) {
		blk_put_request(rq);
		sdev_printk(KERN_INFO, sdev,
			    "%s: blk_rq_map_kern failed\n", __func__);
		return NULL;
	}

	rq->cmd_type = REQ_TYPE_BLOCK_PC;
	rq->cmd_flags |= REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT |
			 REQ_FAILFAST_DRIVER;
	rq->retries = ALUA_FAILOVER_RETRIES;
	rq->timeout = ALUA_FAILOVER_TIMEOUT;

	return rq;
}

/*
 * submit_std_inquiry - Issue a standard INQUIRY command
 * @sdev: sdev the command should be send to
 */
static int submit_std_inquiry(struct scsi_device *sdev, struct alua_dh_data *h)
{
	struct request *rq;
	int err = SCSI_DH_RES_TEMP_UNAVAIL;

	rq = get_alua_req(sdev, h->inq, ALUA_INQUIRY_SIZE, READ);
	if (!rq)
		goto done;

	/* Prepare the command. */
	rq->cmd[0] = INQUIRY;
	rq->cmd[1] = 0;
	rq->cmd[2] = 0;
	rq->cmd[4] = ALUA_INQUIRY_SIZE;
	rq->cmd_len = COMMAND_SIZE(INQUIRY);

	rq->sense = h->sense;
	memset(rq->sense, 0, SCSI_SENSE_BUFFERSIZE);
	rq->sense_len = h->senselen = 0;

	err = blk_execute_rq(rq->q, NULL, rq, 1);
	if (err == -EIO) {
		sdev_printk(KERN_INFO, sdev,
			    "%s: std inquiry failed with %x\n",
			    ALUA_DH_NAME, rq->errors);
		h->senselen = rq->sense_len;
		err = SCSI_DH_IO;
	}
	blk_put_request(rq);
done:
	return err;
}

/*
 * submit_vpd_inquiry - Issue an INQUIRY VPD page 0x83 command
 * @sdev: sdev the command should be sent to
 */
static int submit_vpd_inquiry(struct scsi_device *sdev, struct alua_dh_data *h)
{
	struct request *rq;
	int err = SCSI_DH_RES_TEMP_UNAVAIL;

	rq = get_alua_req(sdev, h->buff, h->bufflen, READ);
	if (!rq)
		goto done;

	/* Prepare the command. */
	rq->cmd[0] = INQUIRY;
	rq->cmd[1] = 1;
	rq->cmd[2] = 0x83;
	rq->cmd[4] = h->bufflen;
	rq->cmd_len = COMMAND_SIZE(INQUIRY);

	rq->sense = h->sense;
	memset(rq->sense, 0, SCSI_SENSE_BUFFERSIZE);
	rq->sense_len = h->senselen = 0;

	err = blk_execute_rq(rq->q, NULL, rq, 1);
	if (err == -EIO) {
		sdev_printk(KERN_INFO, sdev,
			    "%s: evpd inquiry failed with %x\n",
			    ALUA_DH_NAME, rq->errors);
		h->senselen = rq->sense_len;
		err = SCSI_DH_IO;
	}
	blk_put_request(rq);
done:
	return err;
}

/*
 * submit_rtpg - Issue a REPORT TARGET GROUP STATES command
 * @sdev: sdev the command should be sent to
 */
static unsigned submit_rtpg(struct scsi_device *sdev, struct alua_dh_data *h)
{
	struct request *rq;
	int err = SCSI_DH_RES_TEMP_UNAVAIL;

	rq = get_alua_req(sdev, h->buff, h->bufflen, READ);
	if (!rq)
		goto done;

	/* Prepare the command. */
	rq->cmd[0] = MAINTENANCE_IN;
	rq->cmd[1] = MI_REPORT_TARGET_PGS;
	rq->cmd[6] = (h->bufflen >> 24) & 0xff;
	rq->cmd[7] = (h->bufflen >> 16) & 0xff;
	rq->cmd[8] = (h->bufflen >>  8) & 0xff;
	rq->cmd[9] = h->bufflen & 0xff;
	rq->cmd_len = COMMAND_SIZE(MAINTENANCE_IN);

	rq->sense = h->sense;
	memset(rq->sense, 0, SCSI_SENSE_BUFFERSIZE);
	rq->sense_len = h->senselen = 0;

	err = blk_execute_rq(rq->q, NULL, rq, 1);
	if (err == -EIO) {
		sdev_printk(KERN_INFO, sdev,
			    "%s: rtpg failed with %x\n",
			    ALUA_DH_NAME, rq->errors);
		h->senselen = rq->sense_len;
		err = SCSI_DH_IO;
	}
	blk_put_request(rq);
done:
	return err;
}

/*
 * submit_stpg - Issue a SET TARGET GROUP STATES command
 * @sdev: sdev the command should be sent to
 *
 * Currently we're only setting the current target port group state
 * to 'active/optimized' and let the array firmware figure out
 * the states of the remaining groups.
 */
static unsigned submit_stpg(struct scsi_device *sdev, struct alua_dh_data *h)
{
	struct request *rq;
	int err = SCSI_DH_RES_TEMP_UNAVAIL;
	int stpg_len = 8;

	/* Prepare the data buffer */
	memset(h->buff, 0, stpg_len);
	h->buff[4] = TPGS_STATE_OPTIMIZED & 0x0f;
	h->buff[6] = (h->group_id >> 8) & 0xff;
	h->buff[7] = h->group_id & 0xff;

	rq = get_alua_req(sdev, h->buff, stpg_len, WRITE);
	if (!rq)
		goto done;

	/* Prepare the command. */
	rq->cmd[0] = MAINTENANCE_OUT;
	rq->cmd[1] = MO_SET_TARGET_PGS;
	rq->cmd[6] = (stpg_len >> 24) & 0xff;
	rq->cmd[7] = (stpg_len >> 16) & 0xff;
	rq->cmd[8] = (stpg_len >>  8) & 0xff;
	rq->cmd[9] = stpg_len & 0xff;
	rq->cmd_len = COMMAND_SIZE(MAINTENANCE_OUT);

	rq->sense = h->sense;
	memset(rq->sense, 0, SCSI_SENSE_BUFFERSIZE);
	rq->sense_len = h->senselen = 0;

	err = blk_execute_rq(rq->q, NULL, rq, 1);
	if (err == -EIO) {
		sdev_printk(KERN_INFO, sdev,
			    "%s: stpg failed with %x\n",
			    ALUA_DH_NAME, rq->errors);
		h->senselen = rq->sense_len;
		err = SCSI_DH_IO;
	}
	blk_put_request(rq);
done:
	return err;
}

/*
 * alua_std_inquiry - Evaluate standard INQUIRY command
 * @sdev: device to be checked
 *
 * Just extract the TPGS setting to find out if ALUA
 * is supported.
 */
static int alua_std_inquiry(struct scsi_device *sdev, struct alua_dh_data *h)
{
	int err;

	err = submit_std_inquiry(sdev, h);

	if (err != SCSI_DH_OK)
		return err;

	/* Check TPGS setting */
	h->tpgs = (h->inq[5] >> 4) & 0x3;
	switch (h->tpgs) {
	case TPGS_MODE_EXPLICIT|TPGS_MODE_IMPLICIT:
		sdev_printk(KERN_INFO, sdev,
			    "%s: supports implicit and explicit TPGS\n",
			    ALUA_DH_NAME);
		break;
	case TPGS_MODE_EXPLICIT:
		sdev_printk(KERN_INFO, sdev, "%s: supports explicit TPGS\n",
			    ALUA_DH_NAME);
		break;
	case TPGS_MODE_IMPLICIT:
		sdev_printk(KERN_INFO, sdev, "%s: supports implicit TPGS\n",
			    ALUA_DH_NAME);
		break;
	default:
		h->tpgs = TPGS_MODE_NONE;
		sdev_printk(KERN_INFO, sdev, "%s: not supported\n",
			    ALUA_DH_NAME);
		err = SCSI_DH_DEV_UNSUPP;
		break;
	}

	return err;
}

/*
 * alua_vpd_inquiry - Evaluate INQUIRY vpd page 0x83
 * @sdev: device to be checked
 *
 * Extract the relative target port and the target port group
 * descriptor from the list of identificators.
 */
static int alua_vpd_inquiry(struct scsi_device *sdev, struct alua_dh_data *h)
{
	int len;
	unsigned err;
	unsigned char *d;

 retry:
	err = submit_vpd_inquiry(sdev, h);

	if (err != SCSI_DH_OK)
		return err;

	/* Check if vpd page exceeds initial buffer */
	len = (h->buff[2] << 8) + h->buff[3] + 4;
	if (len > h->bufflen) {
		/* Resubmit with the correct length */
		if (realloc_buffer(h, len)) {
			sdev_printk(KERN_WARNING, sdev,
				    "%s: kmalloc buffer failed\n",
				    ALUA_DH_NAME);
			/* Temporary failure, bypass */
			return SCSI_DH_DEV_TEMP_BUSY;
		}
		goto retry;
	}

	/*
	 * Now look for the correct descriptor.
	 */
	d = h->buff + 4;
	while (d < h->buff + len) {
		switch (d[1] & 0xf) {
		case 0x4:
			/* Relative target port */
			h->rel_port = (d[6] << 8) + d[7];
			break;
		case 0x5:
			/* Target port group */
			h->group_id = (d[6] << 8) + d[7];
			break;
		default:
			break;
		}
		d += d[3] + 4;
	}

	if (h->group_id == -1) {
		/*
		 * Internal error; TPGS supported but required
		 * VPD identification descriptors not present.
		 * Disable ALUA support
		 */
		sdev_printk(KERN_INFO, sdev,
			    "%s: No target port descriptors found\n",
			    ALUA_DH_NAME);
		h->state = TPGS_STATE_OPTIMIZED;
		h->tpgs = TPGS_MODE_NONE;
		err = SCSI_DH_DEV_UNSUPP;
	} else {
		sdev_printk(KERN_INFO, sdev,
			    "%s: port group %02x rel port %02x\n",
			    ALUA_DH_NAME, h->group_id, h->rel_port);
	}

	return err;
}

static char print_alua_state(int state)
{
	switch (state) {
	case TPGS_STATE_OPTIMIZED:
		return 'A';
	case TPGS_STATE_NONOPTIMIZED:
		return 'N';
	case TPGS_STATE_STANDBY:
		return 'S';
	case TPGS_STATE_UNAVAILABLE:
		return 'U';
	case TPGS_STATE_OFFLINE:
		return 'O';
	case TPGS_STATE_TRANSITIONING:
		return 'T';
	default:
		return 'X';
	}
}

static int alua_check_sense(struct scsi_device *sdev,
			    struct scsi_sense_hdr *sense_hdr)
{
	switch (sense_hdr->sense_key) {
	case NOT_READY:
		if (sense_hdr->asc == 0x04 && sense_hdr->ascq == 0x0a)
			/*
			 * LUN Not Accessible - ALUA state transition
			 */
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0x04 && sense_hdr->ascq == 0x0b)
			/*
			 * LUN Not Accessible -- Target port in standby state
			 */
			return SUCCESS;
		if (sense_hdr->asc == 0x04 && sense_hdr->ascq == 0x0c)
			/*
			 * LUN Not Accessible -- Target port in unavailable state
			 */
			return SUCCESS;
		if (sense_hdr->asc == 0x04 && sense_hdr->ascq == 0x12)
			/*
			 * LUN Not Ready -- Offline
			 */
			return SUCCESS;
		break;
	case UNIT_ATTENTION:
		if (sense_hdr->asc == 0x29 && sense_hdr->ascq == 0x00)
			/*
			 * Power On, Reset, or Bus Device Reset, just retry.
			 */
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0x2a && sense_hdr->ascq == 0x06) {
			/*
			 * ALUA state changed
			 */
			return ADD_TO_MLQUEUE;
		}
		if (sense_hdr->asc == 0x2a && sense_hdr->ascq == 0x07) {
			/*
			 * Implicit ALUA state transition failed
			 */
			return ADD_TO_MLQUEUE;
		}
		if (sense_hdr->asc == 0x3f && sense_hdr->ascq == 0x0e) {
			/*
			 * REPORTED_LUNS_DATA_HAS_CHANGED is reported
			 * when switching controllers on targets like
			 * Intel Multi-Flex. We can just retry.
			 */
			return ADD_TO_MLQUEUE;
		}

		break;
	}

	return SCSI_RETURN_NOT_HANDLED;
}

/*
 * alua_stpg - Evaluate SET TARGET GROUP STATES
 * @sdev: the device to be evaluated
 * @state: the new target group state
 *
 * Send a SET TARGET GROUP STATES command to the device.
 * We only have to test here if we should resubmit the command;
 * any other error is assumed as a failure.
 */
static int alua_stpg(struct scsi_device *sdev, int state,
		     struct alua_dh_data *h)
{
	struct scsi_sense_hdr sense_hdr;
	unsigned err;
	int retry = ALUA_FAILOVER_RETRIES;

 retry:
	err = submit_stpg(sdev, h);
	if (err == SCSI_DH_IO && h->senselen > 0) {
		err = scsi_normalize_sense(h->sense, SCSI_SENSE_BUFFERSIZE,
					   &sense_hdr);
		if (!err)
			return SCSI_DH_IO;
		err = alua_check_sense(sdev, &sense_hdr);
		if (retry > 0 && err == ADD_TO_MLQUEUE) {
			retry--;
			goto retry;
		}
		sdev_printk(KERN_INFO, sdev,
			    "%s: stpg sense code: %02x/%02x/%02x\n",
			    ALUA_DH_NAME, sense_hdr.sense_key,
			    sense_hdr.asc, sense_hdr.ascq);
		err = SCSI_DH_IO;
	}
	if (err == SCSI_DH_OK) {
		h->state = state;
		sdev_printk(KERN_INFO, sdev,
			    "%s: port group %02x switched to state %c\n",
			    ALUA_DH_NAME, h->group_id,
			    print_alua_state(h->state) );
	}
	return err;
}

/*
 * alua_rtpg - Evaluate REPORT TARGET GROUP STATES
 * @sdev: the device to be evaluated.
 *
 * Evaluate the Target Port Group State.
 * Returns SCSI_DH_DEV_OFFLINED if the path is
 * found to be unuseable.
 */
static int alua_rtpg(struct scsi_device *sdev, struct alua_dh_data *h)
{
	struct scsi_sense_hdr sense_hdr;
	int len, k, off, valid_states = 0;
	char *ucp;
	unsigned err;

 retry:
	err = submit_rtpg(sdev, h);

	if (err == SCSI_DH_IO && h->senselen > 0) {
		err = scsi_normalize_sense(h->sense, SCSI_SENSE_BUFFERSIZE,
					   &sense_hdr);
		if (!err)
			return SCSI_DH_IO;

		err = alua_check_sense(sdev, &sense_hdr);
		if (err == ADD_TO_MLQUEUE)
			goto retry;
		sdev_printk(KERN_INFO, sdev,
			    "%s: rtpg sense code %02x/%02x/%02x\n",
			    ALUA_DH_NAME, sense_hdr.sense_key,
			    sense_hdr.asc, sense_hdr.ascq);
		err = SCSI_DH_IO;
	}
	if (err != SCSI_DH_OK)
		return err;

	len = (h->buff[0] << 24) + (h->buff[1] << 16) +
		(h->buff[2] << 8) + h->buff[3] + 4;

	if (len > h->bufflen) {
		/* Resubmit with the correct length */
		if (realloc_buffer(h, len)) {
			sdev_printk(KERN_WARNING, sdev,
				    "%s: kmalloc buffer failed\n",__func__);
			/* Temporary failure, bypass */
			return SCSI_DH_DEV_TEMP_BUSY;
		}
		goto retry;
	}

	for (k = 4, ucp = h->buff + 4; k < len; k += off, ucp += off) {
		if (h->group_id == (ucp[2] << 8) + ucp[3]) {
			h->state = ucp[0] & 0x0f;
			valid_states = ucp[1];
		}
		off = 8 + (ucp[7] * 4);
	}

	sdev_printk(KERN_INFO, sdev,
		    "%s: port group %02x state %c supports %c%c%c%c%c%c\n",
		    ALUA_DH_NAME, h->group_id, print_alua_state(h->state),
		    valid_states&TPGS_SUPPORT_TRANSITION?'T':'t',
		    valid_states&TPGS_SUPPORT_OFFLINE?'O':'o',
		    valid_states&TPGS_SUPPORT_UNAVAILABLE?'U':'u',
		    valid_states&TPGS_SUPPORT_STANDBY?'S':'s',
		    valid_states&TPGS_SUPPORT_NONOPTIMIZED?'N':'n',
		    valid_states&TPGS_SUPPORT_OPTIMIZED?'A':'a');

	if (h->tpgs & TPGS_MODE_EXPLICIT) {
		switch (h->state) {
		case TPGS_STATE_TRANSITIONING:
			/* State transition, retry */
			goto retry;
			break;
		case TPGS_STATE_OFFLINE:
			/* Path is offline, fail */
			err = SCSI_DH_DEV_OFFLINED;
			break;
		default:
			break;
		}
	} else {
		/* Only Implicit ALUA support */
		if (h->state == TPGS_STATE_OPTIMIZED ||
		    h->state == TPGS_STATE_NONOPTIMIZED ||
		    h->state == TPGS_STATE_STANDBY)
			/* Useable path if active */
			err = SCSI_DH_OK;
		else
			/* Path unuseable for unavailable/offline */
			err = SCSI_DH_DEV_OFFLINED;
	}
	return err;
}

/*
 * alua_initialize - Initialize ALUA state
 * @sdev: the device to be initialized
 *
 * For the prep_fn to work correctly we have
 * to initialize the ALUA state for the device.
 */
static int alua_initialize(struct scsi_device *sdev, struct alua_dh_data *h)
{
	int err;

	err = alua_std_inquiry(sdev, h);
	if (err != SCSI_DH_OK)
		goto out;

	err = alua_vpd_inquiry(sdev, h);
	if (err != SCSI_DH_OK)
		goto out;

	err = alua_rtpg(sdev, h);
	if (err != SCSI_DH_OK)
		goto out;

out:
	return err;
}

/*
 * alua_activate - activate a path
 * @sdev: device on the path to be activated
 *
 * We're currently switching the port group to be activated only and
 * let the array figure out the rest.
 * There may be other arrays which require us to switch all port groups
 * based on a certain policy. But until we actually encounter them it
 * should be okay.
 */
static int alua_activate(struct scsi_device *sdev)
{
	struct alua_dh_data *h = get_alua_data(sdev);
	int err = SCSI_DH_OK;

	if (h->group_id != -1) {
		err = alua_rtpg(sdev, h);
		if (err != SCSI_DH_OK)
			goto out;
	}

	if (h->tpgs & TPGS_MODE_EXPLICIT && h->state != TPGS_STATE_OPTIMIZED)
		err = alua_stpg(sdev, TPGS_STATE_OPTIMIZED, h);

out:
	return err;
}

/*
 * alua_prep_fn - request callback
 *
 * Fail I/O to all paths not in state
 * active/optimized or active/non-optimized.
 */
static int alua_prep_fn(struct scsi_device *sdev, struct request *req)
{
	struct alua_dh_data *h = get_alua_data(sdev);
	int ret = BLKPREP_OK;

	if (h->state != TPGS_STATE_OPTIMIZED &&
	    h->state != TPGS_STATE_NONOPTIMIZED) {
		ret = BLKPREP_KILL;
		req->cmd_flags |= REQ_QUIET;
	}
	return ret;

}

static const struct scsi_dh_devlist alua_dev_list[] = {
	{"HP", "MSA VOLUME" },
	{"HP", "HSV101" },
	{"HP", "HSV111" },
	{"HP", "HSV200" },
	{"HP", "HSV210" },
	{"HP", "HSV300" },
	{"IBM", "2107900" },
	{"IBM", "2145" },
	{"Pillar", "Axiom" },
	{"Intel", "Multi-Flex"},
	{"NETAPP", "LUN"},
	{"AIX", "NVDISK"},
	{NULL, NULL}
};

static int alua_bus_attach(struct scsi_device *sdev);
static void alua_bus_detach(struct scsi_device *sdev);

static struct scsi_device_handler alua_dh = {
	.name = ALUA_DH_NAME,
	.module = THIS_MODULE,
	.devlist = alua_dev_list,
	.attach = alua_bus_attach,
	.detach = alua_bus_detach,
	.prep_fn = alua_prep_fn,
	.check_sense = alua_check_sense,
	.activate = alua_activate,
};

/*
 * alua_bus_attach - Attach device handler
 * @sdev: device to be attached to
 */
static int alua_bus_attach(struct scsi_device *sdev)
{
	struct scsi_dh_data *scsi_dh_data;
	struct alua_dh_data *h;
	unsigned long flags;
	int err = SCSI_DH_OK;

	scsi_dh_data = kzalloc(sizeof(struct scsi_device_handler *)
			       + sizeof(*h) , GFP_KERNEL);
	if (!scsi_dh_data) {
		sdev_printk(KERN_ERR, sdev, "%s: Attach failed\n",
			    ALUA_DH_NAME);
		return -ENOMEM;
	}

	scsi_dh_data->scsi_dh = &alua_dh;
	h = (struct alua_dh_data *) scsi_dh_data->buf;
	h->tpgs = TPGS_MODE_UNINITIALIZED;
	h->state = TPGS_STATE_OPTIMIZED;
	h->group_id = -1;
	h->rel_port = -1;
	h->buff = h->inq;
	h->bufflen = ALUA_INQUIRY_SIZE;

	err = alua_initialize(sdev, h);
	if (err != SCSI_DH_OK)
		goto failed;

	if (!try_module_get(THIS_MODULE))
		goto failed;

	spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
	sdev->scsi_dh_data = scsi_dh_data;
	spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);

	return 0;

failed:
	kfree(scsi_dh_data);
	sdev_printk(KERN_ERR, sdev, "%s: not attached\n", ALUA_DH_NAME);
	return -EINVAL;
}

/*
 * alua_bus_detach - Detach device handler
 * @sdev: device to be detached from
 */
static void alua_bus_detach(struct scsi_device *sdev)
{
	struct scsi_dh_data *scsi_dh_data;
	struct alua_dh_data *h;
	unsigned long flags;

	spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
	scsi_dh_data = sdev->scsi_dh_data;
	sdev->scsi_dh_data = NULL;
	spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);

	h = (struct alua_dh_data *) scsi_dh_data->buf;
	if (h->buff && h->inq != h->buff)
		kfree(h->buff);
	kfree(scsi_dh_data);
	module_put(THIS_MODULE);
	sdev_printk(KERN_NOTICE, sdev, "%s: Detached\n", ALUA_DH_NAME);
}

static int __init alua_init(void)
{
	int r;

	r = scsi_register_device_handler(&alua_dh);
	if (r != 0)
		printk(KERN_ERR "%s: Failed to register scsi device handler",
			ALUA_DH_NAME);
	return r;
}

static void __exit alua_exit(void)
{
	scsi_unregister_device_handler(&alua_dh);
}

module_init(alua_init);
module_exit(alua_exit);

MODULE_DESCRIPTION("DM Multipath ALUA support");
MODULE_AUTHOR("Hannes Reinecke <hare@suse.de>");
MODULE_LICENSE("GPL");
MODULE_VERSION(ALUA_DH_VER);
