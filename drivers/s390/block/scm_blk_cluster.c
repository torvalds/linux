/*
 * Block driver for s390 storage class memory.
 *
 * Copyright IBM Corp. 2012
 * Author(s): Sebastian Ott <sebott@linux.vnet.ibm.com>
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/eadm.h>
#include "scm_blk.h"

static unsigned int write_cluster_size = 64;
module_param(write_cluster_size, uint, S_IRUGO);
MODULE_PARM_DESC(write_cluster_size,
		 "Number of pages used for contiguous writes.");

#define CLUSTER_SIZE (write_cluster_size * PAGE_SIZE)

void __scm_free_rq_cluster(struct scm_request *scmrq)
{
	int i;

	if (!scmrq->cluster.buf)
		return;

	for (i = 0; i < 2 * write_cluster_size; i++)
		free_page((unsigned long) scmrq->cluster.buf[i]);

	kfree(scmrq->cluster.buf);
}

int __scm_alloc_rq_cluster(struct scm_request *scmrq)
{
	int i;

	scmrq->cluster.buf = kzalloc(sizeof(void *) * 2 * write_cluster_size,
				 GFP_KERNEL);
	if (!scmrq->cluster.buf)
		return -ENOMEM;

	for (i = 0; i < 2 * write_cluster_size; i++) {
		scmrq->cluster.buf[i] = (void *) get_zeroed_page(GFP_DMA);
		if (!scmrq->cluster.buf[i])
			return -ENOMEM;
	}
	INIT_LIST_HEAD(&scmrq->cluster.list);
	return 0;
}

void scm_request_cluster_init(struct scm_request *scmrq)
{
	scmrq->cluster.state = CLUSTER_NONE;
}

static bool clusters_intersect(struct request *A, struct request *B)
{
	unsigned long firstA, lastA, firstB, lastB;

	firstA = ((u64) blk_rq_pos(A) << 9) / CLUSTER_SIZE;
	lastA = (((u64) blk_rq_pos(A) << 9) +
		    blk_rq_bytes(A) - 1) / CLUSTER_SIZE;

	firstB = ((u64) blk_rq_pos(B) << 9) / CLUSTER_SIZE;
	lastB = (((u64) blk_rq_pos(B) << 9) +
		    blk_rq_bytes(B) - 1) / CLUSTER_SIZE;

	return (firstB <= lastA && firstA <= lastB);
}

bool scm_reserve_cluster(struct scm_request *scmrq)
{
	struct request *req = scmrq->request[scmrq->aob->request.msb_count];
	struct scm_blk_dev *bdev = scmrq->bdev;
	struct scm_request *iter;
	int pos, add = 1;

	if (write_cluster_size == 0)
		return true;

	spin_lock(&bdev->lock);
	list_for_each_entry(iter, &bdev->cluster_list, cluster.list) {
		if (iter == scmrq) {
			/*
			 * We don't have to use clusters_intersect here, since
			 * cluster requests are always started separately.
			 */
			add = 0;
			continue;
		}
		for (pos = 0; pos <= iter->aob->request.msb_count; pos++) {
			if (clusters_intersect(req, iter->request[pos]) &&
			    (rq_data_dir(req) == WRITE ||
			     rq_data_dir(iter->request[pos]) == WRITE)) {
				spin_unlock(&bdev->lock);
				return false;
			}
		}
	}
	if (add)
		list_add(&scmrq->cluster.list, &bdev->cluster_list);
	spin_unlock(&bdev->lock);

	return true;
}

void scm_release_cluster(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	unsigned long flags;

	if (write_cluster_size == 0)
		return;

	spin_lock_irqsave(&bdev->lock, flags);
	list_del(&scmrq->cluster.list);
	spin_unlock_irqrestore(&bdev->lock, flags);
}

void scm_blk_dev_cluster_setup(struct scm_blk_dev *bdev)
{
	INIT_LIST_HEAD(&bdev->cluster_list);
	blk_queue_io_opt(bdev->rq, CLUSTER_SIZE);
}

static int scm_prepare_cluster_request(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	struct scm_device *scmdev = bdev->gendisk->private_data;
	struct request *req = scmrq->request[0];
	struct msb *msb = &scmrq->aob->msb[0];
	struct req_iterator iter;
	struct aidaw *aidaw;
	struct bio_vec bv;
	int i = 0;
	u64 addr;

	switch (scmrq->cluster.state) {
	case CLUSTER_NONE:
		scmrq->cluster.state = CLUSTER_READ;
		/* fall through */
	case CLUSTER_READ:
		msb->bs = MSB_BS_4K;
		msb->oc = MSB_OC_READ;
		msb->flags = MSB_FLAG_IDA;
		msb->blk_count = write_cluster_size;

		addr = scmdev->address + ((u64) blk_rq_pos(req) << 9);
		msb->scm_addr = round_down(addr, CLUSTER_SIZE);

		if (msb->scm_addr !=
		    round_down(addr + (u64) blk_rq_bytes(req) - 1,
			       CLUSTER_SIZE))
			msb->blk_count = 2 * write_cluster_size;

		aidaw = scm_aidaw_fetch(scmrq, msb->blk_count * PAGE_SIZE);
		if (!aidaw)
			return -ENOMEM;

		scmrq->aob->request.msb_count = 1;
		msb->data_addr = (u64) aidaw;
		for (i = 0; i < msb->blk_count; i++) {
			aidaw->data_addr = (u64) scmrq->cluster.buf[i];
			aidaw++;
		}

		break;
	case CLUSTER_WRITE:
		aidaw = (void *) msb->data_addr;
		msb->oc = MSB_OC_WRITE;

		for (addr = msb->scm_addr;
		     addr < scmdev->address + ((u64) blk_rq_pos(req) << 9);
		     addr += PAGE_SIZE) {
			aidaw->data_addr = (u64) scmrq->cluster.buf[i];
			aidaw++;
			i++;
		}
		rq_for_each_segment(bv, req, iter) {
			aidaw->data_addr = (u64) page_address(bv.bv_page);
			aidaw++;
			i++;
		}
		for (; i < msb->blk_count; i++) {
			aidaw->data_addr = (u64) scmrq->cluster.buf[i];
			aidaw++;
		}
		break;
	}
	return 0;
}

bool scm_need_cluster_request(struct scm_request *scmrq)
{
	int pos = scmrq->aob->request.msb_count;

	if (rq_data_dir(scmrq->request[pos]) == READ)
		return false;

	return blk_rq_bytes(scmrq->request[pos]) < CLUSTER_SIZE;
}

/* Called with queue lock held. */
void scm_initiate_cluster_request(struct scm_request *scmrq)
{
	if (scm_prepare_cluster_request(scmrq))
		goto requeue;
	if (eadm_start_aob(scmrq->aob))
		goto requeue;
	return;
requeue:
	scm_request_requeue(scmrq);
}

bool scm_test_cluster_request(struct scm_request *scmrq)
{
	return scmrq->cluster.state != CLUSTER_NONE;
}

void scm_cluster_request_irq(struct scm_request *scmrq)
{
	struct scm_blk_dev *bdev = scmrq->bdev;
	unsigned long flags;

	switch (scmrq->cluster.state) {
	case CLUSTER_NONE:
		BUG();
		break;
	case CLUSTER_READ:
		if (scmrq->error) {
			scm_request_finish(scmrq);
			break;
		}
		scmrq->cluster.state = CLUSTER_WRITE;
		spin_lock_irqsave(&bdev->rq_lock, flags);
		scm_initiate_cluster_request(scmrq);
		spin_unlock_irqrestore(&bdev->rq_lock, flags);
		break;
	case CLUSTER_WRITE:
		scm_request_finish(scmrq);
		break;
	}
}

bool scm_cluster_size_valid(void)
{
	if (write_cluster_size == 1 || write_cluster_size > 128)
		return false;

	return !(write_cluster_size & (write_cluster_size - 1));
}
