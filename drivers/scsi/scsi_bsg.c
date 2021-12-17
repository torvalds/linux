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
		fmode_t mode, unsigned int timeout)
{
	struct scsi_request *sreq;
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

	ret = -ENOMEM;
	sreq = scsi_req(rq);
	sreq->cmd_len = hdr->request_len;
	if (sreq->cmd_len > BLK_MAX_CDB) {
		sreq->cmd = kzalloc(sreq->cmd_len, GFP_KERNEL);
		if (!sreq->cmd)
			goto out_put_request;
	}

	ret = -EFAULT;
	if (copy_from_user(sreq->cmd, uptr64(hdr->request), sreq->cmd_len))
		goto out_free_cmd;
	ret = -EPERM;
	if (!scsi_cmd_allowed(sreq->cmd, mode))
		goto out_free_cmd;

	ret = 0;
	if (hdr->dout_xfer_len) {
		ret = blk_rq_map_user(rq->q, rq, NULL, uptr64(hdr->dout_xferp),
				hdr->dout_xfer_len, GFP_KERNEL);
	} else if (hdr->din_xfer_len) {
		ret = blk_rq_map_user(rq->q, rq, NULL, uptr64(hdr->din_xferp),
				hdr->din_xfer_len, GFP_KERNEL);
	}

	if (ret)
		goto out_free_cmd;

	bio = rq->bio;
	blk_execute_rq(NULL, rq, !(hdr->flags & BSG_FLAG_Q_AT_TAIL));

	/*
	 * fill in all the output members
	 */
	hdr->device_status = sreq->result & 0xff;
	hdr->transport_status = host_byte(sreq->result);
	hdr->driver_status = 0;
	if (scsi_status_is_check_condition(sreq->result))
		hdr->driver_status = DRIVER_SENSE;
	hdr->info = 0;
	if (hdr->device_status || hdr->transport_status || hdr->driver_status)
		hdr->info |= SG_INFO_CHECK;
	hdr->response_len = 0;

	if (sreq->sense_len && hdr->response) {
		int len = min_t(unsigned int, hdr->max_response_len,
					sreq->sense_len);

		if (copy_to_user(uptr64(hdr->response), sreq->sense, len))
			ret = -EFAULT;
		else
			hdr->response_len = len;
	}

	if (rq_data_dir(rq) == READ)
		hdr->din_resid = sreq->resid_len;
	else
		hdr->dout_resid = sreq->resid_len;

	blk_rq_unmap_user(bio);

out_free_cmd:
	scsi_req_free_cmd(scsi_req(rq));
out_put_request:
	blk_mq_free_request(rq);
	return ret;
}

struct bsg_device *scsi_bsg_register_queue(struct scsi_device *sdev)
{
	return bsg_register_queue(sdev->request_queue, &sdev->sdev_gendev,
			dev_name(&sdev->sdev_gendev), scsi_bsg_sg_io_fn);
}
