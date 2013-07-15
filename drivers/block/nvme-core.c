/*
 * NVM Express device driver
 * Copyright (c) 2011, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/nvme.h>
#include <linux/bio.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kdev_t.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/poison.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <scsi/sg.h>
#include <asm-generic/io-64-nonatomic-lo-hi.h>

#define NVME_Q_DEPTH 1024
#define SQ_SIZE(depth)		(depth * sizeof(struct nvme_command))
#define CQ_SIZE(depth)		(depth * sizeof(struct nvme_completion))
#define NVME_MINORS 64
#define ADMIN_TIMEOUT	(60 * HZ)

static int nvme_major;
module_param(nvme_major, int, 0);

static int use_threaded_interrupts;
module_param(use_threaded_interrupts, int, 0);

static DEFINE_SPINLOCK(dev_list_lock);
static LIST_HEAD(dev_list);
static struct task_struct *nvme_thread;

/*
 * An NVM Express queue.  Each device has at least two (one for admin
 * commands and one for I/O commands).
 */
struct nvme_queue {
	struct device *q_dmadev;
	struct nvme_dev *dev;
	spinlock_t q_lock;
	struct nvme_command *sq_cmds;
	volatile struct nvme_completion *cqes;
	dma_addr_t sq_dma_addr;
	dma_addr_t cq_dma_addr;
	wait_queue_head_t sq_full;
	wait_queue_t sq_cong_wait;
	struct bio_list sq_cong;
	u32 __iomem *q_db;
	u16 q_depth;
	u16 cq_vector;
	u16 sq_head;
	u16 sq_tail;
	u16 cq_head;
	u8 cq_phase;
	u8 cqe_seen;
	unsigned long cmdid_data[];
};

/*
 * Check we didin't inadvertently grow the command struct
 */
static inline void _nvme_check_size(void)
{
	BUILD_BUG_ON(sizeof(struct nvme_rw_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_create_cq) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_create_sq) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_delete_queue) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_features) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_format_cmd) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_id_ctrl) != 4096);
	BUILD_BUG_ON(sizeof(struct nvme_id_ns) != 4096);
	BUILD_BUG_ON(sizeof(struct nvme_lba_range_type) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_smart_log) != 512);
}

typedef void (*nvme_completion_fn)(struct nvme_dev *, void *,
						struct nvme_completion *);

struct nvme_cmd_info {
	nvme_completion_fn fn;
	void *ctx;
	unsigned long timeout;
};

static struct nvme_cmd_info *nvme_cmd_info(struct nvme_queue *nvmeq)
{
	return (void *)&nvmeq->cmdid_data[BITS_TO_LONGS(nvmeq->q_depth)];
}

/**
 * alloc_cmdid() - Allocate a Command ID
 * @nvmeq: The queue that will be used for this command
 * @ctx: A pointer that will be passed to the handler
 * @handler: The function to call on completion
 *
 * Allocate a Command ID for a queue.  The data passed in will
 * be passed to the completion handler.  This is implemented by using
 * the bottom two bits of the ctx pointer to store the handler ID.
 * Passing in a pointer that's not 4-byte aligned will cause a BUG.
 * We can change this if it becomes a problem.
 *
 * May be called with local interrupts disabled and the q_lock held,
 * or with interrupts enabled and no locks held.
 */
static int alloc_cmdid(struct nvme_queue *nvmeq, void *ctx,
				nvme_completion_fn handler, unsigned timeout)
{
	int depth = nvmeq->q_depth - 1;
	struct nvme_cmd_info *info = nvme_cmd_info(nvmeq);
	int cmdid;

	do {
		cmdid = find_first_zero_bit(nvmeq->cmdid_data, depth);
		if (cmdid >= depth)
			return -EBUSY;
	} while (test_and_set_bit(cmdid, nvmeq->cmdid_data));

	info[cmdid].fn = handler;
	info[cmdid].ctx = ctx;
	info[cmdid].timeout = jiffies + timeout;
	return cmdid;
}

static int alloc_cmdid_killable(struct nvme_queue *nvmeq, void *ctx,
				nvme_completion_fn handler, unsigned timeout)
{
	int cmdid;
	wait_event_killable(nvmeq->sq_full,
		(cmdid = alloc_cmdid(nvmeq, ctx, handler, timeout)) >= 0);
	return (cmdid < 0) ? -EINTR : cmdid;
}

/* Special values must be less than 0x1000 */
#define CMD_CTX_BASE		((void *)POISON_POINTER_DELTA)
#define CMD_CTX_CANCELLED	(0x30C + CMD_CTX_BASE)
#define CMD_CTX_COMPLETED	(0x310 + CMD_CTX_BASE)
#define CMD_CTX_INVALID		(0x314 + CMD_CTX_BASE)
#define CMD_CTX_FLUSH		(0x318 + CMD_CTX_BASE)

static void special_completion(struct nvme_dev *dev, void *ctx,
						struct nvme_completion *cqe)
{
	if (ctx == CMD_CTX_CANCELLED)
		return;
	if (ctx == CMD_CTX_FLUSH)
		return;
	if (ctx == CMD_CTX_COMPLETED) {
		dev_warn(&dev->pci_dev->dev,
				"completed id %d twice on queue %d\n",
				cqe->command_id, le16_to_cpup(&cqe->sq_id));
		return;
	}
	if (ctx == CMD_CTX_INVALID) {
		dev_warn(&dev->pci_dev->dev,
				"invalid id %d completed on queue %d\n",
				cqe->command_id, le16_to_cpup(&cqe->sq_id));
		return;
	}

	dev_warn(&dev->pci_dev->dev, "Unknown special completion %p\n", ctx);
}

/*
 * Called with local interrupts disabled and the q_lock held.  May not sleep.
 */
static void *free_cmdid(struct nvme_queue *nvmeq, int cmdid,
						nvme_completion_fn *fn)
{
	void *ctx;
	struct nvme_cmd_info *info = nvme_cmd_info(nvmeq);

	if (cmdid >= nvmeq->q_depth) {
		*fn = special_completion;
		return CMD_CTX_INVALID;
	}
	if (fn)
		*fn = info[cmdid].fn;
	ctx = info[cmdid].ctx;
	info[cmdid].fn = special_completion;
	info[cmdid].ctx = CMD_CTX_COMPLETED;
	clear_bit(cmdid, nvmeq->cmdid_data);
	wake_up(&nvmeq->sq_full);
	return ctx;
}

static void *cancel_cmdid(struct nvme_queue *nvmeq, int cmdid,
						nvme_completion_fn *fn)
{
	void *ctx;
	struct nvme_cmd_info *info = nvme_cmd_info(nvmeq);
	if (fn)
		*fn = info[cmdid].fn;
	ctx = info[cmdid].ctx;
	info[cmdid].fn = special_completion;
	info[cmdid].ctx = CMD_CTX_CANCELLED;
	return ctx;
}

struct nvme_queue *get_nvmeq(struct nvme_dev *dev)
{
	return dev->queues[get_cpu() + 1];
}

void put_nvmeq(struct nvme_queue *nvmeq)
{
	put_cpu();
}

/**
 * nvme_submit_cmd() - Copy a command into a queue and ring the doorbell
 * @nvmeq: The queue to use
 * @cmd: The command to send
 *
 * Safe to use from interrupt context
 */
static int nvme_submit_cmd(struct nvme_queue *nvmeq, struct nvme_command *cmd)
{
	unsigned long flags;
	u16 tail;
	spin_lock_irqsave(&nvmeq->q_lock, flags);
	tail = nvmeq->sq_tail;
	memcpy(&nvmeq->sq_cmds[tail], cmd, sizeof(*cmd));
	if (++tail == nvmeq->q_depth)
		tail = 0;
	writel(tail, nvmeq->q_db);
	nvmeq->sq_tail = tail;
	spin_unlock_irqrestore(&nvmeq->q_lock, flags);

	return 0;
}

static __le64 **iod_list(struct nvme_iod *iod)
{
	return ((void *)iod) + iod->offset;
}

/*
 * Will slightly overestimate the number of pages needed.  This is OK
 * as it only leads to a small amount of wasted memory for the lifetime of
 * the I/O.
 */
static int nvme_npages(unsigned size)
{
	unsigned nprps = DIV_ROUND_UP(size + PAGE_SIZE, PAGE_SIZE);
	return DIV_ROUND_UP(8 * nprps, PAGE_SIZE - 8);
}

static struct nvme_iod *
nvme_alloc_iod(unsigned nseg, unsigned nbytes, gfp_t gfp)
{
	struct nvme_iod *iod = kmalloc(sizeof(struct nvme_iod) +
				sizeof(__le64 *) * nvme_npages(nbytes) +
				sizeof(struct scatterlist) * nseg, gfp);

	if (iod) {
		iod->offset = offsetof(struct nvme_iod, sg[nseg]);
		iod->npages = -1;
		iod->length = nbytes;
		iod->nents = 0;
		iod->start_time = jiffies;
	}

	return iod;
}

void nvme_free_iod(struct nvme_dev *dev, struct nvme_iod *iod)
{
	const int last_prp = PAGE_SIZE / 8 - 1;
	int i;
	__le64 **list = iod_list(iod);
	dma_addr_t prp_dma = iod->first_dma;

	if (iod->npages == 0)
		dma_pool_free(dev->prp_small_pool, list[0], prp_dma);
	for (i = 0; i < iod->npages; i++) {
		__le64 *prp_list = list[i];
		dma_addr_t next_prp_dma = le64_to_cpu(prp_list[last_prp]);
		dma_pool_free(dev->prp_page_pool, prp_list, prp_dma);
		prp_dma = next_prp_dma;
	}
	kfree(iod);
}

static void nvme_start_io_acct(struct bio *bio)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	const int rw = bio_data_dir(bio);
	int cpu = part_stat_lock();
	part_round_stats(cpu, &disk->part0);
	part_stat_inc(cpu, &disk->part0, ios[rw]);
	part_stat_add(cpu, &disk->part0, sectors[rw], bio_sectors(bio));
	part_inc_in_flight(&disk->part0, rw);
	part_stat_unlock();
}

static void nvme_end_io_acct(struct bio *bio, unsigned long start_time)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	const int rw = bio_data_dir(bio);
	unsigned long duration = jiffies - start_time;
	int cpu = part_stat_lock();
	part_stat_add(cpu, &disk->part0, ticks[rw], duration);
	part_round_stats(cpu, &disk->part0);
	part_dec_in_flight(&disk->part0, rw);
	part_stat_unlock();
}

static void bio_completion(struct nvme_dev *dev, void *ctx,
						struct nvme_completion *cqe)
{
	struct nvme_iod *iod = ctx;
	struct bio *bio = iod->private;
	u16 status = le16_to_cpup(&cqe->status) >> 1;

	if (iod->nents) {
		dma_unmap_sg(&dev->pci_dev->dev, iod->sg, iod->nents,
			bio_data_dir(bio) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		nvme_end_io_acct(bio, iod->start_time);
	}
	nvme_free_iod(dev, iod);
	if (status)
		bio_endio(bio, -EIO);
	else
		bio_endio(bio, 0);
}

/* length is in bytes.  gfp flags indicates whether we may sleep. */
int nvme_setup_prps(struct nvme_dev *dev, struct nvme_common_command *cmd,
			struct nvme_iod *iod, int total_len, gfp_t gfp)
{
	struct dma_pool *pool;
	int length = total_len;
	struct scatterlist *sg = iod->sg;
	int dma_len = sg_dma_len(sg);
	u64 dma_addr = sg_dma_address(sg);
	int offset = offset_in_page(dma_addr);
	__le64 *prp_list;
	__le64 **list = iod_list(iod);
	dma_addr_t prp_dma;
	int nprps, i;

	cmd->prp1 = cpu_to_le64(dma_addr);
	length -= (PAGE_SIZE - offset);
	if (length <= 0)
		return total_len;

	dma_len -= (PAGE_SIZE - offset);
	if (dma_len) {
		dma_addr += (PAGE_SIZE - offset);
	} else {
		sg = sg_next(sg);
		dma_addr = sg_dma_address(sg);
		dma_len = sg_dma_len(sg);
	}

	if (length <= PAGE_SIZE) {
		cmd->prp2 = cpu_to_le64(dma_addr);
		return total_len;
	}

	nprps = DIV_ROUND_UP(length, PAGE_SIZE);
	if (nprps <= (256 / 8)) {
		pool = dev->prp_small_pool;
		iod->npages = 0;
	} else {
		pool = dev->prp_page_pool;
		iod->npages = 1;
	}

	prp_list = dma_pool_alloc(pool, gfp, &prp_dma);
	if (!prp_list) {
		cmd->prp2 = cpu_to_le64(dma_addr);
		iod->npages = -1;
		return (total_len - length) + PAGE_SIZE;
	}
	list[0] = prp_list;
	iod->first_dma = prp_dma;
	cmd->prp2 = cpu_to_le64(prp_dma);
	i = 0;
	for (;;) {
		if (i == PAGE_SIZE / 8) {
			__le64 *old_prp_list = prp_list;
			prp_list = dma_pool_alloc(pool, gfp, &prp_dma);
			if (!prp_list)
				return total_len - length;
			list[iod->npages++] = prp_list;
			prp_list[0] = old_prp_list[i - 1];
			old_prp_list[i - 1] = cpu_to_le64(prp_dma);
			i = 1;
		}
		prp_list[i++] = cpu_to_le64(dma_addr);
		dma_len -= PAGE_SIZE;
		dma_addr += PAGE_SIZE;
		length -= PAGE_SIZE;
		if (length <= 0)
			break;
		if (dma_len > 0)
			continue;
		BUG_ON(dma_len < 0);
		sg = sg_next(sg);
		dma_addr = sg_dma_address(sg);
		dma_len = sg_dma_len(sg);
	}

	return total_len;
}

struct nvme_bio_pair {
	struct bio b1, b2, *parent;
	struct bio_vec *bv1, *bv2;
	int err;
	atomic_t cnt;
};

static void nvme_bio_pair_endio(struct bio *bio, int err)
{
	struct nvme_bio_pair *bp = bio->bi_private;

	if (err)
		bp->err = err;

	if (atomic_dec_and_test(&bp->cnt)) {
		bio_endio(bp->parent, bp->err);
		kfree(bp->bv1);
		kfree(bp->bv2);
		kfree(bp);
	}
}

static struct nvme_bio_pair *nvme_bio_split(struct bio *bio, int idx,
							int len, int offset)
{
	struct nvme_bio_pair *bp;

	BUG_ON(len > bio->bi_size);
	BUG_ON(idx > bio->bi_vcnt);

	bp = kmalloc(sizeof(*bp), GFP_ATOMIC);
	if (!bp)
		return NULL;
	bp->err = 0;

	bp->b1 = *bio;
	bp->b2 = *bio;

	bp->b1.bi_size = len;
	bp->b2.bi_size -= len;
	bp->b1.bi_vcnt = idx;
	bp->b2.bi_idx = idx;
	bp->b2.bi_sector += len >> 9;

	if (offset) {
		bp->bv1 = kmalloc(bio->bi_max_vecs * sizeof(struct bio_vec),
								GFP_ATOMIC);
		if (!bp->bv1)
			goto split_fail_1;

		bp->bv2 = kmalloc(bio->bi_max_vecs * sizeof(struct bio_vec),
								GFP_ATOMIC);
		if (!bp->bv2)
			goto split_fail_2;

		memcpy(bp->bv1, bio->bi_io_vec,
			bio->bi_max_vecs * sizeof(struct bio_vec));
		memcpy(bp->bv2, bio->bi_io_vec,
			bio->bi_max_vecs * sizeof(struct bio_vec));

		bp->b1.bi_io_vec = bp->bv1;
		bp->b2.bi_io_vec = bp->bv2;
		bp->b2.bi_io_vec[idx].bv_offset += offset;
		bp->b2.bi_io_vec[idx].bv_len -= offset;
		bp->b1.bi_io_vec[idx].bv_len = offset;
		bp->b1.bi_vcnt++;
	} else
		bp->bv1 = bp->bv2 = NULL;

	bp->b1.bi_private = bp;
	bp->b2.bi_private = bp;

	bp->b1.bi_end_io = nvme_bio_pair_endio;
	bp->b2.bi_end_io = nvme_bio_pair_endio;

	bp->parent = bio;
	atomic_set(&bp->cnt, 2);

	return bp;

 split_fail_2:
	kfree(bp->bv1);
 split_fail_1:
	kfree(bp);
	return NULL;
}

static int nvme_split_and_submit(struct bio *bio, struct nvme_queue *nvmeq,
						int idx, int len, int offset)
{
	struct nvme_bio_pair *bp = nvme_bio_split(bio, idx, len, offset);
	if (!bp)
		return -ENOMEM;

	if (bio_list_empty(&nvmeq->sq_cong))
		add_wait_queue(&nvmeq->sq_full, &nvmeq->sq_cong_wait);
	bio_list_add(&nvmeq->sq_cong, &bp->b1);
	bio_list_add(&nvmeq->sq_cong, &bp->b2);

	return 0;
}

/* NVMe scatterlists require no holes in the virtual address */
#define BIOVEC_NOT_VIRT_MERGEABLE(vec1, vec2)	((vec2)->bv_offset || \
			(((vec1)->bv_offset + (vec1)->bv_len) % PAGE_SIZE))

static int nvme_map_bio(struct nvme_queue *nvmeq, struct nvme_iod *iod,
		struct bio *bio, enum dma_data_direction dma_dir, int psegs)
{
	struct bio_vec *bvec, *bvprv = NULL;
	struct scatterlist *sg = NULL;
	int i, length = 0, nsegs = 0, split_len = bio->bi_size;

	if (nvmeq->dev->stripe_size)
		split_len = nvmeq->dev->stripe_size -
			((bio->bi_sector << 9) & (nvmeq->dev->stripe_size - 1));

	sg_init_table(iod->sg, psegs);
	bio_for_each_segment(bvec, bio, i) {
		if (bvprv && BIOVEC_PHYS_MERGEABLE(bvprv, bvec)) {
			sg->length += bvec->bv_len;
		} else {
			if (bvprv && BIOVEC_NOT_VIRT_MERGEABLE(bvprv, bvec))
				return nvme_split_and_submit(bio, nvmeq, i,
								length, 0);

			sg = sg ? sg + 1 : iod->sg;
			sg_set_page(sg, bvec->bv_page, bvec->bv_len,
							bvec->bv_offset);
			nsegs++;
		}

		if (split_len - length < bvec->bv_len)
			return nvme_split_and_submit(bio, nvmeq, i, split_len,
							split_len - length);
		length += bvec->bv_len;
		bvprv = bvec;
	}
	iod->nents = nsegs;
	sg_mark_end(sg);
	if (dma_map_sg(nvmeq->q_dmadev, iod->sg, iod->nents, dma_dir) == 0)
		return -ENOMEM;

	BUG_ON(length != bio->bi_size);
	return length;
}

/*
 * We reuse the small pool to allocate the 16-byte range here as it is not
 * worth having a special pool for these or additional cases to handle freeing
 * the iod.
 */
static int nvme_submit_discard(struct nvme_queue *nvmeq, struct nvme_ns *ns,
		struct bio *bio, struct nvme_iod *iod, int cmdid)
{
	struct nvme_dsm_range *range;
	struct nvme_command *cmnd = &nvmeq->sq_cmds[nvmeq->sq_tail];

	range = dma_pool_alloc(nvmeq->dev->prp_small_pool, GFP_ATOMIC,
							&iod->first_dma);
	if (!range)
		return -ENOMEM;

	iod_list(iod)[0] = (__le64 *)range;
	iod->npages = 0;

	range->cattr = cpu_to_le32(0);
	range->nlb = cpu_to_le32(bio->bi_size >> ns->lba_shift);
	range->slba = cpu_to_le64(nvme_block_nr(ns, bio->bi_sector));

	memset(cmnd, 0, sizeof(*cmnd));
	cmnd->dsm.opcode = nvme_cmd_dsm;
	cmnd->dsm.command_id = cmdid;
	cmnd->dsm.nsid = cpu_to_le32(ns->ns_id);
	cmnd->dsm.prp1 = cpu_to_le64(iod->first_dma);
	cmnd->dsm.nr = 0;
	cmnd->dsm.attributes = cpu_to_le32(NVME_DSMGMT_AD);

	if (++nvmeq->sq_tail == nvmeq->q_depth)
		nvmeq->sq_tail = 0;
	writel(nvmeq->sq_tail, nvmeq->q_db);

	return 0;
}

static int nvme_submit_flush(struct nvme_queue *nvmeq, struct nvme_ns *ns,
								int cmdid)
{
	struct nvme_command *cmnd = &nvmeq->sq_cmds[nvmeq->sq_tail];

	memset(cmnd, 0, sizeof(*cmnd));
	cmnd->common.opcode = nvme_cmd_flush;
	cmnd->common.command_id = cmdid;
	cmnd->common.nsid = cpu_to_le32(ns->ns_id);

	if (++nvmeq->sq_tail == nvmeq->q_depth)
		nvmeq->sq_tail = 0;
	writel(nvmeq->sq_tail, nvmeq->q_db);

	return 0;
}

int nvme_submit_flush_data(struct nvme_queue *nvmeq, struct nvme_ns *ns)
{
	int cmdid = alloc_cmdid(nvmeq, (void *)CMD_CTX_FLUSH,
					special_completion, NVME_IO_TIMEOUT);
	if (unlikely(cmdid < 0))
		return cmdid;

	return nvme_submit_flush(nvmeq, ns, cmdid);
}

/*
 * Called with local interrupts disabled and the q_lock held.  May not sleep.
 */
static int nvme_submit_bio_queue(struct nvme_queue *nvmeq, struct nvme_ns *ns,
								struct bio *bio)
{
	struct nvme_command *cmnd;
	struct nvme_iod *iod;
	enum dma_data_direction dma_dir;
	int cmdid, length, result;
	u16 control;
	u32 dsmgmt;
	int psegs = bio_phys_segments(ns->queue, bio);

	if ((bio->bi_rw & REQ_FLUSH) && psegs) {
		result = nvme_submit_flush_data(nvmeq, ns);
		if (result)
			return result;
	}

	result = -ENOMEM;
	iod = nvme_alloc_iod(psegs, bio->bi_size, GFP_ATOMIC);
	if (!iod)
		goto nomem;
	iod->private = bio;

	result = -EBUSY;
	cmdid = alloc_cmdid(nvmeq, iod, bio_completion, NVME_IO_TIMEOUT);
	if (unlikely(cmdid < 0))
		goto free_iod;

	if (bio->bi_rw & REQ_DISCARD) {
		result = nvme_submit_discard(nvmeq, ns, bio, iod, cmdid);
		if (result)
			goto free_cmdid;
		return result;
	}
	if ((bio->bi_rw & REQ_FLUSH) && !psegs)
		return nvme_submit_flush(nvmeq, ns, cmdid);

	control = 0;
	if (bio->bi_rw & REQ_FUA)
		control |= NVME_RW_FUA;
	if (bio->bi_rw & (REQ_FAILFAST_DEV | REQ_RAHEAD))
		control |= NVME_RW_LR;

	dsmgmt = 0;
	if (bio->bi_rw & REQ_RAHEAD)
		dsmgmt |= NVME_RW_DSM_FREQ_PREFETCH;

	cmnd = &nvmeq->sq_cmds[nvmeq->sq_tail];

	memset(cmnd, 0, sizeof(*cmnd));
	if (bio_data_dir(bio)) {
		cmnd->rw.opcode = nvme_cmd_write;
		dma_dir = DMA_TO_DEVICE;
	} else {
		cmnd->rw.opcode = nvme_cmd_read;
		dma_dir = DMA_FROM_DEVICE;
	}

	result = nvme_map_bio(nvmeq, iod, bio, dma_dir, psegs);
	if (result <= 0)
		goto free_cmdid;
	length = result;

	cmnd->rw.command_id = cmdid;
	cmnd->rw.nsid = cpu_to_le32(ns->ns_id);
	length = nvme_setup_prps(nvmeq->dev, &cmnd->common, iod, length,
								GFP_ATOMIC);
	cmnd->rw.slba = cpu_to_le64(nvme_block_nr(ns, bio->bi_sector));
	cmnd->rw.length = cpu_to_le16((length >> ns->lba_shift) - 1);
	cmnd->rw.control = cpu_to_le16(control);
	cmnd->rw.dsmgmt = cpu_to_le32(dsmgmt);

	nvme_start_io_acct(bio);
	if (++nvmeq->sq_tail == nvmeq->q_depth)
		nvmeq->sq_tail = 0;
	writel(nvmeq->sq_tail, nvmeq->q_db);

	return 0;

 free_cmdid:
	free_cmdid(nvmeq, cmdid, NULL);
 free_iod:
	nvme_free_iod(nvmeq->dev, iod);
 nomem:
	return result;
}

static int nvme_process_cq(struct nvme_queue *nvmeq)
{
	u16 head, phase;

	head = nvmeq->cq_head;
	phase = nvmeq->cq_phase;

	for (;;) {
		void *ctx;
		nvme_completion_fn fn;
		struct nvme_completion cqe = nvmeq->cqes[head];
		if ((le16_to_cpu(cqe.status) & 1) != phase)
			break;
		nvmeq->sq_head = le16_to_cpu(cqe.sq_head);
		if (++head == nvmeq->q_depth) {
			head = 0;
			phase = !phase;
		}

		ctx = free_cmdid(nvmeq, cqe.command_id, &fn);
		fn(nvmeq->dev, ctx, &cqe);
	}

	/* If the controller ignores the cq head doorbell and continuously
	 * writes to the queue, it is theoretically possible to wrap around
	 * the queue twice and mistakenly return IRQ_NONE.  Linux only
	 * requires that 0.1% of your interrupts are handled, so this isn't
	 * a big problem.
	 */
	if (head == nvmeq->cq_head && phase == nvmeq->cq_phase)
		return 0;

	writel(head, nvmeq->q_db + (1 << nvmeq->dev->db_stride));
	nvmeq->cq_head = head;
	nvmeq->cq_phase = phase;

	nvmeq->cqe_seen = 1;
	return 1;
}

static void nvme_make_request(struct request_queue *q, struct bio *bio)
{
	struct nvme_ns *ns = q->queuedata;
	struct nvme_queue *nvmeq = get_nvmeq(ns->dev);
	int result = -EBUSY;

	spin_lock_irq(&nvmeq->q_lock);
	if (bio_list_empty(&nvmeq->sq_cong))
		result = nvme_submit_bio_queue(nvmeq, ns, bio);
	if (unlikely(result)) {
		if (bio_list_empty(&nvmeq->sq_cong))
			add_wait_queue(&nvmeq->sq_full, &nvmeq->sq_cong_wait);
		bio_list_add(&nvmeq->sq_cong, bio);
	}

	nvme_process_cq(nvmeq);
	spin_unlock_irq(&nvmeq->q_lock);
	put_nvmeq(nvmeq);
}

static irqreturn_t nvme_irq(int irq, void *data)
{
	irqreturn_t result;
	struct nvme_queue *nvmeq = data;
	spin_lock(&nvmeq->q_lock);
	nvme_process_cq(nvmeq);
	result = nvmeq->cqe_seen ? IRQ_HANDLED : IRQ_NONE;
	nvmeq->cqe_seen = 0;
	spin_unlock(&nvmeq->q_lock);
	return result;
}

static irqreturn_t nvme_irq_check(int irq, void *data)
{
	struct nvme_queue *nvmeq = data;
	struct nvme_completion cqe = nvmeq->cqes[nvmeq->cq_head];
	if ((le16_to_cpu(cqe.status) & 1) != nvmeq->cq_phase)
		return IRQ_NONE;
	return IRQ_WAKE_THREAD;
}

static void nvme_abort_command(struct nvme_queue *nvmeq, int cmdid)
{
	spin_lock_irq(&nvmeq->q_lock);
	cancel_cmdid(nvmeq, cmdid, NULL);
	spin_unlock_irq(&nvmeq->q_lock);
}

struct sync_cmd_info {
	struct task_struct *task;
	u32 result;
	int status;
};

static void sync_completion(struct nvme_dev *dev, void *ctx,
						struct nvme_completion *cqe)
{
	struct sync_cmd_info *cmdinfo = ctx;
	cmdinfo->result = le32_to_cpup(&cqe->result);
	cmdinfo->status = le16_to_cpup(&cqe->status) >> 1;
	wake_up_process(cmdinfo->task);
}

/*
 * Returns 0 on success.  If the result is negative, it's a Linux error code;
 * if the result is positive, it's an NVM Express status code
 */
int nvme_submit_sync_cmd(struct nvme_queue *nvmeq, struct nvme_command *cmd,
						u32 *result, unsigned timeout)
{
	int cmdid;
	struct sync_cmd_info cmdinfo;

	cmdinfo.task = current;
	cmdinfo.status = -EINTR;

	cmdid = alloc_cmdid_killable(nvmeq, &cmdinfo, sync_completion,
								timeout);
	if (cmdid < 0)
		return cmdid;
	cmd->common.command_id = cmdid;

	set_current_state(TASK_KILLABLE);
	nvme_submit_cmd(nvmeq, cmd);
	schedule_timeout(timeout);

	if (cmdinfo.status == -EINTR) {
		nvme_abort_command(nvmeq, cmdid);
		return -EINTR;
	}

	if (result)
		*result = cmdinfo.result;

	return cmdinfo.status;
}

int nvme_submit_admin_cmd(struct nvme_dev *dev, struct nvme_command *cmd,
								u32 *result)
{
	return nvme_submit_sync_cmd(dev->queues[0], cmd, result, ADMIN_TIMEOUT);
}

static int adapter_delete_queue(struct nvme_dev *dev, u8 opcode, u16 id)
{
	int status;
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.delete_queue.opcode = opcode;
	c.delete_queue.qid = cpu_to_le16(id);

	status = nvme_submit_admin_cmd(dev, &c, NULL);
	if (status)
		return -EIO;
	return 0;
}

static int adapter_alloc_cq(struct nvme_dev *dev, u16 qid,
						struct nvme_queue *nvmeq)
{
	int status;
	struct nvme_command c;
	int flags = NVME_QUEUE_PHYS_CONTIG | NVME_CQ_IRQ_ENABLED;

	memset(&c, 0, sizeof(c));
	c.create_cq.opcode = nvme_admin_create_cq;
	c.create_cq.prp1 = cpu_to_le64(nvmeq->cq_dma_addr);
	c.create_cq.cqid = cpu_to_le16(qid);
	c.create_cq.qsize = cpu_to_le16(nvmeq->q_depth - 1);
	c.create_cq.cq_flags = cpu_to_le16(flags);
	c.create_cq.irq_vector = cpu_to_le16(nvmeq->cq_vector);

	status = nvme_submit_admin_cmd(dev, &c, NULL);
	if (status)
		return -EIO;
	return 0;
}

static int adapter_alloc_sq(struct nvme_dev *dev, u16 qid,
						struct nvme_queue *nvmeq)
{
	int status;
	struct nvme_command c;
	int flags = NVME_QUEUE_PHYS_CONTIG | NVME_SQ_PRIO_MEDIUM;

	memset(&c, 0, sizeof(c));
	c.create_sq.opcode = nvme_admin_create_sq;
	c.create_sq.prp1 = cpu_to_le64(nvmeq->sq_dma_addr);
	c.create_sq.sqid = cpu_to_le16(qid);
	c.create_sq.qsize = cpu_to_le16(nvmeq->q_depth - 1);
	c.create_sq.sq_flags = cpu_to_le16(flags);
	c.create_sq.cqid = cpu_to_le16(qid);

	status = nvme_submit_admin_cmd(dev, &c, NULL);
	if (status)
		return -EIO;
	return 0;
}

static int adapter_delete_cq(struct nvme_dev *dev, u16 cqid)
{
	return adapter_delete_queue(dev, nvme_admin_delete_cq, cqid);
}

static int adapter_delete_sq(struct nvme_dev *dev, u16 sqid)
{
	return adapter_delete_queue(dev, nvme_admin_delete_sq, sqid);
}

int nvme_identify(struct nvme_dev *dev, unsigned nsid, unsigned cns,
							dma_addr_t dma_addr)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.identify.opcode = nvme_admin_identify;
	c.identify.nsid = cpu_to_le32(nsid);
	c.identify.prp1 = cpu_to_le64(dma_addr);
	c.identify.cns = cpu_to_le32(cns);

	return nvme_submit_admin_cmd(dev, &c, NULL);
}

int nvme_get_features(struct nvme_dev *dev, unsigned fid, unsigned nsid,
					dma_addr_t dma_addr, u32 *result)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.features.opcode = nvme_admin_get_features;
	c.features.nsid = cpu_to_le32(nsid);
	c.features.prp1 = cpu_to_le64(dma_addr);
	c.features.fid = cpu_to_le32(fid);

	return nvme_submit_admin_cmd(dev, &c, result);
}

int nvme_set_features(struct nvme_dev *dev, unsigned fid, unsigned dword11,
					dma_addr_t dma_addr, u32 *result)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.features.opcode = nvme_admin_set_features;
	c.features.prp1 = cpu_to_le64(dma_addr);
	c.features.fid = cpu_to_le32(fid);
	c.features.dword11 = cpu_to_le32(dword11);

	return nvme_submit_admin_cmd(dev, &c, result);
}

/**
 * nvme_cancel_ios - Cancel outstanding I/Os
 * @queue: The queue to cancel I/Os on
 * @timeout: True to only cancel I/Os which have timed out
 */
static void nvme_cancel_ios(struct nvme_queue *nvmeq, bool timeout)
{
	int depth = nvmeq->q_depth - 1;
	struct nvme_cmd_info *info = nvme_cmd_info(nvmeq);
	unsigned long now = jiffies;
	int cmdid;

	for_each_set_bit(cmdid, nvmeq->cmdid_data, depth) {
		void *ctx;
		nvme_completion_fn fn;
		static struct nvme_completion cqe = {
			.status = cpu_to_le16(NVME_SC_ABORT_REQ << 1),
		};

		if (timeout && !time_after(now, info[cmdid].timeout))
			continue;
		if (info[cmdid].ctx == CMD_CTX_CANCELLED)
			continue;
		dev_warn(nvmeq->q_dmadev, "Cancelling I/O %d\n", cmdid);
		ctx = cancel_cmdid(nvmeq, cmdid, &fn);
		fn(nvmeq->dev, ctx, &cqe);
	}
}

static void nvme_free_queue_mem(struct nvme_queue *nvmeq)
{
	dma_free_coherent(nvmeq->q_dmadev, CQ_SIZE(nvmeq->q_depth),
				(void *)nvmeq->cqes, nvmeq->cq_dma_addr);
	dma_free_coherent(nvmeq->q_dmadev, SQ_SIZE(nvmeq->q_depth),
					nvmeq->sq_cmds, nvmeq->sq_dma_addr);
	kfree(nvmeq);
}

static void nvme_free_queue(struct nvme_dev *dev, int qid)
{
	struct nvme_queue *nvmeq = dev->queues[qid];
	int vector = dev->entry[nvmeq->cq_vector].vector;

	spin_lock_irq(&nvmeq->q_lock);
	nvme_cancel_ios(nvmeq, false);
	while (bio_list_peek(&nvmeq->sq_cong)) {
		struct bio *bio = bio_list_pop(&nvmeq->sq_cong);
		bio_endio(bio, -EIO);
	}
	spin_unlock_irq(&nvmeq->q_lock);

	irq_set_affinity_hint(vector, NULL);
	free_irq(vector, nvmeq);

	/* Don't tell the adapter to delete the admin queue */
	if (qid) {
		adapter_delete_sq(dev, qid);
		adapter_delete_cq(dev, qid);
	}

	nvme_free_queue_mem(nvmeq);
}

static struct nvme_queue *nvme_alloc_queue(struct nvme_dev *dev, int qid,
							int depth, int vector)
{
	struct device *dmadev = &dev->pci_dev->dev;
	unsigned extra = DIV_ROUND_UP(depth, 8) + (depth *
						sizeof(struct nvme_cmd_info));
	struct nvme_queue *nvmeq = kzalloc(sizeof(*nvmeq) + extra, GFP_KERNEL);
	if (!nvmeq)
		return NULL;

	nvmeq->cqes = dma_alloc_coherent(dmadev, CQ_SIZE(depth),
					&nvmeq->cq_dma_addr, GFP_KERNEL);
	if (!nvmeq->cqes)
		goto free_nvmeq;
	memset((void *)nvmeq->cqes, 0, CQ_SIZE(depth));

	nvmeq->sq_cmds = dma_alloc_coherent(dmadev, SQ_SIZE(depth),
					&nvmeq->sq_dma_addr, GFP_KERNEL);
	if (!nvmeq->sq_cmds)
		goto free_cqdma;

	nvmeq->q_dmadev = dmadev;
	nvmeq->dev = dev;
	spin_lock_init(&nvmeq->q_lock);
	nvmeq->cq_head = 0;
	nvmeq->cq_phase = 1;
	init_waitqueue_head(&nvmeq->sq_full);
	init_waitqueue_entry(&nvmeq->sq_cong_wait, nvme_thread);
	bio_list_init(&nvmeq->sq_cong);
	nvmeq->q_db = &dev->dbs[qid << (dev->db_stride + 1)];
	nvmeq->q_depth = depth;
	nvmeq->cq_vector = vector;

	return nvmeq;

 free_cqdma:
	dma_free_coherent(dmadev, CQ_SIZE(depth), (void *)nvmeq->cqes,
							nvmeq->cq_dma_addr);
 free_nvmeq:
	kfree(nvmeq);
	return NULL;
}

static int queue_request_irq(struct nvme_dev *dev, struct nvme_queue *nvmeq,
							const char *name)
{
	if (use_threaded_interrupts)
		return request_threaded_irq(dev->entry[nvmeq->cq_vector].vector,
					nvme_irq_check, nvme_irq,
					IRQF_DISABLED | IRQF_SHARED,
					name, nvmeq);
	return request_irq(dev->entry[nvmeq->cq_vector].vector, nvme_irq,
				IRQF_DISABLED | IRQF_SHARED, name, nvmeq);
}

static struct nvme_queue *nvme_create_queue(struct nvme_dev *dev, int qid,
					    int cq_size, int vector)
{
	int result;
	struct nvme_queue *nvmeq = nvme_alloc_queue(dev, qid, cq_size, vector);

	if (!nvmeq)
		return ERR_PTR(-ENOMEM);

	result = adapter_alloc_cq(dev, qid, nvmeq);
	if (result < 0)
		goto free_nvmeq;

	result = adapter_alloc_sq(dev, qid, nvmeq);
	if (result < 0)
		goto release_cq;

	result = queue_request_irq(dev, nvmeq, "nvme");
	if (result < 0)
		goto release_sq;

	return nvmeq;

 release_sq:
	adapter_delete_sq(dev, qid);
 release_cq:
	adapter_delete_cq(dev, qid);
 free_nvmeq:
	dma_free_coherent(nvmeq->q_dmadev, CQ_SIZE(nvmeq->q_depth),
				(void *)nvmeq->cqes, nvmeq->cq_dma_addr);
	dma_free_coherent(nvmeq->q_dmadev, SQ_SIZE(nvmeq->q_depth),
					nvmeq->sq_cmds, nvmeq->sq_dma_addr);
	kfree(nvmeq);
	return ERR_PTR(result);
}

static int nvme_wait_ready(struct nvme_dev *dev, u64 cap, bool enabled)
{
	unsigned long timeout;
	u32 bit = enabled ? NVME_CSTS_RDY : 0;

	timeout = ((NVME_CAP_TIMEOUT(cap) + 1) * HZ / 2) + jiffies;

	while ((readl(&dev->bar->csts) & NVME_CSTS_RDY) != bit) {
		msleep(100);
		if (fatal_signal_pending(current))
			return -EINTR;
		if (time_after(jiffies, timeout)) {
			dev_err(&dev->pci_dev->dev,
				"Device not ready; aborting initialisation\n");
			return -ENODEV;
		}
	}

	return 0;
}

/*
 * If the device has been passed off to us in an enabled state, just clear
 * the enabled bit.  The spec says we should set the 'shutdown notification
 * bits', but doing so may cause the device to complete commands to the
 * admin queue ... and we don't know what memory that might be pointing at!
 */
static int nvme_disable_ctrl(struct nvme_dev *dev, u64 cap)
{
	u32 cc = readl(&dev->bar->cc);

	if (cc & NVME_CC_ENABLE)
		writel(cc & ~NVME_CC_ENABLE, &dev->bar->cc);
	return nvme_wait_ready(dev, cap, false);
}

static int nvme_enable_ctrl(struct nvme_dev *dev, u64 cap)
{
	return nvme_wait_ready(dev, cap, true);
}

static int nvme_configure_admin_queue(struct nvme_dev *dev)
{
	int result;
	u32 aqa;
	u64 cap = readq(&dev->bar->cap);
	struct nvme_queue *nvmeq;

	result = nvme_disable_ctrl(dev, cap);
	if (result < 0)
		return result;

	nvmeq = nvme_alloc_queue(dev, 0, 64, 0);
	if (!nvmeq)
		return -ENOMEM;

	aqa = nvmeq->q_depth - 1;
	aqa |= aqa << 16;

	dev->ctrl_config = NVME_CC_ENABLE | NVME_CC_CSS_NVM;
	dev->ctrl_config |= (PAGE_SHIFT - 12) << NVME_CC_MPS_SHIFT;
	dev->ctrl_config |= NVME_CC_ARB_RR | NVME_CC_SHN_NONE;
	dev->ctrl_config |= NVME_CC_IOSQES | NVME_CC_IOCQES;

	writel(aqa, &dev->bar->aqa);
	writeq(nvmeq->sq_dma_addr, &dev->bar->asq);
	writeq(nvmeq->cq_dma_addr, &dev->bar->acq);
	writel(dev->ctrl_config, &dev->bar->cc);

	result = nvme_enable_ctrl(dev, cap);
	if (result)
		goto free_q;

	result = queue_request_irq(dev, nvmeq, "nvme admin");
	if (result)
		goto free_q;

	dev->queues[0] = nvmeq;
	return result;

 free_q:
	nvme_free_queue_mem(nvmeq);
	return result;
}

struct nvme_iod *nvme_map_user_pages(struct nvme_dev *dev, int write,
				unsigned long addr, unsigned length)
{
	int i, err, count, nents, offset;
	struct scatterlist *sg;
	struct page **pages;
	struct nvme_iod *iod;

	if (addr & 3)
		return ERR_PTR(-EINVAL);
	if (!length || length > INT_MAX - PAGE_SIZE)
		return ERR_PTR(-EINVAL);

	offset = offset_in_page(addr);
	count = DIV_ROUND_UP(offset + length, PAGE_SIZE);
	pages = kcalloc(count, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	err = get_user_pages_fast(addr, count, 1, pages);
	if (err < count) {
		count = err;
		err = -EFAULT;
		goto put_pages;
	}

	iod = nvme_alloc_iod(count, length, GFP_KERNEL);
	sg = iod->sg;
	sg_init_table(sg, count);
	for (i = 0; i < count; i++) {
		sg_set_page(&sg[i], pages[i],
			    min_t(unsigned, length, PAGE_SIZE - offset),
			    offset);
		length -= (PAGE_SIZE - offset);
		offset = 0;
	}
	sg_mark_end(&sg[i - 1]);
	iod->nents = count;

	err = -ENOMEM;
	nents = dma_map_sg(&dev->pci_dev->dev, sg, count,
				write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (!nents)
		goto free_iod;

	kfree(pages);
	return iod;

 free_iod:
	kfree(iod);
 put_pages:
	for (i = 0; i < count; i++)
		put_page(pages[i]);
	kfree(pages);
	return ERR_PTR(err);
}

void nvme_unmap_user_pages(struct nvme_dev *dev, int write,
			struct nvme_iod *iod)
{
	int i;

	dma_unmap_sg(&dev->pci_dev->dev, iod->sg, iod->nents,
				write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

	for (i = 0; i < iod->nents; i++)
		put_page(sg_page(&iod->sg[i]));
}

static int nvme_submit_io(struct nvme_ns *ns, struct nvme_user_io __user *uio)
{
	struct nvme_dev *dev = ns->dev;
	struct nvme_queue *nvmeq;
	struct nvme_user_io io;
	struct nvme_command c;
	unsigned length, meta_len;
	int status, i;
	struct nvme_iod *iod, *meta_iod = NULL;
	dma_addr_t meta_dma_addr;
	void *meta, *uninitialized_var(meta_mem);

	if (copy_from_user(&io, uio, sizeof(io)))
		return -EFAULT;
	length = (io.nblocks + 1) << ns->lba_shift;
	meta_len = (io.nblocks + 1) * ns->ms;

	if (meta_len && ((io.metadata & 3) || !io.metadata))
		return -EINVAL;

	switch (io.opcode) {
	case nvme_cmd_write:
	case nvme_cmd_read:
	case nvme_cmd_compare:
		iod = nvme_map_user_pages(dev, io.opcode & 1, io.addr, length);
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR(iod))
		return PTR_ERR(iod);

	memset(&c, 0, sizeof(c));
	c.rw.opcode = io.opcode;
	c.rw.flags = io.flags;
	c.rw.nsid = cpu_to_le32(ns->ns_id);
	c.rw.slba = cpu_to_le64(io.slba);
	c.rw.length = cpu_to_le16(io.nblocks);
	c.rw.control = cpu_to_le16(io.control);
	c.rw.dsmgmt = cpu_to_le32(io.dsmgmt);
	c.rw.reftag = cpu_to_le32(io.reftag);
	c.rw.apptag = cpu_to_le16(io.apptag);
	c.rw.appmask = cpu_to_le16(io.appmask);

	if (meta_len) {
		meta_iod = nvme_map_user_pages(dev, io.opcode & 1, io.metadata,
								meta_len);
		if (IS_ERR(meta_iod)) {
			status = PTR_ERR(meta_iod);
			meta_iod = NULL;
			goto unmap;
		}

		meta_mem = dma_alloc_coherent(&dev->pci_dev->dev, meta_len,
						&meta_dma_addr, GFP_KERNEL);
		if (!meta_mem) {
			status = -ENOMEM;
			goto unmap;
		}

		if (io.opcode & 1) {
			int meta_offset = 0;

			for (i = 0; i < meta_iod->nents; i++) {
				meta = kmap_atomic(sg_page(&meta_iod->sg[i])) +
						meta_iod->sg[i].offset;
				memcpy(meta_mem + meta_offset, meta,
						meta_iod->sg[i].length);
				kunmap_atomic(meta);
				meta_offset += meta_iod->sg[i].length;
			}
		}

		c.rw.metadata = cpu_to_le64(meta_dma_addr);
	}

	length = nvme_setup_prps(dev, &c.common, iod, length, GFP_KERNEL);

	nvmeq = get_nvmeq(dev);
	/*
	 * Since nvme_submit_sync_cmd sleeps, we can't keep preemption
	 * disabled.  We may be preempted at any point, and be rescheduled
	 * to a different CPU.  That will cause cacheline bouncing, but no
	 * additional races since q_lock already protects against other CPUs.
	 */
	put_nvmeq(nvmeq);
	if (length != (io.nblocks + 1) << ns->lba_shift)
		status = -ENOMEM;
	else
		status = nvme_submit_sync_cmd(nvmeq, &c, NULL, NVME_IO_TIMEOUT);

	if (meta_len) {
		if (status == NVME_SC_SUCCESS && !(io.opcode & 1)) {
			int meta_offset = 0;

			for (i = 0; i < meta_iod->nents; i++) {
				meta = kmap_atomic(sg_page(&meta_iod->sg[i])) +
						meta_iod->sg[i].offset;
				memcpy(meta, meta_mem + meta_offset,
						meta_iod->sg[i].length);
				kunmap_atomic(meta);
				meta_offset += meta_iod->sg[i].length;
			}
		}

		dma_free_coherent(&dev->pci_dev->dev, meta_len, meta_mem,
								meta_dma_addr);
	}

 unmap:
	nvme_unmap_user_pages(dev, io.opcode & 1, iod);
	nvme_free_iod(dev, iod);

	if (meta_iod) {
		nvme_unmap_user_pages(dev, io.opcode & 1, meta_iod);
		nvme_free_iod(dev, meta_iod);
	}

	return status;
}

static int nvme_user_admin_cmd(struct nvme_dev *dev,
					struct nvme_admin_cmd __user *ucmd)
{
	struct nvme_admin_cmd cmd;
	struct nvme_command c;
	int status, length;
	struct nvme_iod *uninitialized_var(iod);
	unsigned timeout;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (copy_from_user(&cmd, ucmd, sizeof(cmd)))
		return -EFAULT;

	memset(&c, 0, sizeof(c));
	c.common.opcode = cmd.opcode;
	c.common.flags = cmd.flags;
	c.common.nsid = cpu_to_le32(cmd.nsid);
	c.common.cdw2[0] = cpu_to_le32(cmd.cdw2);
	c.common.cdw2[1] = cpu_to_le32(cmd.cdw3);
	c.common.cdw10[0] = cpu_to_le32(cmd.cdw10);
	c.common.cdw10[1] = cpu_to_le32(cmd.cdw11);
	c.common.cdw10[2] = cpu_to_le32(cmd.cdw12);
	c.common.cdw10[3] = cpu_to_le32(cmd.cdw13);
	c.common.cdw10[4] = cpu_to_le32(cmd.cdw14);
	c.common.cdw10[5] = cpu_to_le32(cmd.cdw15);

	length = cmd.data_len;
	if (cmd.data_len) {
		iod = nvme_map_user_pages(dev, cmd.opcode & 1, cmd.addr,
								length);
		if (IS_ERR(iod))
			return PTR_ERR(iod);
		length = nvme_setup_prps(dev, &c.common, iod, length,
								GFP_KERNEL);
	}

	timeout = cmd.timeout_ms ? msecs_to_jiffies(cmd.timeout_ms) :
								ADMIN_TIMEOUT;
	if (length != cmd.data_len)
		status = -ENOMEM;
	else
		status = nvme_submit_sync_cmd(dev->queues[0], &c, &cmd.result,
								timeout);

	if (cmd.data_len) {
		nvme_unmap_user_pages(dev, cmd.opcode & 1, iod);
		nvme_free_iod(dev, iod);
	}

	if ((status >= 0) && copy_to_user(&ucmd->result, &cmd.result,
							sizeof(cmd.result)))
		status = -EFAULT;

	return status;
}

static int nvme_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd,
							unsigned long arg)
{
	struct nvme_ns *ns = bdev->bd_disk->private_data;

	switch (cmd) {
	case NVME_IOCTL_ID:
		force_successful_syscall_return();
		return ns->ns_id;
	case NVME_IOCTL_ADMIN_CMD:
		return nvme_user_admin_cmd(ns->dev, (void __user *)arg);
	case NVME_IOCTL_SUBMIT_IO:
		return nvme_submit_io(ns, (void __user *)arg);
	case SG_GET_VERSION_NUM:
		return nvme_sg_get_version_num((void __user *)arg);
	case SG_IO:
		return nvme_sg_io(ns, (void __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct block_device_operations nvme_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= nvme_ioctl,
	.compat_ioctl	= nvme_ioctl,
};

static void nvme_resubmit_bios(struct nvme_queue *nvmeq)
{
	while (bio_list_peek(&nvmeq->sq_cong)) {
		struct bio *bio = bio_list_pop(&nvmeq->sq_cong);
		struct nvme_ns *ns = bio->bi_bdev->bd_disk->private_data;

		if (bio_list_empty(&nvmeq->sq_cong))
			remove_wait_queue(&nvmeq->sq_full,
							&nvmeq->sq_cong_wait);
		if (nvme_submit_bio_queue(nvmeq, ns, bio)) {
			if (bio_list_empty(&nvmeq->sq_cong))
				add_wait_queue(&nvmeq->sq_full,
							&nvmeq->sq_cong_wait);
			bio_list_add_head(&nvmeq->sq_cong, bio);
			break;
		}
	}
}

static int nvme_kthread(void *data)
{
	struct nvme_dev *dev;

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock(&dev_list_lock);
		list_for_each_entry(dev, &dev_list, node) {
			int i;
			for (i = 0; i < dev->queue_count; i++) {
				struct nvme_queue *nvmeq = dev->queues[i];
				if (!nvmeq)
					continue;
				spin_lock_irq(&nvmeq->q_lock);
				nvme_process_cq(nvmeq);
				nvme_cancel_ios(nvmeq, true);
				nvme_resubmit_bios(nvmeq);
				spin_unlock_irq(&nvmeq->q_lock);
			}
		}
		spin_unlock(&dev_list_lock);
		schedule_timeout(round_jiffies_relative(HZ));
	}
	return 0;
}

static DEFINE_IDA(nvme_index_ida);

static int nvme_get_ns_idx(void)
{
	int index, error;

	do {
		if (!ida_pre_get(&nvme_index_ida, GFP_KERNEL))
			return -1;

		spin_lock(&dev_list_lock);
		error = ida_get_new(&nvme_index_ida, &index);
		spin_unlock(&dev_list_lock);
	} while (error == -EAGAIN);

	if (error)
		index = -1;
	return index;
}

static void nvme_put_ns_idx(int index)
{
	spin_lock(&dev_list_lock);
	ida_remove(&nvme_index_ida, index);
	spin_unlock(&dev_list_lock);
}

static void nvme_config_discard(struct nvme_ns *ns)
{
	u32 logical_block_size = queue_logical_block_size(ns->queue);
	ns->queue->limits.discard_zeroes_data = 0;
	ns->queue->limits.discard_alignment = logical_block_size;
	ns->queue->limits.discard_granularity = logical_block_size;
	ns->queue->limits.max_discard_sectors = 0xffffffff;
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, ns->queue);
}

static struct nvme_ns *nvme_alloc_ns(struct nvme_dev *dev, unsigned nsid,
			struct nvme_id_ns *id, struct nvme_lba_range_type *rt)
{
	struct nvme_ns *ns;
	struct gendisk *disk;
	int lbaf;

	if (rt->attributes & NVME_LBART_ATTRIB_HIDE)
		return NULL;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return NULL;
	ns->queue = blk_alloc_queue(GFP_KERNEL);
	if (!ns->queue)
		goto out_free_ns;
	ns->queue->queue_flags = QUEUE_FLAG_DEFAULT;
	queue_flag_set_unlocked(QUEUE_FLAG_NOMERGES, ns->queue);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, ns->queue);
	blk_queue_make_request(ns->queue, nvme_make_request);
	ns->dev = dev;
	ns->queue->queuedata = ns;

	disk = alloc_disk(NVME_MINORS);
	if (!disk)
		goto out_free_queue;
	ns->ns_id = nsid;
	ns->disk = disk;
	lbaf = id->flbas & 0xf;
	ns->lba_shift = id->lbaf[lbaf].ds;
	ns->ms = le16_to_cpu(id->lbaf[lbaf].ms);
	blk_queue_logical_block_size(ns->queue, 1 << ns->lba_shift);
	if (dev->max_hw_sectors)
		blk_queue_max_hw_sectors(ns->queue, dev->max_hw_sectors);

	disk->major = nvme_major;
	disk->minors = NVME_MINORS;
	disk->first_minor = NVME_MINORS * nvme_get_ns_idx();
	disk->fops = &nvme_fops;
	disk->private_data = ns;
	disk->queue = ns->queue;
	disk->driverfs_dev = &dev->pci_dev->dev;
	sprintf(disk->disk_name, "nvme%dn%d", dev->instance, nsid);
	set_capacity(disk, le64_to_cpup(&id->nsze) << (ns->lba_shift - 9));

	if (dev->oncs & NVME_CTRL_ONCS_DSM)
		nvme_config_discard(ns);

	return ns;

 out_free_queue:
	blk_cleanup_queue(ns->queue);
 out_free_ns:
	kfree(ns);
	return NULL;
}

static void nvme_ns_free(struct nvme_ns *ns)
{
	int index = ns->disk->first_minor / NVME_MINORS;
	put_disk(ns->disk);
	nvme_put_ns_idx(index);
	blk_cleanup_queue(ns->queue);
	kfree(ns);
}

static int set_queue_count(struct nvme_dev *dev, int count)
{
	int status;
	u32 result;
	u32 q_count = (count - 1) | ((count - 1) << 16);

	status = nvme_set_features(dev, NVME_FEAT_NUM_QUEUES, q_count, 0,
								&result);
	if (status)
		return status < 0 ? -EIO : -EBUSY;
	return min(result & 0xffff, result >> 16) + 1;
}

static int nvme_setup_io_queues(struct nvme_dev *dev)
{
	struct pci_dev *pdev = dev->pci_dev;
	int result, cpu, i, vecs, nr_io_queues, db_bar_size, q_depth;

	nr_io_queues = num_online_cpus();
	result = set_queue_count(dev, nr_io_queues);
	if (result < 0)
		return result;
	if (result < nr_io_queues)
		nr_io_queues = result;

	/* Deregister the admin queue's interrupt */
	free_irq(dev->entry[0].vector, dev->queues[0]);

	db_bar_size = 4096 + ((nr_io_queues + 1) << (dev->db_stride + 3));
	if (db_bar_size > 8192) {
		iounmap(dev->bar);
		dev->bar = ioremap(pci_resource_start(pdev, 0), db_bar_size);
		dev->dbs = ((void __iomem *)dev->bar) + 4096;
		dev->queues[0]->q_db = dev->dbs;
	}

	vecs = nr_io_queues;
	for (i = 0; i < vecs; i++)
		dev->entry[i].entry = i;
	for (;;) {
		result = pci_enable_msix(pdev, dev->entry, vecs);
		if (result <= 0)
			break;
		vecs = result;
	}

	if (result < 0) {
		vecs = nr_io_queues;
		if (vecs > 32)
			vecs = 32;
		for (;;) {
			result = pci_enable_msi_block(pdev, vecs);
			if (result == 0) {
				for (i = 0; i < vecs; i++)
					dev->entry[i].vector = i + pdev->irq;
				break;
			} else if (result < 0) {
				vecs = 1;
				break;
			}
			vecs = result;
		}
	}

	/*
	 * Should investigate if there's a performance win from allocating
	 * more queues than interrupt vectors; it might allow the submission
	 * path to scale better, even if the receive path is limited by the
	 * number of interrupts.
	 */
	nr_io_queues = vecs;

	result = queue_request_irq(dev, dev->queues[0], "nvme admin");
	/* XXX: handle failure here */

	cpu = cpumask_first(cpu_online_mask);
	for (i = 0; i < nr_io_queues; i++) {
		irq_set_affinity_hint(dev->entry[i].vector, get_cpu_mask(cpu));
		cpu = cpumask_next(cpu, cpu_online_mask);
	}

	q_depth = min_t(int, NVME_CAP_MQES(readq(&dev->bar->cap)) + 1,
								NVME_Q_DEPTH);
	for (i = 0; i < nr_io_queues; i++) {
		dev->queues[i + 1] = nvme_create_queue(dev, i + 1, q_depth, i);
		if (IS_ERR(dev->queues[i + 1]))
			return PTR_ERR(dev->queues[i + 1]);
		dev->queue_count++;
	}

	for (; i < num_possible_cpus(); i++) {
		int target = i % rounddown_pow_of_two(dev->queue_count - 1);
		dev->queues[i + 1] = dev->queues[target + 1];
	}

	return 0;
}

static void nvme_free_queues(struct nvme_dev *dev)
{
	int i;

	for (i = dev->queue_count - 1; i >= 0; i--)
		nvme_free_queue(dev, i);
}

/*
 * Return: error value if an error occurred setting up the queues or calling
 * Identify Device.  0 if these succeeded, even if adding some of the
 * namespaces failed.  At the moment, these failures are silent.  TBD which
 * failures should be reported.
 */
static int nvme_dev_add(struct nvme_dev *dev)
{
	int res;
	unsigned nn, i;
	struct nvme_ns *ns;
	struct nvme_id_ctrl *ctrl;
	struct nvme_id_ns *id_ns;
	void *mem;
	dma_addr_t dma_addr;
	int shift = NVME_CAP_MPSMIN(readq(&dev->bar->cap)) + 12;

	res = nvme_setup_io_queues(dev);
	if (res)
		return res;

	mem = dma_alloc_coherent(&dev->pci_dev->dev, 8192, &dma_addr,
								GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	res = nvme_identify(dev, 0, 1, dma_addr);
	if (res) {
		res = -EIO;
		goto out;
	}

	ctrl = mem;
	nn = le32_to_cpup(&ctrl->nn);
	dev->oncs = le16_to_cpup(&ctrl->oncs);
	memcpy(dev->serial, ctrl->sn, sizeof(ctrl->sn));
	memcpy(dev->model, ctrl->mn, sizeof(ctrl->mn));
	memcpy(dev->firmware_rev, ctrl->fr, sizeof(ctrl->fr));
	if (ctrl->mdts)
		dev->max_hw_sectors = 1 << (ctrl->mdts + shift - 9);
	if ((dev->pci_dev->vendor == PCI_VENDOR_ID_INTEL) &&
			(dev->pci_dev->device == 0x0953) && ctrl->vs[3])
		dev->stripe_size = 1 << (ctrl->vs[3] + shift);

	id_ns = mem;
	for (i = 1; i <= nn; i++) {
		res = nvme_identify(dev, i, 0, dma_addr);
		if (res)
			continue;

		if (id_ns->ncap == 0)
			continue;

		res = nvme_get_features(dev, NVME_FEAT_LBA_RANGE, i,
							dma_addr + 4096, NULL);
		if (res)
			memset(mem + 4096, 0, 4096);

		ns = nvme_alloc_ns(dev, i, mem, mem + 4096);
		if (ns)
			list_add_tail(&ns->list, &dev->namespaces);
	}
	list_for_each_entry(ns, &dev->namespaces, list)
		add_disk(ns->disk);
	res = 0;

 out:
	dma_free_coherent(&dev->pci_dev->dev, 8192, mem, dma_addr);
	return res;
}

static int nvme_dev_map(struct nvme_dev *dev)
{
	int bars, result = -ENOMEM;
	struct pci_dev *pdev = dev->pci_dev;

	if (pci_enable_device_mem(pdev))
		return result;

	dev->entry[0].vector = pdev->irq;
	pci_set_master(pdev);
	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (pci_request_selected_regions(pdev, bars, "nvme"))
		goto disable_pci;

	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64)))
		dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	else if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32)))
		dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	else
		goto disable_pci;

	pci_set_drvdata(pdev, dev);
	dev->bar = ioremap(pci_resource_start(pdev, 0), 8192);
	if (!dev->bar)
		goto disable;

	dev->db_stride = NVME_CAP_STRIDE(readq(&dev->bar->cap));
	dev->dbs = ((void __iomem *)dev->bar) + 4096;

	return 0;

 disable:
	pci_release_regions(pdev);
 disable_pci:
	pci_disable_device(pdev);
	return result;
}

static void nvme_dev_unmap(struct nvme_dev *dev)
{
	if (dev->pci_dev->msi_enabled)
		pci_disable_msi(dev->pci_dev);
	else if (dev->pci_dev->msix_enabled)
		pci_disable_msix(dev->pci_dev);

	if (dev->bar) {
		iounmap(dev->bar);
		dev->bar = NULL;
	}

	pci_release_regions(dev->pci_dev);
	if (pci_is_enabled(dev->pci_dev))
		pci_disable_device(dev->pci_dev);
}

static int nvme_dev_remove(struct nvme_dev *dev)
{
	struct nvme_ns *ns, *next;

	spin_lock(&dev_list_lock);
	list_del(&dev->node);
	spin_unlock(&dev_list_lock);

	list_for_each_entry_safe(ns, next, &dev->namespaces, list) {
		list_del(&ns->list);
		del_gendisk(ns->disk);
		nvme_ns_free(ns);
	}

	nvme_free_queues(dev);

	return 0;
}

static int nvme_setup_prp_pools(struct nvme_dev *dev)
{
	struct device *dmadev = &dev->pci_dev->dev;
	dev->prp_page_pool = dma_pool_create("prp list page", dmadev,
						PAGE_SIZE, PAGE_SIZE, 0);
	if (!dev->prp_page_pool)
		return -ENOMEM;

	/* Optimisation for I/Os between 4k and 128k */
	dev->prp_small_pool = dma_pool_create("prp list 256", dmadev,
						256, 256, 0);
	if (!dev->prp_small_pool) {
		dma_pool_destroy(dev->prp_page_pool);
		return -ENOMEM;
	}
	return 0;
}

static void nvme_release_prp_pools(struct nvme_dev *dev)
{
	dma_pool_destroy(dev->prp_page_pool);
	dma_pool_destroy(dev->prp_small_pool);
}

static DEFINE_IDA(nvme_instance_ida);

static int nvme_set_instance(struct nvme_dev *dev)
{
	int instance, error;

	do {
		if (!ida_pre_get(&nvme_instance_ida, GFP_KERNEL))
			return -ENODEV;

		spin_lock(&dev_list_lock);
		error = ida_get_new(&nvme_instance_ida, &instance);
		spin_unlock(&dev_list_lock);
	} while (error == -EAGAIN);

	if (error)
		return -ENODEV;

	dev->instance = instance;
	return 0;
}

static void nvme_release_instance(struct nvme_dev *dev)
{
	spin_lock(&dev_list_lock);
	ida_remove(&nvme_instance_ida, dev->instance);
	spin_unlock(&dev_list_lock);
}

static void nvme_free_dev(struct kref *kref)
{
	struct nvme_dev *dev = container_of(kref, struct nvme_dev, kref);
	nvme_dev_remove(dev);
	nvme_dev_unmap(dev);
	nvme_release_instance(dev);
	nvme_release_prp_pools(dev);
	kfree(dev->queues);
	kfree(dev->entry);
	kfree(dev);
}

static int nvme_dev_open(struct inode *inode, struct file *f)
{
	struct nvme_dev *dev = container_of(f->private_data, struct nvme_dev,
								miscdev);
	kref_get(&dev->kref);
	f->private_data = dev;
	return 0;
}

static int nvme_dev_release(struct inode *inode, struct file *f)
{
	struct nvme_dev *dev = f->private_data;
	kref_put(&dev->kref, nvme_free_dev);
	return 0;
}

static long nvme_dev_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct nvme_dev *dev = f->private_data;
	switch (cmd) {
	case NVME_IOCTL_ADMIN_CMD:
		return nvme_user_admin_cmd(dev, (void __user *)arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations nvme_dev_fops = {
	.owner		= THIS_MODULE,
	.open		= nvme_dev_open,
	.release	= nvme_dev_release,
	.unlocked_ioctl	= nvme_dev_ioctl,
	.compat_ioctl	= nvme_dev_ioctl,
};

static int nvme_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int result = -ENOMEM;
	struct nvme_dev *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->entry = kcalloc(num_possible_cpus(), sizeof(*dev->entry),
								GFP_KERNEL);
	if (!dev->entry)
		goto free;
	dev->queues = kcalloc(num_possible_cpus() + 1, sizeof(void *),
								GFP_KERNEL);
	if (!dev->queues)
		goto free;

	INIT_LIST_HEAD(&dev->namespaces);
	dev->pci_dev = pdev;
	result = nvme_set_instance(dev);
	if (result)
		goto free;

	result = nvme_setup_prp_pools(dev);
	if (result)
		goto release;

	result = nvme_dev_map(dev);
	if (result)
		goto release_pools;

	result = nvme_configure_admin_queue(dev);
	if (result)
		goto unmap;
	dev->queue_count++;

	spin_lock(&dev_list_lock);
	list_add(&dev->node, &dev_list);
	spin_unlock(&dev_list_lock);

	result = nvme_dev_add(dev);
	if (result && result != -EBUSY)
		goto delete;

	scnprintf(dev->name, sizeof(dev->name), "nvme%d", dev->instance);
	dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	dev->miscdev.parent = &pdev->dev;
	dev->miscdev.name = dev->name;
	dev->miscdev.fops = &nvme_dev_fops;
	result = misc_register(&dev->miscdev);
	if (result)
		goto remove;

	kref_init(&dev->kref);
	return 0;

 remove:
	nvme_dev_remove(dev);
 delete:
	spin_lock(&dev_list_lock);
	list_del(&dev->node);
	spin_unlock(&dev_list_lock);

	nvme_free_queues(dev);
 unmap:
	nvme_dev_unmap(dev);
 release_pools:
	nvme_release_prp_pools(dev);
 release:
	nvme_release_instance(dev);
 free:
	kfree(dev->queues);
	kfree(dev->entry);
	kfree(dev);
	return result;
}

static void nvme_remove(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);
	misc_deregister(&dev->miscdev);
	kref_put(&dev->kref, nvme_free_dev);
}

/* These functions are yet to be implemented */
#define nvme_error_detected NULL
#define nvme_dump_registers NULL
#define nvme_link_reset NULL
#define nvme_slot_reset NULL
#define nvme_error_resume NULL
#define nvme_suspend NULL
#define nvme_resume NULL

static const struct pci_error_handlers nvme_err_handler = {
	.error_detected	= nvme_error_detected,
	.mmio_enabled	= nvme_dump_registers,
	.link_reset	= nvme_link_reset,
	.slot_reset	= nvme_slot_reset,
	.resume		= nvme_error_resume,
};

/* Move to pci_ids.h later */
#define PCI_CLASS_STORAGE_EXPRESS	0x010802

static DEFINE_PCI_DEVICE_TABLE(nvme_id_table) = {
	{ PCI_DEVICE_CLASS(PCI_CLASS_STORAGE_EXPRESS, 0xffffff) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, nvme_id_table);

static struct pci_driver nvme_driver = {
	.name		= "nvme",
	.id_table	= nvme_id_table,
	.probe		= nvme_probe,
	.remove		= nvme_remove,
	.suspend	= nvme_suspend,
	.resume		= nvme_resume,
	.err_handler	= &nvme_err_handler,
};

static int __init nvme_init(void)
{
	int result;

	nvme_thread = kthread_run(nvme_kthread, NULL, "nvme");
	if (IS_ERR(nvme_thread))
		return PTR_ERR(nvme_thread);

	result = register_blkdev(nvme_major, "nvme");
	if (result < 0)
		goto kill_kthread;
	else if (result > 0)
		nvme_major = result;

	result = pci_register_driver(&nvme_driver);
	if (result)
		goto unregister_blkdev;
	return 0;

 unregister_blkdev:
	unregister_blkdev(nvme_major, "nvme");
 kill_kthread:
	kthread_stop(nvme_thread);
	return result;
}

static void __exit nvme_exit(void)
{
	pci_unregister_driver(&nvme_driver);
	unregister_blkdev(nvme_major, "nvme");
	kthread_stop(nvme_thread);
}

MODULE_AUTHOR("Matthew Wilcox <willy@linux.intel.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.8");
module_init(nvme_init);
module_exit(nvme_exit);
