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
#include <linux/blk-mq.h>
#include <linux/delay.h>
#include <linux/scatterlist.h>
#include <linux/bsg-lib.h>
#include <linux/export.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/sg.h>

#define uptr64(val) ((void __user *)(uintptr_t)(val))

struct bsg_set {
	struct blk_mq_tag_set	tag_set;
	bsg_job_fn		*job_fn;
	bsg_timeout_fn		*timeout_fn;
};

static int bsg_transport_check_proto(struct sg_io_v4 *hdr)
{
	if (hdr->protocol != BSG_PROTOCOL_SCSI  ||
	    hdr->subprotocol != BSG_SUB_PROTOCOL_SCSI_TRANSPORT)
		return -EINVAL;
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	return 0;
}

static int bsg_transport_fill_hdr(struct request *rq, struct sg_io_v4 *hdr,
		fmode_t mode)
{
	struct bsg_job *job = blk_mq_rq_to_pdu(rq);
	int ret;

	job->request_len = hdr->request_len;
	job->request = memdup_user(uptr64(hdr->request), hdr->request_len);
	if (IS_ERR(job->request))
		return PTR_ERR(job->request);

	if (hdr->dout_xfer_len && hdr->din_xfer_len) {
		job->bidi_rq = blk_get_request(rq->q, REQ_OP_SCSI_IN, 0);
		if (IS_ERR(job->bidi_rq)) {
			ret = PTR_ERR(job->bidi_rq);
			goto out;
		}

		ret = blk_rq_map_user(rq->q, job->bidi_rq, NULL,
				uptr64(hdr->din_xferp), hdr->din_xfer_len,
				GFP_KERNEL);
		if (ret)
			goto out_free_bidi_rq;

		job->bidi_bio = job->bidi_rq->bio;
	} else {
		job->bidi_rq = NULL;
		job->bidi_bio = NULL;
	}

	return 0;

out_free_bidi_rq:
	if (job->bidi_rq)
		blk_put_request(job->bidi_rq);
out:
	kfree(job->request);
	return ret;
}

static int bsg_transport_complete_rq(struct request *rq, struct sg_io_v4 *hdr)
{
	struct bsg_job *job = blk_mq_rq_to_pdu(rq);
	int ret = 0;

	/*
	 * The assignments below don't make much sense, but are kept for
	 * bug by bug backwards compatibility:
	 */
	hdr->device_status = job->result & 0xff;
	hdr->transport_status = host_byte(job->result);
	hdr->driver_status = driver_byte(job->result);
	hdr->info = 0;
	if (hdr->device_status || hdr->transport_status || hdr->driver_status)
		hdr->info |= SG_INFO_CHECK;
	hdr->response_len = 0;

	if (job->result < 0) {
		/* we're only returning the result field in the reply */
		job->reply_len = sizeof(u32);
		ret = job->result;
	}

	if (job->reply_len && hdr->response) {
		int len = min(hdr->max_response_len, job->reply_len);

		if (copy_to_user(uptr64(hdr->response), job->reply, len))
			ret = -EFAULT;
		else
			hdr->response_len = len;
	}

	/* we assume all request payload was transferred, residual == 0 */
	hdr->dout_resid = 0;

	if (job->bidi_rq) {
		unsigned int rsp_len = job->reply_payload.payload_len;

		if (WARN_ON(job->reply_payload_rcv_len > rsp_len))
			hdr->din_resid = 0;
		else
			hdr->din_resid = rsp_len - job->reply_payload_rcv_len;
	} else {
		hdr->din_resid = 0;
	}

	return ret;
}

static void bsg_transport_free_rq(struct request *rq)
{
	struct bsg_job *job = blk_mq_rq_to_pdu(rq);

	if (job->bidi_rq) {
		blk_rq_unmap_user(job->bidi_bio);
		blk_put_request(job->bidi_rq);
	}

	kfree(job->request);
}

static const struct bsg_ops bsg_transport_ops = {
	.check_proto		= bsg_transport_check_proto,
	.fill_hdr		= bsg_transport_fill_hdr,
	.complete_rq		= bsg_transport_complete_rq,
	.free_rq		= bsg_transport_free_rq,
};

/**
 * bsg_teardown_job - routine to teardown a bsg job
 * @kref: kref inside bsg_job that is to be torn down
 */
static void bsg_teardown_job(struct kref *kref)
{
	struct bsg_job *job = container_of(kref, struct bsg_job, kref);
	struct request *rq = blk_mq_rq_from_pdu(job);

	put_device(job->dev);	/* release reference for the request */

	kfree(job->request_payload.sg_list);
	kfree(job->reply_payload.sg_list);

	blk_mq_end_request(rq, BLK_STS_OK);
}

void bsg_job_put(struct bsg_job *job)
{
	kref_put(&job->kref, bsg_teardown_job);
}
EXPORT_SYMBOL_GPL(bsg_job_put);

int bsg_job_get(struct bsg_job *job)
{
	return kref_get_unless_zero(&job->kref);
}
EXPORT_SYMBOL_GPL(bsg_job_get);

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
	job->result = result;
	job->reply_payload_rcv_len = reply_payload_rcv_len;
	blk_mq_complete_request(blk_mq_rq_from_pdu(job));
}
EXPORT_SYMBOL_GPL(bsg_job_done);

/**
 * bsg_complete - softirq done routine for destroying the bsg requests
 * @rq: BSG request that holds the job to be destroyed
 */
static void bsg_complete(struct request *rq)
{
	struct bsg_job *job = blk_mq_rq_to_pdu(rq);

	bsg_job_put(job);
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
 * bsg_prepare_job - create the bsg_job structure for the bsg request
 * @dev: device that is being sent the bsg request
 * @req: BSG request that needs a job structure
 */
static bool bsg_prepare_job(struct device *dev, struct request *req)
{
	struct bsg_job *job = blk_mq_rq_to_pdu(req);
	int ret;

	job->timeout = req->timeout;

	if (req->bio) {
		ret = bsg_map_buffer(&job->request_payload, req);
		if (ret)
			goto failjob_rls_job;
	}
	if (job->bidi_rq) {
		ret = bsg_map_buffer(&job->reply_payload, job->bidi_rq);
		if (ret)
			goto failjob_rls_rqst_payload;
	}
	job->dev = dev;
	/* take a reference for the request */
	get_device(job->dev);
	kref_init(&job->kref);
	return true;

failjob_rls_rqst_payload:
	kfree(job->request_payload.sg_list);
failjob_rls_job:
	job->result = -ENOMEM;
	return false;
}

/**
 * bsg_queue_rq - generic handler for bsg requests
 * @hctx: hardware queue
 * @bd: queue data
 *
 * On error the create_bsg_job function should return a -Exyz error value
 * that will be set to ->result.
 *
 * Drivers/subsys should pass this to the queue init function.
 */
static blk_status_t bsg_queue_rq(struct blk_mq_hw_ctx *hctx,
				 const struct blk_mq_queue_data *bd)
{
	struct request_queue *q = hctx->queue;
	struct device *dev = q->queuedata;
	struct request *req = bd->rq;
	struct bsg_set *bset =
		container_of(q->tag_set, struct bsg_set, tag_set);
	int ret;

	blk_mq_start_request(req);

	if (!get_device(dev))
		return BLK_STS_IOERR;

	if (!bsg_prepare_job(dev, req))
		return BLK_STS_IOERR;

	ret = bset->job_fn(blk_mq_rq_to_pdu(req));
	if (ret)
		return BLK_STS_IOERR;

	put_device(dev);
	return BLK_STS_OK;
}

/* called right after the request is allocated for the request_queue */
static int bsg_init_rq(struct blk_mq_tag_set *set, struct request *req,
		       unsigned int hctx_idx, unsigned int numa_node)
{
	struct bsg_job *job = blk_mq_rq_to_pdu(req);

	job->reply = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_KERNEL);
	if (!job->reply)
		return -ENOMEM;
	return 0;
}

/* called right before the request is given to the request_queue user */
static void bsg_initialize_rq(struct request *req)
{
	struct bsg_job *job = blk_mq_rq_to_pdu(req);
	void *reply = job->reply;

	memset(job, 0, sizeof(*job));
	job->reply = reply;
	job->reply_len = SCSI_SENSE_BUFFERSIZE;
	job->dd_data = job + 1;
}

static void bsg_exit_rq(struct blk_mq_tag_set *set, struct request *req,
		       unsigned int hctx_idx)
{
	struct bsg_job *job = blk_mq_rq_to_pdu(req);

	kfree(job->reply);
}

void bsg_remove_queue(struct request_queue *q)
{
	if (q) {
		struct bsg_set *bset =
			container_of(q->tag_set, struct bsg_set, tag_set);

		bsg_unregister_queue(q);
		blk_cleanup_queue(q);
		blk_mq_free_tag_set(&bset->tag_set);
		kfree(bset);
	}
}
EXPORT_SYMBOL_GPL(bsg_remove_queue);

static enum blk_eh_timer_return bsg_timeout(struct request *rq, bool reserved)
{
	struct bsg_set *bset =
		container_of(rq->q->tag_set, struct bsg_set, tag_set);

	if (!bset->timeout_fn)
		return BLK_EH_DONE;
	return bset->timeout_fn(rq);
}

static const struct blk_mq_ops bsg_mq_ops = {
	.queue_rq		= bsg_queue_rq,
	.init_request		= bsg_init_rq,
	.exit_request		= bsg_exit_rq,
	.initialize_rq_fn	= bsg_initialize_rq,
	.complete		= bsg_complete,
	.timeout		= bsg_timeout,
};

/**
 * bsg_setup_queue - Create and add the bsg hooks so we can receive requests
 * @dev: device to attach bsg device to
 * @name: device to give bsg device
 * @job_fn: bsg job handler
 * @dd_job_size: size of LLD data needed for each job
 */
struct request_queue *bsg_setup_queue(struct device *dev, const char *name,
		bsg_job_fn *job_fn, bsg_timeout_fn *timeout, int dd_job_size)
{
	struct bsg_set *bset;
	struct blk_mq_tag_set *set;
	struct request_queue *q;
	int ret = -ENOMEM;

	bset = kzalloc(sizeof(*bset), GFP_KERNEL);
	if (!bset)
		return ERR_PTR(-ENOMEM);

	bset->job_fn = job_fn;
	bset->timeout_fn = timeout;

	set = &bset->tag_set;
	set->ops = &bsg_mq_ops,
	set->nr_hw_queues = 1;
	set->queue_depth = 128;
	set->numa_node = NUMA_NO_NODE;
	set->cmd_size = sizeof(struct bsg_job) + dd_job_size;
	set->flags = BLK_MQ_F_NO_SCHED | BLK_MQ_F_BLOCKING;
	if (blk_mq_alloc_tag_set(set))
		goto out_tag_set;

	q = blk_mq_init_queue(set);
	if (IS_ERR(q)) {
		ret = PTR_ERR(q);
		goto out_queue;
	}

	q->queuedata = dev;
	blk_queue_rq_timeout(q, BLK_DEFAULT_SG_TIMEOUT);

	ret = bsg_register_queue(q, dev, name, &bsg_transport_ops);
	if (ret) {
		printk(KERN_ERR "%s: bsg interface failed to "
		       "initialize - register queue\n", dev->kobj.name);
		goto out_cleanup_queue;
	}

	return q;
out_cleanup_queue:
	blk_cleanup_queue(q);
out_queue:
	blk_mq_free_tag_set(set);
out_tag_set:
	kfree(bset);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(bsg_setup_queue);
