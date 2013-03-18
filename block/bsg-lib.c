/*
 *  BSG helper library
 *
 *  Copyright (C) 2008   James Smart, Emulex Corporation
 *  Copyright (C) 2011   Red Hat, Inc.  All rights reserved.
 *  Copyright (C) 2011   Mike Christie
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <linux/bsg-lib.h>
#include <linux/export.h>
#include <scsi/scsi_cmnd.h>

/**
 * bsg_destroy_job - routine to teardown/delete a bsg job
 * @job: bsg_job that is to be torn down
 */
static void bsg_destroy_job(struct bsg_job *job)
{
	put_device(job->dev);	/* release reference for the request */

	kfree(job->request_payload.sg_list);
	kfree(job->reply_payload.sg_list);
	kfree(job);
}

/**
 * bsg_job_done - completion routine for bsg requests
 * @job: bsg_job that is complete
 * @result: job reply result
 * @reply_payload_rcv_len: length of payload recvd
 *
 * The LLD should call this when the bsg job has completed.
 */
void bsg_job_done(struct bsg_job *job, int result,
		  unsigned int reply_payload_rcv_len)
{
	struct request *req = job->req;
	struct request *rsp = req->next_rq;
	int err;

	err = job->req->errors = result;
	if (err < 0)
		/* we're only returning the result field in the reply */
		job->req->sense_len = sizeof(u32);
	else
		job->req->sense_len = job->reply_len;
	/* we assume all request payload was transferred, residual == 0 */
	req->resid_len = 0;

	if (rsp) {
		WARN_ON(reply_payload_rcv_len > rsp->resid_len);

		/* set reply (bidi) residual */
		rsp->resid_len -= min(reply_payload_rcv_len, rsp->resid_len);
	}
	blk_complete_request(req);
}
EXPORT_SYMBOL_GPL(bsg_job_done);

/**
 * bsg_softirq_done - softirq done routine for destroying the bsg requests
 * @rq: BSG request that holds the job to be destroyed
 */
static void bsg_softirq_done(struct request *rq)
{
	struct bsg_job *job = rq->special;

	blk_end_request_all(rq, rq->errors);
	bsg_destroy_job(job);
}

static int bsg_map_buffer(struct bsg_buffer *buf, struct request *req)
{
	size_t sz = (sizeof(struct scatterlist) * req->nr_phys_segments);

	BUG_ON(!req->nr_phys_segments);

	buf->sg_list = kzalloc(sz, GFP_KERNEL);
	if (!buf->sg_list)
		return -ENOMEM;
	sg_init_table(buf->sg_list, req->nr_phys_segments);
	buf->sg_cnt = blk_rq_map_sg(req->q, req, buf->sg_list);
	buf->payload_len = blk_rq_bytes(req);
	return 0;
}

/**
 * bsg_create_job - create the bsg_job structure for the bsg request
 * @dev: device that is being sent the bsg request
 * @req: BSG request that needs a job structure
 */
static int bsg_create_job(struct device *dev, struct request *req)
{
	struct request *rsp = req->next_rq;
	struct request_queue *q = req->q;
	struct bsg_job *job;
	int ret;

	BUG_ON(req->special);

	job = kzalloc(sizeof(struct bsg_job) + q->bsg_job_size, GFP_KERNEL);
	if (!job)
		return -ENOMEM;

	req->special = job;
	job->req = req;
	if (q->bsg_job_size)
		job->dd_data = (void *)&job[1];
	job->request = req->cmd;
	job->request_len = req->cmd_len;
	job->reply = req->sense;
	job->reply_len = SCSI_SENSE_BUFFERSIZE;	/* Size of sense buffer
						 * allocated */
	if (req->bio) {
		ret = bsg_map_buffer(&job->request_payload, req);
		if (ret)
			goto failjob_rls_job;
	}
	if (rsp && rsp->bio) {
		ret = bsg_map_buffer(&job->reply_payload, rsp);
		if (ret)
			goto failjob_rls_rqst_payload;
	}
	job->dev = dev;
	/* take a reference for the request */
	get_device(job->dev);
	return 0;

failjob_rls_rqst_payload:
	kfree(job->request_payload.sg_list);
failjob_rls_job:
	kfree(job);
	return -ENOMEM;
}

/**
 * bsg_request_fn - generic handler for bsg requests
 * @q: request queue to manage
 *
 * On error the create_bsg_job function should return a -Exyz error value
 * that will be set to the req->errors.
 *
 * Drivers/subsys should pass this to the queue init function.
 */
void bsg_request_fn(struct request_queue *q)
{
	struct device *dev = q->queuedata;
	struct request *req;
	struct bsg_job *job;
	int ret;

	if (!get_device(dev))
		return;

	while (1) {
		req = blk_fetch_request(q);
		if (!req)
			break;
		spin_unlock_irq(q->queue_lock);

		ret = bsg_create_job(dev, req);
		if (ret) {
			req->errors = ret;
			blk_end_request_all(req, ret);
			spin_lock_irq(q->queue_lock);
			continue;
		}

		job = req->special;
		ret = q->bsg_job_fn(job);
		spin_lock_irq(q->queue_lock);
		if (ret)
			break;
	}

	spin_unlock_irq(q->queue_lock);
	put_device(dev);
	spin_lock_irq(q->queue_lock);
}
EXPORT_SYMBOL_GPL(bsg_request_fn);

/**
 * bsg_setup_queue - Create and add the bsg hooks so we can receive requests
 * @dev: device to attach bsg device to
 * @q: request queue setup by caller
 * @name: device to give bsg device
 * @job_fn: bsg job handler
 * @dd_job_size: size of LLD data needed for each job
 *
 * The caller should have setup the reuqest queue with bsg_request_fn
 * as the request_fn.
 */
int bsg_setup_queue(struct device *dev, struct request_queue *q,
		    char *name, bsg_job_fn *job_fn, int dd_job_size)
{
	int ret;

	q->queuedata = dev;
	q->bsg_job_size = dd_job_size;
	q->bsg_job_fn = job_fn;
	queue_flag_set_unlocked(QUEUE_FLAG_BIDI, q);
	blk_queue_softirq_done(q, bsg_softirq_done);
	blk_queue_rq_timeout(q, BLK_DEFAULT_SG_TIMEOUT);

	ret = bsg_register_queue(q, dev, name, NULL);
	if (ret) {
		printk(KERN_ERR "%s: bsg interface failed to "
		       "initialize - register queue\n", dev->kobj.name);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(bsg_setup_queue);
