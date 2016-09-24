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
	__scm_free_rq_cluster(scmrq);
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

	if (__scm_alloc_rq_cluster(scmrq))
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

	spin_lock(&list_lock);
	if (list_empty(&inactive_requests))
		goto out;
	scmrq = list_first_entry(&inactive_requests, struct scm_request, list);
	list_del(&scmrq->list);
out:
	spin_unlock(&list_lock);
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
	scmrq->error = 0;
	/* We don't use all msbs - place aidaws at the end of the aob page. */
	scmrq->next_aidaw = (void *) &aob->msb[nr_requests_per_io];
	scm_request_cluster_init(scmrq);
}

static void scm_ensure_queue_restart(struct scm_blk_dev *bdev)
{
	if (atomic_read(&bdev->queued_reqs)) {
		/* Queue restart is triggered by the next interrupt. */
		return;
	}
	blk_delay_queue(bdev->rq, SCM_QUEUE_DELAY);
}

void scm_request_requeue(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	int i;

	scm_release_cluster(scmrq);
	for (i = 0; i < nr_requests_per_io && scmrq->request[i]; i++)
		blk_requeue_request(bdev->rq, scmrq->request[i]);

	atomic_dec(&bdev->queued_reqs);
	scm_request_done(scmrq);
	scm_ensure_queue_restart(bdev);
}

void scm_request_finish(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	int i;

	scm_release_cluster(scmrq);
	for (i = 0; i < nr_requests_per_io && scmrq->request[i]; i++)
		blk_end_request_all(scmrq->request[i], scmrq->error);

	atomic_dec(&bdev->queued_reqs);
	scm_request_done(scmrq);
}

static int scm_request_start(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	int ret;

	atomic_inc(&bdev->queued_reqs);
	if (!scmrq->aob->request.msb_count) {
		scm_request_requeue(scmrq);
		return -EINVAL;
	}

	ret = eadm_start_aob(scmrq->aob);
	if (ret) {
		SCM_LOG(5, "no subchannel");
		scm_request_requeue(scmrq);
	}
	return ret;
}

static void scm_blk_request(struct request_queue *rq)
{
	struct scm_device *scmdev = rq->queuedata;
	struct scm_blk_dev *bdev = dev_get_drvdata(&scmdev->dev);
	struct scm_request *scmrq = NULL;
	struct request *req;

	while ((req = blk_peek_request(rq))) {
		if (req->cmd_type != REQ_TYPE_FS) {
			blk_start_request(req);
			blk_dump_rq_flags(req, KMSG_COMPONENT " bad request");
			__blk_end_request_all(req, -EIO);
			continue;
		}

		if (!scm_permit_request(bdev, req))
			goto out;

		if (!scmrq) {
			scmrq = scm_request_fetch();
			if (!scmrq) {
				SCM_LOG(5, "no request");
				goto out;
			}
			scm_request_init(bdev, scmrq);
		}
		scm_request_set(scmrq, req);

		if (!scm_reserve_cluster(scmrq)) {
			SCM_LOG(5, "cluster busy");
			scm_request_set(scmrq, NULL);
			if (scmrq->aob->request.msb_count)
				goto out;

			scm_request_done(scmrq);
			return;
		}

		if (scm_need_cluster_request(scmrq)) {
			if (scmrq->aob->request.msb_count) {
				/* Start cluster requests separately. */
				scm_request_set(scmrq, NULL);
				if (scm_request_start(scmrq))
					return;
			} else {
				atomic_inc(&bdev->queued_reqs);
				blk_start_request(req);
				scm_initiate_cluster_request(scmrq);
			}
			scmrq = NULL;
			continue;
		}

		if (scm_request_prepare(scmrq)) {
			SCM_LOG(5, "aidaw alloc failed");
			scm_request_set(scmrq, NULL);
			goto out;
		}
		blk_start_request(req);

		if (scmrq->aob->request.msb_count < nr_requests_per_io)
			continue;

		if (scm_request_start(scmrq))
			return;

		scmrq = NULL;
	}
out:
	if (scmrq)
		scm_request_start(scmrq);
	else
		scm_ensure_queue_restart(bdev);
}

static void __scmrq_log_error(struct scm_request *scmrq)
{
	struct aob *aob = scmrq->aob;

	if (scmrq->error == -ETIMEDOUT)
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

void scm_blk_irq(struct scm_device *scmdev, void *data, int error)
{
	struct scm_request *scmrq = data;
	struct scm_blk_dev *bdev = scmrq->bdev;

	scmrq->error = error;
	if (error)
		__scmrq_log_error(scmrq);

	spin_lock(&bdev->lock);
	list_add_tail(&scmrq->list, &bdev->finished_requests);
	spin_unlock(&bdev->lock);
	tasklet_hi_schedule(&bdev->tasklet);
}

static void scm_blk_handle_error(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	unsigned long flags;

	if (scmrq->error != -EIO)
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
	spin_lock_irqsave(&bdev->rq_lock, flags);
	scm_request_requeue(scmrq);
	spin_unlock_irqrestore(&bdev->rq_lock, flags);
}

static void scm_blk_tasklet(struct scm_blk_dev *bdev)
{
	struct scm_request *scmrq;
	unsigned long flags;

	spin_lock_irqsave(&bdev->lock, flags);
	while (!list_empty(&bdev->finished_requests)) {
		scmrq = list_first_entry(&bdev->finished_requests,
					 struct scm_request, list);
		list_del(&scmrq->list);
		spin_unlock_irqrestore(&bdev->lock, flags);

		if (scmrq->error && scmrq->retries-- > 0) {
			scm_blk_handle_error(scmrq);

			/* Request restarted or requeued, handle next. */
			spin_lock_irqsave(&bdev->lock, flags);
			continue;
		}

		if (scm_test_cluster_request(scmrq)) {
			scm_cluster_request_irq(scmrq);
			spin_lock_irqsave(&bdev->lock, flags);
			continue;
		}

		scm_request_finish(scmrq);
		spin_lock_irqsave(&bdev->lock, flags);
	}
	spin_unlock_irqrestore(&bdev->lock, flags);
	/* Look out for more requests. */
	blk_run_queue(bdev->rq);
}

static const struct block_device_operations scm_blk_devops = {
	.owner = THIS_MODULE,
};

int scm_blk_dev_setup(struct scm_blk_dev *bdev, struct scm_device *scmdev)
{
	struct request_queue *rq;
	int len, ret = -ENOMEM;
	unsigned int devindex, nr_max_blk;

	devindex = atomic_inc_return(&nr_devices) - 1;
	/* scma..scmz + scmaa..scmzz */
	if (devindex > 701) {
		ret = -ENODEV;
		goto out;
	}

	bdev->scmdev = scmdev;
	bdev->state = SCM_OPER;
	spin_lock_init(&bdev->rq_lock);
	spin_lock_init(&bdev->lock);
	INIT_LIST_HEAD(&bdev->finished_requests);
	atomic_set(&bdev->queued_reqs, 0);
	tasklet_init(&bdev->tasklet,
		     (void (*)(unsigned long)) scm_blk_tasklet,
		     (unsigned long) bdev);

	rq = blk_init_queue(scm_blk_request, &bdev->rq_lock);
	if (!rq)
		goto out;

	bdev->rq = rq;
	nr_max_blk = min(scmdev->nr_max_block,
			 (unsigned int) (PAGE_SIZE / sizeof(struct aidaw)));

	blk_queue_logical_block_size(rq, 1 << 12);
	blk_queue_max_hw_sectors(rq, nr_max_blk << 3); /* 8 * 512 = blk_size */
	blk_queue_max_segments(rq, nr_max_blk);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, rq);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, rq);
	scm_blk_dev_cluster_setup(bdev);

	bdev->gendisk = alloc_disk(SCM_NR_PARTS);
	if (!bdev->gendisk)
		goto out_queue;

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
	device_add_disk(&scmdev->dev, bdev->gendisk);
	return 0;

out_queue:
	blk_cleanup_queue(rq);
out:
	atomic_dec(&nr_devices);
	return ret;
}

void scm_blk_dev_cleanup(struct scm_blk_dev *bdev)
{
	tasklet_kill(&bdev->tasklet);
	del_gendisk(bdev->gendisk);
	blk_cleanup_queue(bdev->gendisk->queue);
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

	return scm_cluster_size_valid();
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
