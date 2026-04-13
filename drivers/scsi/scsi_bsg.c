// SPDX-License-Identifier: GPL-2.0
#include <linux/bsg.h>
#include <linux/io_uring/cmd.h>
#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/sg.h>
#include "scsi_priv.h"

#define uptr64(val) ((void __user *)(uintptr_t)(val))

/*
 * Per-command BSG SCSI PDU stored in io_uring_cmd.pdu[32].
 * Holds temporary state between submission, completion and task_work.
 */
struct scsi_bsg_uring_cmd_pdu {
	struct bio *bio;		/* mapped user buffer, unmap in task work */
	struct request *req;		/* block request, freed in task work */
	u64 response_addr;		/* user space response buffer address */
};
static_assert(sizeof(struct scsi_bsg_uring_cmd_pdu) <= sizeof_field(struct io_uring_cmd, pdu));

static inline struct scsi_bsg_uring_cmd_pdu *scsi_bsg_uring_cmd_pdu(
	struct io_uring_cmd *ioucmd)
{
	return io_uring_cmd_to_pdu(ioucmd, struct scsi_bsg_uring_cmd_pdu);
}

/* Task work: build res2 (layout in uapi/linux/bsg.h) and copy sense to user. */
static void scsi_bsg_uring_task_cb(struct io_tw_req tw_req, io_tw_token_t tw)
{
	struct io_uring_cmd *ioucmd = io_uring_cmd_from_tw(tw_req);
	struct scsi_bsg_uring_cmd_pdu *pdu = scsi_bsg_uring_cmd_pdu(ioucmd);
	struct request *rq = pdu->req;
	struct scsi_cmnd *scmd = blk_mq_rq_to_pdu(rq);
	u64 res2;
	int ret = 0;
	u8 driver_status = 0;
	u8 sense_len_wr = 0;

	if (pdu->bio)
		blk_rq_unmap_user(pdu->bio);

	if (scsi_status_is_check_condition(scmd->result)) {
		driver_status = DRIVER_SENSE;
		if (pdu->response_addr)
			sense_len_wr = min_t(u8, scmd->sense_len,
					     SCSI_SENSE_BUFFERSIZE);
	}

	if (sense_len_wr) {
		if (copy_to_user(uptr64(pdu->response_addr), scmd->sense_buffer,
				 sense_len_wr))
			ret = -EFAULT;
	}

	res2 = bsg_scsi_res2_build(status_byte(scmd->result), driver_status,
				  host_byte(scmd->result), sense_len_wr,
				  scmd->resid_len);

	blk_mq_free_request(rq);
	io_uring_cmd_done32(ioucmd, ret, res2,
			    IO_URING_CMD_TASK_WORK_ISSUE_FLAGS);
}

static enum rq_end_io_ret scsi_bsg_uring_cmd_done(struct request *req,
						  blk_status_t status,
						  const struct io_comp_batch *iocb)
{
	struct io_uring_cmd *ioucmd = req->end_io_data;

	io_uring_cmd_do_in_task_lazy(ioucmd, scsi_bsg_uring_task_cb);
	return RQ_END_IO_NONE;
}

static int scsi_bsg_map_user_buffer(struct request *req,
				    struct io_uring_cmd *ioucmd,
				    unsigned int issue_flags, gfp_t gfp_mask)
{
	const struct bsg_uring_cmd *cmd = io_uring_sqe128_cmd(ioucmd->sqe, struct bsg_uring_cmd);
	bool is_write = cmd->dout_xfer_len > 0;
	u64 buf_addr = is_write ? cmd->dout_xferp : cmd->din_xferp;
	unsigned long buf_len = is_write ? cmd->dout_xfer_len : cmd->din_xfer_len;
	struct iov_iter iter;
	int ret;

	if (ioucmd->flags & IORING_URING_CMD_FIXED) {
		ret = io_uring_cmd_import_fixed(buf_addr, buf_len,
						is_write ? WRITE : READ,
						&iter, ioucmd, issue_flags);
		if (ret < 0)
			return ret;
		ret = blk_rq_map_user_iov(req->q, req, NULL, &iter, gfp_mask);
	} else {
		ret = blk_rq_map_user(req->q, req, NULL, uptr64(buf_addr),
				      buf_len, gfp_mask);
	}

	return ret;
}

static int scsi_bsg_uring_cmd(struct request_queue *q, struct io_uring_cmd *ioucmd,
			       unsigned int issue_flags, bool open_for_write)
{
	struct scsi_bsg_uring_cmd_pdu *pdu = scsi_bsg_uring_cmd_pdu(ioucmd);
	const struct bsg_uring_cmd *cmd = io_uring_sqe128_cmd(ioucmd->sqe, struct bsg_uring_cmd);
	struct scsi_cmnd *scmd;
	struct request *req;
	blk_mq_req_flags_t blk_flags = 0;
	gfp_t gfp_mask = GFP_KERNEL;
	int ret;

	if (cmd->protocol != BSG_PROTOCOL_SCSI ||
	    cmd->subprotocol != BSG_SUB_PROTOCOL_SCSI_CMD)
		return -EINVAL;

	if (!cmd->request || cmd->request_len == 0)
		return -EINVAL;

	if (cmd->dout_xfer_len && cmd->din_xfer_len) {
		pr_warn_once("BIDI support in bsg has been removed.\n");
		return -EOPNOTSUPP;
	}

	if (cmd->dout_iovec_count > 0 || cmd->din_iovec_count > 0)
		return -EOPNOTSUPP;

	if (issue_flags & IO_URING_F_NONBLOCK) {
		blk_flags = BLK_MQ_REQ_NOWAIT;
		gfp_mask = GFP_NOWAIT;
	}

	req = scsi_alloc_request(q, cmd->dout_xfer_len ?
				 REQ_OP_DRV_OUT : REQ_OP_DRV_IN, blk_flags);
	if (IS_ERR(req))
		return PTR_ERR(req);

	scmd = blk_mq_rq_to_pdu(req);
	if (cmd->request_len > sizeof(scmd->cmnd)) {
		ret = -EINVAL;
		goto out_free_req;
	}
	scmd->cmd_len = cmd->request_len;
	scmd->allowed = SG_DEFAULT_RETRIES;

	if (copy_from_user(scmd->cmnd, uptr64(cmd->request), cmd->request_len)) {
		ret = -EFAULT;
		goto out_free_req;
	}

	if (!scsi_cmd_allowed(scmd->cmnd, open_for_write)) {
		ret = -EPERM;
		goto out_free_req;
	}

	pdu->response_addr = cmd->response;
	scmd->sense_len = cmd->max_response_len ?
		min(cmd->max_response_len, SCSI_SENSE_BUFFERSIZE) : SCSI_SENSE_BUFFERSIZE;

	if (cmd->dout_xfer_len || cmd->din_xfer_len) {
		ret = scsi_bsg_map_user_buffer(req, ioucmd, issue_flags, gfp_mask);
		if (ret)
			goto out_free_req;
		pdu->bio = req->bio;
	} else {
		pdu->bio = NULL;
	}

	req->timeout = cmd->timeout_ms ?
		msecs_to_jiffies(cmd->timeout_ms) : BLK_DEFAULT_SG_TIMEOUT;

	req->end_io = scsi_bsg_uring_cmd_done;
	req->end_io_data = ioucmd;
	pdu->req = req;

	blk_execute_rq_nowait(req, false);
	return -EIOCBQUEUED;

out_free_req:
	blk_mq_free_request(req);
	return ret;
}

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
			dev_name(&sdev->sdev_gendev), scsi_bsg_sg_io_fn,
			scsi_bsg_uring_cmd);
}
