// SPDX-License-Identifier: GPL-2.0
/*
 * bsg endpoint that supports UPIUs
 *
 * Copyright (C) 2018 Western Digital Corporation
 */
#include "ufs_bsg.h"


static int ufs_bsg_request(struct bsg_job *job)
{
	struct ufs_bsg_request *bsg_request = job->request;
	struct ufs_bsg_reply *bsg_reply = job->reply;
	int ret = -ENOTSUPP;

	bsg_reply->reply_payload_rcv_len = 0;

	/* Do Nothing for now */
	dev_err(job->dev, "unsupported message_code 0x%x\n",
		bsg_request->msgcode);

	bsg_reply->result = ret;
	job->reply_len = sizeof(struct ufs_bsg_reply) +
			 bsg_reply->reply_payload_rcv_len;

	bsg_job_done(job, ret, bsg_reply->reply_payload_rcv_len);

	return ret;
}

/**
 * ufs_bsg_remove - detach and remove the added ufs-bsg node
 *
 * Should be called when unloading the driver.
 */
void ufs_bsg_remove(struct ufs_hba *hba)
{
	struct device *bsg_dev = &hba->bsg_dev;

	if (!hba->bsg_queue)
		return;

	bsg_unregister_queue(hba->bsg_queue);

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

	dev_set_name(bsg_dev, "ufs-bsg");

	ret = device_add(bsg_dev);
	if (ret)
		goto out;

	q = bsg_setup_queue(bsg_dev, dev_name(bsg_dev), ufs_bsg_request, 0);
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
