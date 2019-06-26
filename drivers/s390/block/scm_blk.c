// SPDX-License-Identifier: GPL-2.0
/*
 * Block driver for s390 storage class memory.
 *
 * Copyright IBM Corp. 2012
 * Author(s): Sebastian Ott <sebott@linux.vnet.ibm.com>
 */

#define KMSG_COMPONENT "scm_block"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/eadm.h>
#include "scm_blk.h"

debug_info_t *scm_debug;
static int scm_major;
static mempool_t *aidaw_pool;
static DEFINE_SPINLOCK(list_lock);
static LIST_HEAD(inactive_requests);
static unsigned int nr_requests = 64;
static unsigned int nr_requests_per_io = 8;
static atomic_t nr_devices = ATOMIC_INIT(0);
module_param(nr_requests, uint, S_IRUGO);
MODULE_PARM_DESC(nr_requests, "Number of parallel requests.");

module_param(nr_requests_per_io, uint, S_IRUGO);
MODULE_PARM_DESC(nr_requests_per_io, "Number of requests per IO.");

MODULE_DESCRIPTION("Block driver for s390 storage class memory.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("scm:scmdev*");

static void __scm_free_rq(struct scm_request *scmrq)
{
	struct aob_rq_header *aobrq = to_aobrq(scmrq);

	free_page((unsigned long) scmrq->aob);
	kfree(scmrq->request);
	kfree(aobrq);
}

static void scm_free_rqs(void)
{
	struct list_head *iter, *safe;
	struct scm_request *scmrq;

	spin_lock_irq(&list_lock);
	list_for_each_safe(iter, safe, &inactive_requests) {
		scmrq = list_entry(iter, struct scm_request, list);
		list_del(&scmrq->list);
		__scm_free_rq(scmrq);
	}
	spin_unlock_irq(&list_lock);

	mempool_destroy(aidaw_pool);
}

static int __scm_alloc_rq(void)
{
	struct aob_rq_header *aobrq;
	struct scm_request *scmrq;

	aobrq = kzalloc(sizeof(*aobrq) + sizeof(*scmrq), GFP_KERNEL);
	if (!aobrq)
		return -ENOMEM;

	scmrq = (void *) aobrq->data;
	scmrq->aob = (void *) get_zeroed_page(GFP_DMA);
	if (!scmrq->aob)
		goto free;

	scmrq->request = kcalloc(nr_requests_per_io, sizeof(scmrq->request[0]),
				 GFP_KERNEL);
	if (!scmrq->request)
		goto free;

	INIT_LIST_HEAD(&scmrq->list);
	spin_lock_irq(&list_lock);
	list_add(&scmrq->list, &inactive_requests);
	spin_unlock_irq(&list_lock);

	return 0;
free:
	__scm_free_rq(scmrq);
	return -ENOMEM;
}

static int scm_alloc_rqs(unsigned int nrqs)
{
	int ret = 0;

	aidaw_pool = mempool_create_page_pool(max(nrqs/8, 1U), 0);
	if (!aidaw_pool)
		return -ENOMEM;

	while (nrqs-- && !ret)
		ret = __scm_alloc_rq();

	return ret;
}

static struct scm_request *scm_request_fetch(void)
{
	struct scm_request *scmrq = NULL;

	spin_lock_irq(&list_lock);
	if (list_empty(&inactive_requests))
		goto out;
	scmrq = list_first_entry(&inactive_requests, struct scm_request, list);
	list_del(&scmrq->list);
out:
	spin_unlock_irq(&list_lock);
	return scmrq;
}

static void scm_request_done(struct scm_request *scmrq)
{
	unsigned long flags;
	struct msb *msb;
	u64 aidaw;
	int i;

	for (i = 0; i < nr_requests_per_io && scmrq->request[i]; i++) {
		msb = &scmrq->aob->msb[i];
		aidaw = msb->data_addr;

		if ((msb->flags & MSB_FLAG_IDA) && aidaw &&
		    IS_ALIGNED(aidaw, PAGE_SIZE))
			mempool_free(virt_to_page(aidaw), aidaw_pool);
	}

	spin_lock_irqsave(&list_lock, flags);
	list_add(&scmrq->list, &inactive_requests);
	spin_unlock_irqrestore(&list_lock, flags);
}

static bool scm_permit_request(struct scm_blk_dev *bdev, struct request *req)
{
	return rq_data_dir(req) != WRITE || bdev->state != SCM_WR_PROHIBIT;
}

static inline struct aidaw *scm_aidaw_alloc(void)
{
	struct page *page = mempool_alloc(aidaw_pool, GFP_ATOMIC);

	return page ? page_address(page) : NULL;
}

static inline unsigned long scm_aidaw_bytes(struct aidaw *aidaw)
{
	unsigned long _aidaw = (unsigned long) aidaw;
	unsigned long bytes = ALIGN(_aidaw, PAGE_SIZE) - _aidaw;

	return (bytes / sizeof(*aidaw)) * PAGE_SIZE;
}

struct aidaw *scm_aidaw_fetch(struct scm_request *scmrq, unsigned int bytes)
{
	struct aidaw *aidaw;

	if (scm_aidaw_bytes(scmrq->next_aidaw) >= bytes)
		return scmrq->next_aidaw;

	aidaw = scm_aidaw_alloc();
	if (aidaw)
		memset(aidaw, 0, PAGE_SIZE);
	return aidaw;
}

static int scm_request_prepare(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	struct scm_device *scmdev = bdev->gendisk->private_data;
	int pos = scmrq->aob->request.msb_count;
	struct msb *msb = &scmrq->aob->msb[pos];
	struct request *req = scmrq->request[pos];
	struct req_iterator iter;
	struct aidaw *aidaw;
	struct bio_vec bv;

	aidaw = scm_aidaw_fetch(scmrq, blk_rq_bytes(req));
	if (!aidaw)
		return -ENOMEM;

	msb->bs = MSB_BS_4K;
	scmrq->aob->request.msb_count++;
	msb->scm_addr = scmdev->address + ((u64) blk_rq_pos(req) << 9);
	msb->oc = (rq_data_dir(req) == READ) ? MSB_OC_READ : MSB_OC_WRITE;
	msb->flags |= MSB_FLAG_IDA;
	msb->data_addr = (u64) aidaw;

	rq_for_each_segment(bv, req, iter) {
		WARN_ON(bv.bv_offset);
		msb->blk_count += bv.bv_len >> 12;
		aidaw->data_addr = (u64) page_address(bv.bv_page);
		aidaw++;
	}

	scmrq->next_aidaw = aidaw;
	return 0;
}

static inline void scm_request_set(struct scm_request *scmrq,
				   struct request *req)
{
	scmrq->request[scmrq->aob->request.msb_count] = req;
}

static inline void scm_request_init(struct scm_blk_dev *bdev,
				    struct scm_request *scmrq)
{
	struct aob_rq_header *aobrq = to_aobrq(scmrq);
	struct aob *aob = scmrq->aob;

	memset(scmrq->request, 0,
	       nr_requests_per_io * sizeof(scmrq->request[0]));
	memset(aob, 0, sizeof(*aob));
	aobrq->scmdev = bdev->scmdev;
	aob->request.cmd_code = ARQB_CMD_MOVE;
	aob->request.data = (u64) aobrq;
	scmrq->bdev = bdev;
	scmrq->retries = 4;
	scmrq->error = BLK_STS_OK;
	/* We don't use all msbs - place aidaws at the end of the aob page. */
	scmrq->next_aidaw = (void *) &aob->msb[nr_requests_per_io];
}

static void scm_request_requeue(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	int i;

	for (i = 0; i < nr_requests_per_io && scmrq->request[i]; i++)
		blk_mq_requeue_request(scmrq->request[i], false);

	atomic_dec(&bdev->queued_reqs);
	scm_request_done(scmrq);
	blk_mq_kick_requeue_list(bdev->rq);
}

static void scm_request_finish(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	blk_status_t *error;
	int i;

	for (i = 0; i < nr_requests_per_io && scmrq->request[i]; i++) {
		error = blk_mq_rq_to_pdu(scmrq->request[i]);
		*error = scmrq->error;
		blk_mq_complete_request(scmrq->request[i]);
	}

	atomic_dec(&bdev->queued_reqs);
	scm_request_done(scmrq);
}

static void scm_request_start(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;

	atomic_inc(&bdev->queued_reqs);
	if (eadm_start_aob(scmrq->aob)) {
		SCM_LOG(5, "no subchannel");
		scm_request_requeue(scmrq);
	}
}

struct scm_queue {
	struct scm_request *scmrq;
	spinlock_t lock;
};

static blk_status_t scm_blk_request(struct blk_mq_hw_ctx *hctx,
			   const struct blk_mq_queue_data *qd)
{
	struct scm_device *scmdev = hctx->queue->queuedata;
	struct scm_blk_dev *bdev = dev_get_drvdata(&scmdev->dev);
	struct scm_queue *sq = hctx->driver_data;
	struct request *req = qd->rq;
	struct scm_request *scmrq;

	spin_lock(&sq->lock);
	if (!scm_permit_request(bdev, req)) {
		spin_unlock(&sq->lock);
		return BLK_STS_RESOURCE;
	}

	scmrq = sq->scmrq;
	if (!scmrq) {
		scmrq = scm_request_fetch();
		if (!scmrq) {
			SCM_LOG(5, "no request");
			spin_unlock(&sq->lock);
			return BLK_STS_RESOURCE;
		}
		scm_request_init(bdev, scmrq);
		sq->scmrq = scmrq;
	}
	scm_request_set(scmrq, req);

	if (scm_request_prepare(scmrq)) {
		SCM_LOG(5, "aidaw alloc failed");
		scm_request_set(scmrq, NULL);

		if (scmrq->aob->request.msb_count)
			scm_request_start(scmrq);

		sq->scmrq = NULL;
		spin_unlock(&sq->lock);
		return BLK_STS_RESOURCE;
	}
	blk_mq_start_request(req);

	if (qd->last || scmrq->aob->request.msb_count == nr_requests_per_io) {
		scm_request_start(scmrq);
		sq->scmrq = NULL;
	}
	spin_unlock(&sq->lock);
	return BLK_STS_OK;
}

static int scm_blk_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
			     unsigned int idx)
{
	struct scm_queue *qd = kzalloc(sizeof(*qd), GFP_KERNEL);

	if (!qd)
		return -ENOMEM;

	spin_lock_init(&qd->lock);
	hctx->driver_data = qd;

	return 0;
}

static void scm_blk_exit_hctx(struct blk_mq_hw_ctx *hctx, unsigned int idx)
{
	struct scm_queue *qd = hctx->driver_data;

	WARN_ON(qd->scmrq);
	kfree(hctx->driver_data);
	hctx->driver_data = NULL;
}

static void __scmrq_log_error(struct scm_request *scmrq)
{
	struct aob *aob = scmrq->aob;

	if (scmrq->error == BLK_STS_TIMEOUT)
		SCM_LOG(1, "Request timeout");
	else {
		SCM_LOG(1, "Request error");
		SCM_LOG_HEX(1, &aob->response, sizeof(aob->response));
	}
	if (scmrq->retries)
		SCM_LOG(1, "Retry request");
	else
		pr_err("An I/O operation to SCM failed with rc=%d\n",
		       scmrq->error);
}

static void scm_blk_handle_error(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	unsigned long flags;

	if (scmrq->error != BLK_STS_IOERR)
		goto restart;

	/* For -EIO the response block is valid. */
	switch (scmrq->aob->response.eqc) {
	case EQC_WR_PROHIBIT:
		spin_lock_irqsave(&bdev->lock, flags);
		if (bdev->state != SCM_WR_PROHIBIT)
			pr_info("%lx: Write access to the SCM increment is suspended\n",
				(unsigned long) bdev->scmdev->address);
		bdev->state = SCM_WR_PROHIBIT;
		spin_unlock_irqrestore(&bdev->lock, flags);
		goto requeue;
	default:
		break;
	}

restart:
	if (!eadm_start_aob(scmrq->aob))
		return;

requeue:
	scm_request_requeue(scmrq);
}

void scm_blk_irq(struct scm_device *scmdev, void *data, blk_status_t error)
{
	struct scm_request *scmrq = data;

	scmrq->error = error;
	if (error) {
		__scmrq_log_error(scmrq);
		if (scmrq->retries-- > 0) {
			scm_blk_handle_error(scmrq);
			return;
		}
	}

	scm_request_finish(scmrq);
}

static void scm_blk_request_done(struct request *req)
{
	blk_status_t *error = blk_mq_rq_to_pdu(req);

	blk_mq_end_request(req, *error);
}

static const struct block_device_operations scm_blk_devops = {
	.owner = THIS_MODULE,
};

static const struct blk_mq_ops scm_mq_ops = {
	.queue_rq = scm_blk_request,
	.complete = scm_blk_request_done,
	.init_hctx = scm_blk_init_hctx,
	.exit_hctx = scm_blk_exit_hctx,
};

int scm_blk_dev_setup(struct scm_blk_dev *bdev, struct scm_device *scmdev)
{
	unsigned int devindex, nr_max_blk;
	struct request_queue *rq;
	int len, ret;

	devindex = atomic_inc_return(&nr_devices) - 1;
	/* scma..scmz + scmaa..scmzz */
	if (devindex > 701) {
		ret = -ENODEV;
		goto out;
	}

	bdev->scmdev = scmdev;
	bdev->state = SCM_OPER;
	spin_lock_init(&bdev->lock);
	atomic_set(&bdev->queued_reqs, 0);

	bdev->tag_set.ops = &scm_mq_ops;
	bdev->tag_set.cmd_size = sizeof(blk_status_t);
	bdev->tag_set.nr_hw_queues = nr_requests;
	bdev->tag_set.queue_depth = nr_requests_per_io * nr_requests;
	bdev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	bdev->tag_set.numa_node = NUMA_NO_NODE;

	ret = blk_mq_alloc_tag_set(&bdev->tag_set);
	if (ret)
		goto out;

	rq = blk_mq_init_queue(&bdev->tag_set);
	if (IS_ERR(rq)) {
		ret = PTR_ERR(rq);
		goto out_tag;
	}
	bdev->rq = rq;
	nr_max_blk = min(scmdev->nr_max_block,
			 (unsigned int) (PAGE_SIZE / sizeof(struct aidaw)));

	blk_queue_logical_block_size(rq, 1 << 12);
	blk_queue_max_hw_sectors(rq, nr_max_blk << 3); /* 8 * 512 = blk_size */
	blk_queue_max_segments(rq, nr_max_blk);
	blk_queue_flag_set(QUEUE_FLAG_NONROT, rq);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, rq);

	bdev->gendisk = alloc_disk(SCM_NR_PARTS);
	if (!bdev->gendisk) {
		ret = -ENOMEM;
		goto out_queue;
	}
	rq->queuedata = scmdev;
	bdev->gendisk->private_data = scmdev;
	bdev->gendisk->fops = &scm_blk_devops;
	bdev->gendisk->queue = rq;
	bdev->gendisk->major = scm_major;
	bdev->gendisk->first_minor = devindex * SCM_NR_PARTS;

	len = snprintf(bdev->gendisk->disk_name, DISK_NAME_LEN, "scm");
	if (devindex > 25) {
		len += snprintf(bdev->gendisk->disk_name + len,
				DISK_NAME_LEN - len, "%c",
				'a' + (devindex / 26) - 1);
		devindex = devindex % 26;
	}
	snprintf(bdev->gendisk->disk_name + len, DISK_NAME_LEN - len, "%c",
		 'a' + devindex);

	/* 512 byte sectors */
	set_capacity(bdev->gendisk, scmdev->size >> 9);
	device_add_disk(&scmdev->dev, bdev->gendisk, NULL);
	return 0;

out_queue:
	blk_cleanup_queue(rq);
out_tag:
	blk_mq_free_tag_set(&bdev->tag_set);
out:
	atomic_dec(&nr_devices);
	return ret;
}

void scm_blk_dev_cleanup(struct scm_blk_dev *bdev)
{
	del_gendisk(bdev->gendisk);
	blk_cleanup_queue(bdev->gendisk->queue);
	blk_mq_free_tag_set(&bdev->tag_set);
	put_disk(bdev->gendisk);
}

void scm_blk_set_available(struct scm_blk_dev *bdev)
{
	unsigned long flags;

	spin_lock_irqsave(&bdev->lock, flags);
	if (bdev->state == SCM_WR_PROHIBIT)
		pr_info("%lx: Write access to the SCM increment is restored\n",
			(unsigned long) bdev->scmdev->address);
	bdev->state = SCM_OPER;
	spin_unlock_irqrestore(&bdev->lock, flags);
}

static bool __init scm_blk_params_valid(void)
{
	if (!nr_requests_per_io || nr_requests_per_io > 64)
		return false;

	return true;
}

static int __init scm_blk_init(void)
{
	int ret = -EINVAL;

	if (!scm_blk_params_valid())
		goto out;

	ret = register_blkdev(0, "scm");
	if (ret < 0)
		goto out;

	scm_major = ret;
	ret = scm_alloc_rqs(nr_requests);
	if (ret)
		goto out_free;

	scm_debug = debug_register("scm_log", 16, 1, 16);
	if (!scm_debug) {
		ret = -ENOMEM;
		goto out_free;
	}

	debug_register_view(scm_debug, &debug_hex_ascii_view);
	debug_set_level(scm_debug, 2);

	ret = scm_drv_init();
	if (ret)
		goto out_dbf;

	return ret;

out_dbf:
	debug_unregister(scm_debug);
out_free:
	scm_free_rqs();
	unregister_blkdev(scm_major, "scm");
out:
	return ret;
}
module_init(scm_blk_init);

static void __exit scm_blk_cleanup(void)
{
	scm_drv_cleanup();
	debug_unregister(scm_debug);
	scm_free_rqs();
	unregister_blkdev(scm_major, "scm");
}
module_exit(scm_blk_cleanup);
