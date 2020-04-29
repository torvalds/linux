// SPDX-License-Identifier: GPL-2.0
/*
 * bsg endpoint that supports UPIUs
 *
 * Copyright (C) 2018 Western Digital Corporation
 */
#include "ufs_bsg.h"

static int ufs_bsg_get_query_desc_size(struct ufs_hba *hba, int *desc_len,
				       struct utp_upiu_query *qr)
{
	int desc_size = be16_to_cpu(qr->length);
	int desc_id = qr->idn;
	int ret;

	if (desc_size <= 0)
		return -EINVAL;

	ret = ufshcd_map_desc_id_to_length(hba, desc_id, desc_len);
	if (ret || !*desc_len)
		return -EINVAL;

	*desc_len = min_t(int, *desc_len, desc_size);

	return 0;
}

static int ufs_bsg_verify_query_size(struct ufs_hba *hba,
				     unsigned int request_len,
				     unsigned int reply_len)
{
	int min_req_len = sizeof(struct ufs_bsg_request);
	int min_rsp_len = sizeof(struct ufs_bsg_reply);

	if (min_req_len > request_len || min_rsp_len > reply_len) {
		dev_err(hba->dev, "not enough space assigned\n");
		return -EINVAL;
	}

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

static int ufs_bsg_request(struct bsg_job *job)
{
	struct ufs_bsg_request *bsg_request = job->request;
	struct ufs_bsg_reply *bsg_reply = job->reply;
	struct ufs_hba *hba = shost_priv(dev_to_shost(job->dev->parent));
	unsigned int req_len = job->request_len;
	unsigned int reply_len = job->reply_len;
	struct uic_command uc = {};
	int msgcode;
	uint8_t *desc_buff = NULL;
	int desc_len = 0;
	enum query_opcode desc_op = UPIU_QUERY_OPCODE_NOP;
	int ret;

	ret = ufs_bsg_verify_query_size(hba, req_len, reply_len);
	if (ret)
		goto out;

	bsg_reply->reply_payload_rcv_len = 0;

	pm_runtime_get_sync(hba->dev);

	msgcode = bsg_request->msgcode;
	switch (msgcode) {
	case UPIU_TRANSACTION_QUERY_REQ:
		desc_op = bsg_request->upiu_req.qr.opcode;
		ret = ufs_bsg_alloc_desc_buffer(hba, job, &desc_buff,
						&desc_len, desc_op);
		if (ret)
			goto out;

		/* fall through */
	case UPIU_TRANSACTION_NOP_OUT:
	case UPIU_TRANSACTION_TASK_REQ:
		ret = ufshcd_exec_raw_upiu_cmd(hba, &bsg_request->upiu_req,
					       &bsg_reply->upiu_rsp, msgcode,
					       desc_buff, &desc_len, desc_op);
		if (ret)
			dev_err(hba->dev,
				"exe raw upiu: error code %d\n", ret);

		break;
	case UPIU_TRANSACTION_UIC_CMD:
		memcpy(&uc, &bsg_request->upiu_req.uc, UIC_CMD_SIZE);
		ret = ufshcd_send_uic_cmd(hba, &uc);
		if (ret)
			dev_err(hba->dev,
				"send uic cmd: error code %d\n", ret);

		memcpy(&bsg_reply->upiu_rsp.uc, &uc, UIC_CMD_SIZE);

		break;
	default:
		ret = -ENOTSUPP;
		dev_err(hba->dev, "unsupported msgcode 0x%x\n", msgcode);

		break;
	}

	pm_runtime_put_sync(hba->dev);

	if (!desc_buff)
		goto out;

	if (desc_op == UPIU_QUERY_OPCODE_READ_DESC && desc_len)
		bsg_reply->reply_payload_rcv_len =
			sg_copy_from_buffer(job->request_payload.sg_list,
					    job->request_payload.sg_cnt,
					    desc_buff, desc_len);

	kfree(desc_buff);

out:
	bsg_reply->result = ret;
	job->reply_len = sizeof(struct ufs_bsg_reply);
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

	q = bsg_setup_queue(bsg_dev, dev_name(bsg_dev), ufs_bsg_request, NULL, 0);
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
