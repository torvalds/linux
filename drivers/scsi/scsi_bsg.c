// SPDX-License-Identifier: GPL-2.0
#include <linux/bsg.h>
#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/sg.h>
#include "scsi_priv.h"

#define uptr64(val) ((void __user *)(uintptr_t)(val))

static int scsi_bsg_sg_io_fn(struct request_queue *q, struct sg_io_v4 *hdr,
		bool open_for_write, unsigned int timeout)
{
	struct scsi_cmnd *scmd;
	struct request *rq;
	struct bio *bio;
	int ret;

	if (hdr->protocol != BSG_PROTOCOL_SCSI  ||
	    hdr->subprotocol != BSG_SUB_PROTOCOL_SCSI_CMD)
		return -EINVAL;
	if (hdr->dout_xfer_len && hdr->din_xfer_len) {
		pr_warn_once("BIDI support in bsg has been removed.\n");
		return -EOPNOTSUPP;
	}

	rq = scsi_alloc_request(q, hdr->dout_xfer_len ?
				REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(rq))
		return PTR_ERR(rq);
	rq->timeout = timeout;

	scmd = blk_mq_rq_to_pdu(rq);
	scmd->cmd_len = hdr->request_len;
	if (scmd->cmd_len > sizeof(scmd->cmnd)) {
		ret = -EINVAL;
		goto out_put_request;
	}

	ret = -EFAULT;
	if (copy_from_user(scmd->cmnd, uptr64(hdr->request), scmd->cmd_len))
		goto out_put_request;
	ret = -EPERM;
	if (!scsi_cmd_allowed(scmd->cmnd, open_for_write))
		goto out_put_request;

	ret = 0;
	if (hdr->dout_xfer_len) {
		ret = blk_rq_map_user(rq->q, rq, NULL, uptr64(hdr->dout_xferp),
				hdr->dout_xfer_len, GFP_KERNEL);
	} else if (hdr->din_xfer_len) {
		ret = blk_rq_map_user(rq->q, rq, NULL, uptr64(hdr->din_xferp),
				hdr->din_xfer_len, GFP_KERNEL);
	}

	if (ret)
		goto out_put_request;

	bio = rq->bio;
	blk_execute_rq(rq, !(hdr->flags & BSG_FLAG_Q_AT_TAIL));

	/*
	 * fill in all the output members
	 */
	hdr->device_status = scmd->result & 0xff;
	hdr->transport_status = host_byte(scmd->result);
	hdr->driver_status = 0;
	if (scsi_status_is_check_condition(scmd->result))
		hdr->driver_status = DRIVER_SENSE;
	hdr->info = 0;
	if (hdr->device_status || hdr->transport_status || hdr->driver_status)
		hdr->info |= SG_INFO_CHECK;
	hdr->response_len = 0;

	if (scmd->sense_len && hdr->response) {
		int len = min_t(unsigned int, hdr->max_response_len,
				scmd->sense_len);

		if (copy_to_user(uptr64(hdr->response), scmd->sense_buffer,
				 len))
			ret = -EFAULT;
		else
			hdr->response_len = len;
	}

	if (rq_data_dir(rq) == READ)
		hdr->din_resid = scmd->resid_len;
	else
		hdr->dout_resid = scmd->resid_len;

	blk_rq_unmap_user(bio);

out_put_request:
	blk_mq_free_request(rq);
	return ret;
}

struct bsg_device *scsi_bsg_register_queue(struct scsi_device *sdev)
{
	return bsg_register_queue(sdev->request_queue, &sdev->sdev_gendev,
			dev_name(&sdev->sdev_gendev), scsi_bsg_sg_io_fn);
}
