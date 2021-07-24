// SPDX-License-Identifier: GPL-2.0
#include <linux/bsg.h>
#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/sg.h>
#include "scsi_priv.h"

#define uptr64(val) ((void __user *)(uintptr_t)(val))

static int scsi_bsg_check_proto(struct sg_io_v4 *hdr)
{
	if (hdr->protocol != BSG_PROTOCOL_SCSI  ||
	    hdr->subprotocol != BSG_SUB_PROTOCOL_SCSI_CMD)
		return -EINVAL;
	return 0;
}

static int scsi_bsg_fill_hdr(struct request *rq, struct sg_io_v4 *hdr,
		fmode_t mode)
{
	struct scsi_request *sreq = scsi_req(rq);

	if (hdr->dout_xfer_len && hdr->din_xfer_len) {
		pr_warn_once("BIDI support in bsg has been removed.\n");
		return -EOPNOTSUPP;
	}

	sreq->cmd_len = hdr->request_len;
	if (sreq->cmd_len > BLK_MAX_CDB) {
		sreq->cmd = kzalloc(sreq->cmd_len, GFP_KERNEL);
		if (!sreq->cmd)
			return -ENOMEM;
	}

	if (copy_from_user(sreq->cmd, uptr64(hdr->request), sreq->cmd_len))
		return -EFAULT;
	if (!scsi_cmd_allowed(sreq->cmd, mode))
		return -EPERM;
	return 0;
}

static int scsi_bsg_complete_rq(struct request *rq, struct sg_io_v4 *hdr)
{
	struct scsi_request *sreq = scsi_req(rq);
	int ret = 0;

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

	return ret;
}

static void scsi_bsg_free_rq(struct request *rq)
{
	scsi_req_free_cmd(scsi_req(rq));
}

static const struct bsg_ops scsi_bsg_ops = {
	.check_proto		= scsi_bsg_check_proto,
	.fill_hdr		= scsi_bsg_fill_hdr,
	.complete_rq		= scsi_bsg_complete_rq,
	.free_rq		= scsi_bsg_free_rq,
};

int scsi_bsg_register_queue(struct request_queue *q, struct device *parent)
{
	return bsg_register_queue(q, parent, dev_name(parent), &scsi_bsg_ops);
}
