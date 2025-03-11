// SPDX-License-Identifier: GPL-2.0
/*
 * bsg endpoint that supports UPIUs
 *
 * Copyright (C) 2018 Western Digital Corporation
 */

#include <linux/bsg-lib.h>
#include <linux/dma-mapping.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include "ufs_bsg.h"
#include <ufs/ufshcd.h>
#include "ufshcd-priv.h"

static int ufs_bsg_get_query_desc_size(struct ufs_hba *hba, int *desc_len,
				       struct utp_upiu_query *qr)
{
	int desc_size = be16_to_cpu(qr->length);

	if (desc_size <= 0)
		return -EINVAL;

	*desc_len = min_t(int, QUERY_DESC_MAX_SIZE, desc_size);

	return 0;
}

static int ufs_bsg_alloc_desc_buffer(struct ufs_hba *hba, struct bsg_job *job,
				     uint8_t **desc_buff, int *desc_len,
				     enum query_opcode desc_op)
{
	struct ufs_bsg_request *bsg_request = job->request;
	struct utp_upiu_query *qr;
	u8 *descp;

	if (desc_op != UPIU_QUERY_OPCODE_WRITE_DESC &&
	    desc_op != UPIU_QUERY_OPCODE_READ_DESC)
		goto out;

	qr = &bsg_request->upiu_req.qr;
	if (ufs_bsg_get_query_desc_size(hba, desc_len, qr)) {
		dev_err(hba->dev, "Illegal desc size\n");
		return -EINVAL;
	}

	if (*desc_len > job->request_payload.payload_len) {
		dev_err(hba->dev, "Illegal desc size\n");
		return -EINVAL;
	}

	descp = kzalloc(*desc_len, GFP_KERNEL);
	if (!descp)
		return -ENOMEM;

	if (desc_op == UPIU_QUERY_OPCODE_WRITE_DESC)
		sg_copy_to_buffer(job->request_payload.sg_list,
				  job->request_payload.sg_cnt, descp,
				  *desc_len);

	*desc_buff = descp;

out:
	return 0;
}

static int ufs_bsg_exec_advanced_rpmb_req(struct ufs_hba *hba, struct bsg_job *job)
{
	struct ufs_rpmb_request *rpmb_request = job->request;
	struct ufs_rpmb_reply *rpmb_reply = job->reply;
	struct bsg_buffer *payload = NULL;
	enum dma_data_direction dir;
	struct scatterlist *sg_list = NULL;
	int rpmb_req_type;
	int sg_cnt = 0;
	int ret;
	int data_len;

	if (hba->ufs_version < ufshci_version(4, 0) || !hba->dev_info.b_advanced_rpmb_en)
		return -EINVAL;

	if (rpmb_request->ehs_req.length != 2 || rpmb_request->ehs_req.ehs_type != 1)
		return -EINVAL;

	rpmb_req_type = be16_to_cpu(rpmb_request->ehs_req.meta.req_resp_type);

	switch (rpmb_req_type) {
	case UFS_RPMB_WRITE_KEY:
	case UFS_RPMB_READ_CNT:
	case UFS_RPMB_PURGE_ENABLE:
		dir = DMA_NONE;
		break;
	case UFS_RPMB_WRITE:
	case UFS_RPMB_SEC_CONF_WRITE:
		dir = DMA_TO_DEVICE;
		break;
	case UFS_RPMB_READ:
	case UFS_RPMB_SEC_CONF_READ:
	case UFS_RPMB_PURGE_STATUS_READ:
		dir = DMA_FROM_DEVICE;
		break;
	default:
		return -EINVAL;
	}

	if (dir != DMA_NONE) {
		payload = &job->request_payload;
		if (!payload || !payload->payload_len || !payload->sg_cnt)
			return -EINVAL;

		sg_cnt = dma_map_sg(hba->host->dma_dev, payload->sg_list, payload->sg_cnt, dir);
		if (unlikely(!sg_cnt))
			return -ENOMEM;
		sg_list = payload->sg_list;
		data_len = payload->payload_len;
	}

	ret = ufshcd_advanced_rpmb_req_handler(hba, &rpmb_request->bsg_request.upiu_req,
				   &rpmb_reply->bsg_reply.upiu_rsp, &rpmb_request->ehs_req,
				   &rpmb_reply->ehs_rsp, sg_cnt, sg_list, dir);

	if (dir != DMA_NONE) {
		dma_unmap_sg(hba->host->dma_dev, payload->sg_list, payload->sg_cnt, dir);

		if (!ret)
			rpmb_reply->bsg_reply.reply_payload_rcv_len = data_len;
	}

	return ret;
}

static int ufs_bsg_request(struct bsg_job *job)
{
	struct ufs_bsg_request *bsg_request = job->request;
	struct ufs_bsg_reply *bsg_reply = job->reply;
	struct ufs_hba *hba = shost_priv(dev_to_shost(job->dev->parent));
	struct uic_command uc = {};
	int msgcode;
	uint8_t *buff = NULL;
	int desc_len = 0;
	enum query_opcode desc_op = UPIU_QUERY_OPCODE_NOP;
	int ret;
	bool rpmb = false;

	bsg_reply->reply_payload_rcv_len = 0;

	ufshcd_rpm_get_sync(hba);

	msgcode = bsg_request->msgcode;
	switch (msgcode) {
	case UPIU_TRANSACTION_QUERY_REQ:
		desc_op = bsg_request->upiu_req.qr.opcode;
		ret = ufs_bsg_alloc_desc_buffer(hba, job, &buff, &desc_len, desc_op);
		if (ret)
			goto out;
		fallthrough;
	case UPIU_TRANSACTION_NOP_OUT:
	case UPIU_TRANSACTION_TASK_REQ:
		ret = ufshcd_exec_raw_upiu_cmd(hba, &bsg_request->upiu_req,
					       &bsg_reply->upiu_rsp, msgcode,
					       buff, &desc_len, desc_op);
		if (ret)
			dev_err(hba->dev, "exe raw upiu: error code %d\n", ret);
		else if (desc_op == UPIU_QUERY_OPCODE_READ_DESC && desc_len) {
			bsg_reply->reply_payload_rcv_len =
				sg_copy_from_buffer(job->request_payload.sg_list,
						    job->request_payload.sg_cnt,
						    buff, desc_len);
		}
		break;
	case UPIU_TRANSACTION_UIC_CMD:
		memcpy(&uc, &bsg_request->upiu_req.uc, UIC_CMD_SIZE);
		ret = ufshcd_send_bsg_uic_cmd(hba, &uc);
		if (ret)
			dev_err(hba->dev, "send uic cmd: error code %d\n", ret);

		memcpy(&bsg_reply->upiu_rsp.uc, &uc, UIC_CMD_SIZE);

		break;
	case UPIU_TRANSACTION_ARPMB_CMD:
		rpmb = true;
		ret = ufs_bsg_exec_advanced_rpmb_req(hba, job);
		if (ret)
			dev_err(hba->dev, "ARPMB OP failed: error code  %d\n", ret);
		break;
	default:
		ret = -ENOTSUPP;
		dev_err(hba->dev, "unsupported msgcode 0x%x\n", msgcode);

		break;
	}

out:
	ufshcd_rpm_put_sync(hba);
	kfree(buff);
	bsg_reply->result = ret;
	job->reply_len = !rpmb ? sizeof(struct ufs_bsg_reply) : sizeof(struct ufs_rpmb_reply);
	/* complete the job here only if no error */
	if (ret == 0)
		bsg_job_done(job, ret, bsg_reply->reply_payload_rcv_len);

	return ret;
}

/**
 * ufs_bsg_remove - detach and remove the added ufs-bsg node
 * @hba: per adapter object
 *
 * Should be called when unloading the driver.
 */
void ufs_bsg_remove(struct ufs_hba *hba)
{
	struct device *bsg_dev = &hba->bsg_dev;

	if (!hba->bsg_queue)
		return;

	bsg_remove_queue(hba->bsg_queue);

	device_del(bsg_dev);
	put_device(bsg_dev);
}

static inline void ufs_bsg_node_release(struct device *dev)
{
	put_device(dev->parent);
}

/**
 * ufs_bsg_probe - Add ufs bsg device node
 * @hba: per adapter object
 *
 * Called during initial loading of the driver, and before scsi_scan_host.
 *
 * Returns: 0 (success).
 */
int ufs_bsg_probe(struct ufs_hba *hba)
{
	struct device *bsg_dev = &hba->bsg_dev;
	struct Scsi_Host *shost = hba->host;
	struct device *parent = &shost->shost_gendev;
	struct request_queue *q;
	int ret;

	device_initialize(bsg_dev);

	bsg_dev->parent = get_device(parent);
	bsg_dev->release = ufs_bsg_node_release;

	dev_set_name(bsg_dev, "ufs-bsg%u", shost->host_no);

	ret = device_add(bsg_dev);
	if (ret)
		goto out;

	q = bsg_setup_queue(bsg_dev, dev_name(bsg_dev), NULL, ufs_bsg_request,
			NULL, 0);
	if (IS_ERR(q)) {
		ret = PTR_ERR(q);
		goto out;
	}

	hba->bsg_queue = q;

	return 0;

out:
	dev_err(bsg_dev, "fail to initialize a bsg dev %d\n", shost->host_no);
	put_device(bsg_dev);
	return ret;
}
