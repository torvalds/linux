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
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genhd.h>
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
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>

#define NVME_Q_DEPTH 1024
#define SQ_SIZE(depth)		(depth * sizeof(struct nvme_command))
#define CQ_SIZE(depth)		(depth * sizeof(struct nvme_completion))
#define NVME_MINORS 64
#define IO_TIMEOUT	(5 * HZ)
#define ADMIN_TIMEOUT	(60 * HZ)

static int nvme_major;
module_param(nvme_major, int, 0);

static int use_threaded_interrupts;
module_param(use_threaded_interrupts, int, 0);

static DEFINE_SPINLOCK(dev_list_lock);
static LIST_HEAD(dev_list);
static struct task_struct *nvme_thread;

/*
 * Represents an NVM Express device.  Each nvme_dev is a PCI function.
 */
struct nvme_dev {
	struct list_head node;
	struct nvme_queue **queues;
	u32 __iomem *dbs;
	struct pci_dev *pci_dev;
	struct dma_pool *prp_page_pool;
	struct dma_pool *prp_small_pool;
	int instance;
	int queue_count;
	u32 ctrl_config;
	struct msix_entry *entry;
	struct nvme_bar __iomem *bar;
	struct list_head namespaces;
	char serial[20];
	char model[40];
	char firmware_rev[8];
};

/*
 * An NVM Express namespace is equivalent to a SCSI LUN
 */
struct nvme_ns {
	struct list_head list;

	struct nvme_dev *dev;
	struct request_queue *queue;
	struct gendisk *disk;

	int ns_id;
	int lba_shift;
};

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
	u16 cq_phase;
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
	BUILD_BUG_ON(sizeof(struct nvme_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_id_ctrl) != 4096);
	BUILD_BUG_ON(sizeof(struct nvme_id_ns) != 4096);
	BUILD_BUG_ON(sizeof(struct nvme_lba_range_type) != 64);
}

struct nvme_cmd_info {
	unsigned long ctx;
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
 * @handler: The ID of the handler to call
 *
 * Allocate a Command ID for a queue.  The data passed in will
 * be passed to the completion handler.  This is implemented by using
 * the bottom two bits of the ctx pointer to store the handler ID.
 * Passing in a pointer that's not 4-byte aligned will cause a BUG.
 * We can change this if it becomes a problem.
 */
static int alloc_cmdid(struct nvme_queue *nvmeq, void *ctx, int handler,
							unsigned timeout)
{
	int depth = nvmeq->q_depth - 1;
	struct nvme_cmd_info *info = nvme_cmd_info(nvmeq);
	int cmdid;

	BUG_ON((unsigned long)ctx & 3);

	do {
		cmdid = find_first_zero_bit(nvmeq->cmdid_data, depth);
		if (cmdid >= depth)
			return -EBUSY;
	} while (test_and_set_bit(cmdid, nvmeq->cmdid_data));

	info[cmdid].ctx = (unsigned long)ctx | handler;
	info[cmdid].timeout = jiffies + timeout;
	return cmdid;
}

static int alloc_cmdid_killable(struct nvme_queue *nvmeq, void *ctx,
						int handler, unsigned timeout)
{
	int cmdid;
	wait_event_killable(nvmeq->sq_full,
		(cmdid = alloc_cmdid(nvmeq, ctx, handler, timeout)) >= 0);
	return (cmdid < 0) ? -EINTR : cmdid;
}

/*
 * If you need more than four handlers, you'll need to change how
 * alloc_cmdid and nvme_process_cq work.  Consider using a special
 * CMD_CTX value instead, if that works for your situation.
 */
enum {
	sync_completion_id = 0,
	bio_completion_id,
};

/* Special values must be a multiple of 4, and less than 0x1000 */
#define CMD_CTX_BASE		(POISON_POINTER_DELTA + sync_completion_id)
#define CMD_CTX_CANCELLED	(0x30C + CMD_CTX_BASE)
#define CMD_CTX_COMPLETED	(0x310 + CMD_CTX_BASE)
#define CMD_CTX_INVALID		(0x314 + CMD_CTX_BASE)
#define CMD_CTX_FLUSH		(0x318 + CMD_CTX_BASE)

static unsigned long free_cmdid(struct nvme_queue *nvmeq, int cmdid)
{
	unsigned long data;
	struct nvme_cmd_info *info = nvme_cmd_info(nvmeq);

	if (cmdid >= nvmeq->q_depth)
		return CMD_CTX_INVALID;
	data = info[cmdid].ctx;
	info[cmdid].ctx = CMD_CTX_COMPLETED;
	clear_bit(cmdid, nvmeq->cmdid_data);
	wake_up(&nvmeq->sq_full);
	return data;
}

static unsigned long cancel_cmdid(struct nvme_queue *nvmeq, int cmdid)
{
	unsigned long data;
	struct nvme_cmd_info *info = nvme_cmd_info(nvmeq);
	data = info[cmdid].ctx;
	info[cmdid].ctx = CMD_CTX_CANCELLED;
	return data;
}

static struct nvme_queue *get_nvmeq(struct nvme_ns *ns)
{
	return ns->dev->queues[get_cpu() + 1];
}

static void put_nvmeq(struct nvme_queue *nvmeq)
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

struct nvme_prps {
	int npages;
	dma_addr_t first_dma;
	__le64 *list[0];
};

static void nvme_free_prps(struct nvme_dev *dev, struct nvme_prps *prps)
{
	const int last_prp = PAGE_SIZE / 8 - 1;
	int i;
	dma_addr_t prp_dma;

	if (!prps)
		return;

	prp_dma = prps->first_dma;

	if (prps->npages == 0)
		dma_pool_free(dev->prp_small_pool, prps->list[0], prp_dma);
	for (i = 0; i < prps->npages; i++) {
		__le64 *prp_list = prps->list[i];
		dma_addr_t next_prp_dma = le64_to_cpu(prp_list[last_prp]);
		dma_pool_free(dev->prp_page_pool, prp_list, prp_dma);
		prp_dma = next_prp_dma;
	}
	kfree(prps);
}

struct nvme_bio {
	struct bio *bio;
	int nents;
	struct nvme_prps *prps;
	struct scatterlist sg[0];
};

/* XXX: use a mempool */
static struct nvme_bio *alloc_nbio(unsigned nseg, gfp_t gfp)
{
	return kzalloc(sizeof(struct nvme_bio) +
			sizeof(struct scatterlist) * nseg, gfp);
}

static void free_nbio(struct nvme_queue *nvmeq, struct nvme_bio *nbio)
{
	nvme_free_prps(nvmeq->dev, nbio->prps);
	kfree(nbio);
}

static void bio_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	struct nvme_bio *nbio = ctx;
	struct bio *bio = nbio->bio;
	u16 status = le16_to_cpup(&cqe->status) >> 1;

	dma_unmap_sg(nvmeq->q_dmadev, nbio->sg, nbio->nents,
			bio_data_dir(bio) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	free_nbio(nvmeq, nbio);
	if (status) {
		bio_endio(bio, -EIO);
	} else if (bio->bi_vcnt > bio->bi_idx) {
		bio_list_add(&nvmeq->sq_cong, bio);
		wake_up_process(nvme_thread);
	} else {
		bio_endio(bio, 0);
	}
}

/* length is in bytes */
static struct nvme_prps *nvme_setup_prps(struct nvme_dev *dev,
					struct nvme_common_command *cmd,
					struct scatterlist *sg, int length)
{
	struct dma_pool *pool;
	int dma_len = sg_dma_len(sg);
	u64 dma_addr = sg_dma_address(sg);
	int offset = offset_in_page(dma_addr);
	__le64 *prp_list;
	dma_addr_t prp_dma;
	int nprps, npages, i, prp_page;
	struct nvme_prps *prps = NULL;

	cmd->prp1 = cpu_to_le64(dma_addr);
	length -= (PAGE_SIZE - offset);
	if (length <= 0)
		return prps;

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
		return prps;
	}

	nprps = DIV_ROUND_UP(length, PAGE_SIZE);
	npages = DIV_ROUND_UP(8 * nprps, PAGE_SIZE);
	prps = kmalloc(sizeof(*prps) + sizeof(__le64 *) * npages, GFP_ATOMIC);
	prp_page = 0;
	if (nprps <= (256 / 8)) {
		pool = dev->prp_small_pool;
		prps->npages = 0;
	} else {
		pool = dev->prp_page_pool;
		prps->npages = npages;
	}

	prp_list = dma_pool_alloc(pool, GFP_ATOMIC, &prp_dma);
	prps->list[prp_page++] = prp_list;
	prps->first_dma = prp_dma;
	cmd->prp2 = cpu_to_le64(prp_dma);
	i = 0;
	for (;;) {
		if (i == PAGE_SIZE / 8) {
			__le64 *old_prp_list = prp_list;
			prp_list = dma_pool_alloc(pool, GFP_ATOMIC, &prp_dma);
			prps->list[prp_page++] = prp_list;
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

	return prps;
}

/* NVMe scatterlists require no holes in the virtual address */
#define BIOVEC_NOT_VIRT_MERGEABLE(vec1, vec2)	((vec2)->bv_offset || \
			(((vec1)->bv_offset + (vec1)->bv_len) % PAGE_SIZE))

static int nvme_map_bio(struct device *dev, struct nvme_bio *nbio,
		struct bio *bio, enum dma_data_direction dma_dir, int psegs)
{
	struct bio_vec *bvec, *bvprv = NULL;
	struct scatterlist *sg = NULL;
	int i, old_idx, length = 0, nsegs = 0;

	sg_init_table(nbio->sg, psegs);
	old_idx = bio->bi_idx;
	bio_for_each_segment(bvec, bio, i) {
		if (bvprv && BIOVEC_PHYS_MERGEABLE(bvprv, bvec)) {
			sg->length += bvec->bv_len;
		} else {
			if (bvprv && BIOVEC_NOT_VIRT_MERGEABLE(bvprv, bvec))
				break;
			sg = sg ? sg + 1 : nbio->sg;
			sg_set_page(sg, bvec->bv_page, bvec->bv_len,
							bvec->bv_offset);
			nsegs++;
		}
		length += bvec->bv_len;
		bvprv = bvec;
	}
	bio->bi_idx = i;
	nbio->nents = nsegs;
	sg_mark_end(sg);
	if (dma_map_sg(dev, nbio->sg, nbio->nents, dma_dir) == 0) {
		bio->bi_idx = old_idx;
		return -ENOMEM;
	}
	return length;
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

static int nvme_submit_flush_data(struct nvme_queue *nvmeq, struct nvme_ns *ns)
{
	int cmdid = alloc_cmdid(nvmeq, (void *)CMD_CTX_FLUSH,
						sync_completion_id, IO_TIMEOUT);
	if (unlikely(cmdid < 0))
		return cmdid;

	return nvme_submit_flush(nvmeq, ns, cmdid);
}

static int nvme_submit_bio_queue(struct nvme_queue *nvmeq, struct nvme_ns *ns,
								struct bio *bio)
{
	struct nvme_command *cmnd;
	struct nvme_bio *nbio;
	enum dma_data_direction dma_dir;
	int cmdid, length, result = -ENOMEM;
	u16 control;
	u32 dsmgmt;
	int psegs = bio_phys_segments(ns->queue, bio);

	if ((bio->bi_rw & REQ_FLUSH) && psegs) {
		result = nvme_submit_flush_data(nvmeq, ns);
		if (result)
			return result;
	}

	nbio = alloc_nbio(psegs, GFP_ATOMIC);
	if (!nbio)
		goto nomem;
	nbio->bio = bio;

	result = -EBUSY;
	cmdid = alloc_cmdid(nvmeq, nbio, bio_completion_id, IO_TIMEOUT);
	if (unlikely(cmdid < 0))
		goto free_nbio;

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

	result = nvme_map_bio(nvmeq->q_dmadev, nbio, bio, dma_dir, psegs);
	if (result < 0)
		goto free_nbio;
	length = result;

	cmnd->rw.command_id = cmdid;
	cmnd->rw.nsid = cpu_to_le32(ns->ns_id);
	nbio->prps = nvme_setup_prps(nvmeq->dev, &cmnd->common, nbio->sg,
								length);
	cmnd->rw.slba = cpu_to_le64(bio->bi_sector >> (ns->lba_shift - 9));
	cmnd->rw.length = cpu_to_le16((length >> ns->lba_shift) - 1);
	cmnd->rw.control = cpu_to_le16(control);
	cmnd->rw.dsmgmt = cpu_to_le32(dsmgmt);

	bio->bi_sector += length >> 9;

	if (++nvmeq->sq_tail == nvmeq->q_depth)
		nvmeq->sq_tail = 0;
	writel(nvmeq->sq_tail, nvmeq->q_db);

	return 0;

 free_nbio:
	free_nbio(nvmeq, nbio);
 nomem:
	return result;
}

/*
 * NB: return value of non-zero would mean that we were a stacking driver.
 * make_request must always succeed.
 */
static int nvme_make_request(struct request_queue *q, struct bio *bio)
{
	struct nvme_ns *ns = q->queuedata;
	struct nvme_queue *nvmeq = get_nvmeq(ns);
	int result = -EBUSY;

	spin_lock_irq(&nvmeq->q_lock);
	if (bio_list_empty(&nvmeq->sq_cong))
		result = nvme_submit_bio_queue(nvmeq, ns, bio);
	if (unlikely(result)) {
		if (bio_list_empty(&nvmeq->sq_cong))
			add_wait_queue(&nvmeq->sq_full, &nvmeq->sq_cong_wait);
		bio_list_add(&nvmeq->sq_cong, bio);
	}

	spin_unlock_irq(&nvmeq->q_lock);
	put_nvmeq(nvmeq);

	return 0;
}

struct sync_cmd_info {
	struct task_struct *task;
	u32 result;
	int status;
};

static void sync_completion(struct nvme_queue *nvmeq, void *ctx,
						struct nvme_completion *cqe)
{
	struct sync_cmd_info *cmdinfo = ctx;
	if (unlikely((unsigned long)cmdinfo == CMD_CTX_CANCELLED))
		return;
	if ((unsigned long)cmdinfo == CMD_CTX_FLUSH)
		return;
	if (unlikely((unsigned long)cmdinfo == CMD_CTX_COMPLETED)) {
		dev_warn(nvmeq->q_dmadev,
				"completed id %d twice on queue %d\n",
				cqe->command_id, le16_to_cpup(&cqe->sq_id));
		return;
	}
	if (unlikely((unsigned long)cmdinfo == CMD_CTX_INVALID)) {
		dev_warn(nvmeq->q_dmadev,
				"invalid id %d completed on queue %d\n",
				cqe->command_id, le16_to_cpup(&cqe->sq_id));
		return;
	}
	cmdinfo->result = le32_to_cpup(&cqe->result);
	cmdinfo->status = le16_to_cpup(&cqe->status) >> 1;
	wake_up_process(cmdinfo->task);
}

typedef void (*completion_fn)(struct nvme_queue *, void *,
						struct nvme_completion *);

static irqreturn_t nvme_process_cq(struct nvme_queue *nvmeq)
{
	u16 head, phase;

	static const completion_fn completions[4] = {
		[sync_completion_id] = sync_completion,
		[bio_completion_id]  = bio_completion,
	};

	head = nvmeq->cq_head;
	phase = nvmeq->cq_phase;

	for (;;) {
		unsigned long data;
		void *ptr;
		unsigned char handler;
		struct nvme_completion cqe = nvmeq->cqes[head];
		if ((le16_to_cpu(cqe.status) & 1) != phase)
			break;
		nvmeq->sq_head = le16_to_cpu(cqe.sq_head);
		if (++head == nvmeq->q_depth) {
			head = 0;
			phase = !phase;
		}

		data = free_cmdid(nvmeq, cqe.command_id);
		handler = data & 3;
		ptr = (void *)(data & ~3UL);
		completions[handler](nvmeq, ptr, &cqe);
	}

	/* If the controller ignores the cq head doorbell and continuously
	 * writes to the queue, it is theoretically possible to wrap around
	 * the queue twice and mistakenly return IRQ_NONE.  Linux only
	 * requires that 0.1% of your interrupts are handled, so this isn't
	 * a big problem.
	 */
	if (head == nvmeq->cq_head && phase == nvmeq->cq_phase)
		return IRQ_NONE;

	writel(head, nvmeq->q_db + 1);
	nvmeq->cq_head = head;
	nvmeq->cq_phase = phase;

	return IRQ_HANDLED;
}

static irqreturn_t nvme_irq(int irq, void *data)
{
	irqreturn_t result;
	struct nvme_queue *nvmeq = data;
	spin_lock(&nvmeq->q_lock);
	result = nvme_process_cq(nvmeq);
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
	cancel_cmdid(nvmeq, cmdid);
	spin_unlock_irq(&nvmeq->q_lock);
}

/*
 * Returns 0 on success.  If the result is negative, it's a Linux error code;
 * if the result is positive, it's an NVM Express status code
 */
static int nvme_submit_sync_cmd(struct nvme_queue *nvmeq,
			struct nvme_command *cmd, u32 *result, unsigned timeout)
{
	int cmdid;
	struct sync_cmd_info cmdinfo;

	cmdinfo.task = current;
	cmdinfo.status = -EINTR;

	cmdid = alloc_cmdid_killable(nvmeq, &cmdinfo, sync_completion_id,
								timeout);
	if (cmdid < 0)
		return cmdid;
	cmd->common.command_id = cmdid;

	set_current_state(TASK_KILLABLE);
	nvme_submit_cmd(nvmeq, cmd);
	schedule();

	if (cmdinfo.status == -EINTR) {
		nvme_abort_command(nvmeq, cmdid);
		return -EINTR;
	}

	if (result)
		*result = cmdinfo.result;

	return cmdinfo.status;
}

static int nvme_submit_admin_cmd(struct nvme_dev *dev, struct nvme_command *cmd,
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

static void nvme_free_queue(struct nvme_dev *dev, int qid)
{
	struct nvme_queue *nvmeq = dev->queues[qid];
	int vector = dev->entry[nvmeq->cq_vector].vector;

	irq_set_affinity_hint(vector, NULL);
	free_irq(vector, nvmeq);

	/* Don't tell the adapter to delete the admin queue */
	if (qid) {
		adapter_delete_sq(dev, qid);
		adapter_delete_cq(dev, qid);
	}

	dma_free_coherent(nvmeq->q_dmadev, CQ_SIZE(nvmeq->q_depth),
				(void *)nvmeq->cqes, nvmeq->cq_dma_addr);
	dma_free_coherent(nvmeq->q_dmadev, SQ_SIZE(nvmeq->q_depth),
					nvmeq->sq_cmds, nvmeq->sq_dma_addr);
	kfree(nvmeq);
}

static struct nvme_queue *nvme_alloc_queue(struct nvme_dev *dev, int qid,
							int depth, int vector)
{
	struct device *dmadev = &dev->pci_dev->dev;
	unsigned extra = (depth / 8) + (depth * sizeof(struct nvme_cmd_info));
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
	nvmeq->q_db = &dev->dbs[qid * 2];
	nvmeq->q_depth = depth;
	nvmeq->cq_vector = vector;

	return nvmeq;

 free_cqdma:
	dma_free_coherent(dmadev, CQ_SIZE(nvmeq->q_depth), (void *)nvmeq->cqes,
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

static __devinit struct nvme_queue *nvme_create_queue(struct nvme_dev *dev,
					int qid, int cq_size, int vector)
{
	int result;
	struct nvme_queue *nvmeq = nvme_alloc_queue(dev, qid, cq_size, vector);

	if (!nvmeq)
		return NULL;

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
	return NULL;
}

static int __devinit nvme_configure_admin_queue(struct nvme_dev *dev)
{
	int result;
	u32 aqa;
	u64 cap;
	unsigned long timeout;
	struct nvme_queue *nvmeq;

	dev->dbs = ((void __iomem *)dev->bar) + 4096;

	nvmeq = nvme_alloc_queue(dev, 0, 64, 0);
	if (!nvmeq)
		return -ENOMEM;

	aqa = nvmeq->q_depth - 1;
	aqa |= aqa << 16;

	dev->ctrl_config = NVME_CC_ENABLE | NVME_CC_CSS_NVM;
	dev->ctrl_config |= (PAGE_SHIFT - 12) << NVME_CC_MPS_SHIFT;
	dev->ctrl_config |= NVME_CC_ARB_RR | NVME_CC_SHN_NONE;
	dev->ctrl_config |= NVME_CC_IOSQES | NVME_CC_IOCQES;

	writel(0, &dev->bar->cc);
	writel(aqa, &dev->bar->aqa);
	writeq(nvmeq->sq_dma_addr, &dev->bar->asq);
	writeq(nvmeq->cq_dma_addr, &dev->bar->acq);
	writel(dev->ctrl_config, &dev->bar->cc);

	cap = readq(&dev->bar->cap);
	timeout = ((NVME_CAP_TIMEOUT(cap) + 1) * HZ / 2) + jiffies;

	while (!(readl(&dev->bar->csts) & NVME_CSTS_RDY)) {
		msleep(100);
		if (fatal_signal_pending(current))
			return -EINTR;
		if (time_after(jiffies, timeout)) {
			dev_err(&dev->pci_dev->dev,
				"Device not ready; aborting initialisation\n");
			return -ENODEV;
		}
	}

	result = queue_request_irq(dev, nvmeq, "nvme admin");
	dev->queues[0] = nvmeq;
	return result;
}

static int nvme_map_user_pages(struct nvme_dev *dev, int write,
				unsigned long addr, unsigned length,
				struct scatterlist **sgp)
{
	int i, err, count, nents, offset;
	struct scatterlist *sg;
	struct page **pages;

	if (addr & 3)
		return -EINVAL;
	if (!length)
		return -EINVAL;

	offset = offset_in_page(addr);
	count = DIV_ROUND_UP(offset + length, PAGE_SIZE);
	pages = kcalloc(count, sizeof(*pages), GFP_KERNEL);

	err = get_user_pages_fast(addr, count, 1, pages);
	if (err < count) {
		count = err;
		err = -EFAULT;
		goto put_pages;
	}

	sg = kcalloc(count, sizeof(*sg), GFP_KERNEL);
	sg_init_table(sg, count);
	sg_set_page(&sg[0], pages[0], PAGE_SIZE - offset, offset);
	length -= (PAGE_SIZE - offset);
	for (i = 1; i < count; i++) {
		sg_set_page(&sg[i], pages[i], min_t(int, length, PAGE_SIZE), 0);
		length -= PAGE_SIZE;
	}

	err = -ENOMEM;
	nents = dma_map_sg(&dev->pci_dev->dev, sg, count,
				write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (!nents)
		goto put_pages;

	kfree(pages);
	*sgp = sg;
	return nents;

 put_pages:
	for (i = 0; i < count; i++)
		put_page(pages[i]);
	kfree(pages);
	return err;
}

static void nvme_unmap_user_pages(struct nvme_dev *dev, int write,
				unsigned long addr, int length,
				struct scatterlist *sg, int nents)
{
	int i, count;

	count = DIV_ROUND_UP(offset_in_page(addr) + length, PAGE_SIZE);
	dma_unmap_sg(&dev->pci_dev->dev, sg, nents, DMA_FROM_DEVICE);

	for (i = 0; i < count; i++)
		put_page(sg_page(&sg[i]));
}

static int nvme_submit_user_admin_command(struct nvme_dev *dev,
					unsigned long addr, unsigned length,
					struct nvme_command *cmd)
{
	int err, nents;
	struct scatterlist *sg;
	struct nvme_prps *prps;

	nents = nvme_map_user_pages(dev, 0, addr, length, &sg);
	if (nents < 0)
		return nents;
	prps = nvme_setup_prps(dev, &cmd->common, sg, length);
	err = nvme_submit_admin_cmd(dev, cmd, NULL);
	nvme_unmap_user_pages(dev, 0, addr, length, sg, nents);
	nvme_free_prps(dev, prps);
	return err ? -EIO : 0;
}

static int nvme_identify(struct nvme_ns *ns, unsigned long addr, int cns)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.identify.opcode = nvme_admin_identify;
	c.identify.nsid = cns ? 0 : cpu_to_le32(ns->ns_id);
	c.identify.cns = cpu_to_le32(cns);

	return nvme_submit_user_admin_command(ns->dev, addr, 4096, &c);
}

static int nvme_get_range_type(struct nvme_ns *ns, unsigned long addr)
{
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.features.opcode = nvme_admin_get_features;
	c.features.nsid = cpu_to_le32(ns->ns_id);
	c.features.fid = cpu_to_le32(NVME_FEAT_LBA_RANGE);

	return nvme_submit_user_admin_command(ns->dev, addr, 4096, &c);
}

static int nvme_submit_io(struct nvme_ns *ns, struct nvme_user_io __user *uio)
{
	struct nvme_dev *dev = ns->dev;
	struct nvme_queue *nvmeq;
	struct nvme_user_io io;
	struct nvme_command c;
	unsigned length;
	int nents, status;
	struct scatterlist *sg;
	struct nvme_prps *prps;

	if (copy_from_user(&io, uio, sizeof(io)))
		return -EFAULT;
	length = (io.nblocks + 1) << ns->lba_shift;

	switch (io.opcode) {
	case nvme_cmd_write:
	case nvme_cmd_read:
		nents = nvme_map_user_pages(dev, io.opcode & 1, io.addr,
								length, &sg);
	default:
		return -EFAULT;
	}

	if (nents < 0)
		return nents;

	memset(&c, 0, sizeof(c));
	c.rw.opcode = io.opcode;
	c.rw.flags = io.flags;
	c.rw.nsid = cpu_to_le32(ns->ns_id);
	c.rw.slba = cpu_to_le64(io.slba);
	c.rw.length = cpu_to_le16(io.nblocks);
	c.rw.control = cpu_to_le16(io.control);
	c.rw.dsmgmt = cpu_to_le16(io.dsmgmt);
	c.rw.reftag = io.reftag;
	c.rw.apptag = io.apptag;
	c.rw.appmask = io.appmask;
	/* XXX: metadata */
	prps = nvme_setup_prps(dev, &c.common, sg, length);

	nvmeq = get_nvmeq(ns);
	/*
	 * Since nvme_submit_sync_cmd sleeps, we can't keep preemption
	 * disabled.  We may be preempted at any point, and be rescheduled
	 * to a different CPU.  That will cause cacheline bouncing, but no
	 * additional races since q_lock already protects against other CPUs.
	 */
	put_nvmeq(nvmeq);
	status = nvme_submit_sync_cmd(nvmeq, &c, NULL, IO_TIMEOUT);

	nvme_unmap_user_pages(dev, io.opcode & 1, io.addr, length, sg, nents);
	nvme_free_prps(dev, prps);
	return status;
}

static int nvme_download_firmware(struct nvme_ns *ns,
						struct nvme_dlfw __user *udlfw)
{
	struct nvme_dev *dev = ns->dev;
	struct nvme_dlfw dlfw;
	struct nvme_command c;
	int nents, status;
	struct scatterlist *sg;
	struct nvme_prps *prps;

	if (copy_from_user(&dlfw, udlfw, sizeof(dlfw)))
		return -EFAULT;
	if (dlfw.length >= (1 << 30))
		return -EINVAL;

	nents = nvme_map_user_pages(dev, 1, dlfw.addr, dlfw.length * 4, &sg);
	if (nents < 0)
		return nents;

	memset(&c, 0, sizeof(c));
	c.dlfw.opcode = nvme_admin_download_fw;
	c.dlfw.numd = cpu_to_le32(dlfw.length);
	c.dlfw.offset = cpu_to_le32(dlfw.offset);
	prps = nvme_setup_prps(dev, &c.common, sg, dlfw.length * 4);

	status = nvme_submit_admin_cmd(dev, &c, NULL);
	nvme_unmap_user_pages(dev, 0, dlfw.addr, dlfw.length * 4, sg, nents);
	nvme_free_prps(dev, prps);
	return status;
}

static int nvme_activate_firmware(struct nvme_ns *ns, unsigned long arg)
{
	struct nvme_dev *dev = ns->dev;
	struct nvme_command c;

	memset(&c, 0, sizeof(c));
	c.common.opcode = nvme_admin_activate_fw;
	c.common.rsvd10[0] = cpu_to_le32(arg);

	return nvme_submit_admin_cmd(dev, &c, NULL);
}

static int nvme_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd,
							unsigned long arg)
{
	struct nvme_ns *ns = bdev->bd_disk->private_data;

	switch (cmd) {
	case NVME_IOCTL_IDENTIFY_NS:
		return nvme_identify(ns, arg, 0);
	case NVME_IOCTL_IDENTIFY_CTRL:
		return nvme_identify(ns, arg, 1);
	case NVME_IOCTL_GET_RANGE_TYPE:
		return nvme_get_range_type(ns, arg);
	case NVME_IOCTL_SUBMIT_IO:
		return nvme_submit_io(ns, (void __user *)arg);
	case NVME_IOCTL_DOWNLOAD_FW:
		return nvme_download_firmware(ns, (void __user *)arg);
	case NVME_IOCTL_ACTIVATE_FW:
		return nvme_activate_firmware(ns, arg);
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
		if (nvme_submit_bio_queue(nvmeq, ns, bio)) {
			bio_list_add_head(&nvmeq->sq_cong, bio);
			break;
		}
		if (bio_list_empty(&nvmeq->sq_cong))
			remove_wait_queue(&nvmeq->sq_full,
							&nvmeq->sq_cong_wait);
	}
}

static int nvme_kthread(void *data)
{
	struct nvme_dev *dev;

	while (!kthread_should_stop()) {
		__set_current_state(TASK_RUNNING);
		spin_lock(&dev_list_lock);
		list_for_each_entry(dev, &dev_list, node) {
			int i;
			for (i = 0; i < dev->queue_count; i++) {
				struct nvme_queue *nvmeq = dev->queues[i];
				if (!nvmeq)
					continue;
				spin_lock_irq(&nvmeq->q_lock);
				if (nvme_process_cq(nvmeq))
					printk("process_cq did something\n");
				nvme_resubmit_bios(nvmeq);
				spin_unlock_irq(&nvmeq->q_lock);
			}
		}
		spin_unlock(&dev_list_lock);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}
	return 0;
}

static struct nvme_ns *nvme_alloc_ns(struct nvme_dev *dev, int index,
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
	ns->queue->queue_flags = QUEUE_FLAG_DEFAULT | QUEUE_FLAG_NOMERGES |
				QUEUE_FLAG_NONROT | QUEUE_FLAG_DISCARD;
	blk_queue_make_request(ns->queue, nvme_make_request);
	ns->dev = dev;
	ns->queue->queuedata = ns;

	disk = alloc_disk(NVME_MINORS);
	if (!disk)
		goto out_free_queue;
	ns->ns_id = index;
	ns->disk = disk;
	lbaf = id->flbas & 0xf;
	ns->lba_shift = id->lbaf[lbaf].ds;

	disk->major = nvme_major;
	disk->minors = NVME_MINORS;
	disk->first_minor = NVME_MINORS * index;
	disk->fops = &nvme_fops;
	disk->private_data = ns;
	disk->queue = ns->queue;
	disk->driverfs_dev = &dev->pci_dev->dev;
	sprintf(disk->disk_name, "nvme%dn%d", dev->instance, index);
	set_capacity(disk, le64_to_cpup(&id->nsze) << (ns->lba_shift - 9));

	return ns;

 out_free_queue:
	blk_cleanup_queue(ns->queue);
 out_free_ns:
	kfree(ns);
	return NULL;
}

static void nvme_ns_free(struct nvme_ns *ns)
{
	put_disk(ns->disk);
	blk_cleanup_queue(ns->queue);
	kfree(ns);
}

static int set_queue_count(struct nvme_dev *dev, int count)
{
	int status;
	u32 result;
	struct nvme_command c;
	u32 q_count = (count - 1) | ((count - 1) << 16);

	memset(&c, 0, sizeof(c));
	c.features.opcode = nvme_admin_get_features;
	c.features.fid = cpu_to_le32(NVME_FEAT_NUM_QUEUES);
	c.features.dword11 = cpu_to_le32(q_count);

	status = nvme_submit_admin_cmd(dev, &c, &result);
	if (status)
		return -EIO;
	return min(result & 0xffff, result >> 16) + 1;
}

static int __devinit nvme_setup_io_queues(struct nvme_dev *dev)
{
	int result, cpu, i, nr_io_queues;

	nr_io_queues = num_online_cpus();
	result = set_queue_count(dev, nr_io_queues);
	if (result < 0)
		return result;
	if (result < nr_io_queues)
		nr_io_queues = result;

	/* Deregister the admin queue's interrupt */
	free_irq(dev->entry[0].vector, dev->queues[0]);

	for (i = 0; i < nr_io_queues; i++)
		dev->entry[i].entry = i;
	for (;;) {
		result = pci_enable_msix(dev->pci_dev, dev->entry,
								nr_io_queues);
		if (result == 0) {
			break;
		} else if (result > 0) {
			nr_io_queues = result;
			continue;
		} else {
			nr_io_queues = 1;
			break;
		}
	}

	result = queue_request_irq(dev, dev->queues[0], "nvme admin");
	/* XXX: handle failure here */

	cpu = cpumask_first(cpu_online_mask);
	for (i = 0; i < nr_io_queues; i++) {
		irq_set_affinity_hint(dev->entry[i].vector, get_cpu_mask(cpu));
		cpu = cpumask_next(cpu, cpu_online_mask);
	}

	for (i = 0; i < nr_io_queues; i++) {
		dev->queues[i + 1] = nvme_create_queue(dev, i + 1,
							NVME_Q_DEPTH, i);
		if (!dev->queues[i + 1])
			return -ENOMEM;
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

static int __devinit nvme_dev_add(struct nvme_dev *dev)
{
	int res, nn, i;
	struct nvme_ns *ns, *next;
	struct nvme_id_ctrl *ctrl;
	void *id;
	dma_addr_t dma_addr;
	struct nvme_command cid, crt;

	res = nvme_setup_io_queues(dev);
	if (res)
		return res;

	/* XXX: Switch to a SG list once prp2 works */
	id = dma_alloc_coherent(&dev->pci_dev->dev, 8192, &dma_addr,
								GFP_KERNEL);

	memset(&cid, 0, sizeof(cid));
	cid.identify.opcode = nvme_admin_identify;
	cid.identify.nsid = 0;
	cid.identify.prp1 = cpu_to_le64(dma_addr);
	cid.identify.cns = cpu_to_le32(1);

	res = nvme_submit_admin_cmd(dev, &cid, NULL);
	if (res) {
		res = -EIO;
		goto out_free;
	}

	ctrl = id;
	nn = le32_to_cpup(&ctrl->nn);
	memcpy(dev->serial, ctrl->sn, sizeof(ctrl->sn));
	memcpy(dev->model, ctrl->mn, sizeof(ctrl->mn));
	memcpy(dev->firmware_rev, ctrl->fr, sizeof(ctrl->fr));

	cid.identify.cns = 0;
	memset(&crt, 0, sizeof(crt));
	crt.features.opcode = nvme_admin_get_features;
	crt.features.prp1 = cpu_to_le64(dma_addr + 4096);
	crt.features.fid = cpu_to_le32(NVME_FEAT_LBA_RANGE);

	for (i = 0; i <= nn; i++) {
		cid.identify.nsid = cpu_to_le32(i);
		res = nvme_submit_admin_cmd(dev, &cid, NULL);
		if (res)
			continue;

		if (((struct nvme_id_ns *)id)->ncap == 0)
			continue;

		crt.features.nsid = cpu_to_le32(i);
		res = nvme_submit_admin_cmd(dev, &crt, NULL);
		if (res)
			continue;

		ns = nvme_alloc_ns(dev, i, id, id + 4096);
		if (ns)
			list_add_tail(&ns->list, &dev->namespaces);
	}
	list_for_each_entry(ns, &dev->namespaces, list)
		add_disk(ns->disk);

	dma_free_coherent(&dev->pci_dev->dev, 4096, id, dma_addr);
	return 0;

 out_free:
	list_for_each_entry_safe(ns, next, &dev->namespaces, list) {
		list_del(&ns->list);
		nvme_ns_free(ns);
	}

	dma_free_coherent(&dev->pci_dev->dev, 4096, id, dma_addr);
	return res;
}

static int nvme_dev_remove(struct nvme_dev *dev)
{
	struct nvme_ns *ns, *next;

	spin_lock(&dev_list_lock);
	list_del(&dev->node);
	spin_unlock(&dev_list_lock);

	/* TODO: wait all I/O finished or cancel them */

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

/* XXX: Use an ida or something to let remove / add work correctly */
static void nvme_set_instance(struct nvme_dev *dev)
{
	static int instance;
	dev->instance = instance++;
}

static void nvme_release_instance(struct nvme_dev *dev)
{
}

static int __devinit nvme_probe(struct pci_dev *pdev,
						const struct pci_device_id *id)
{
	int bars, result = -ENOMEM;
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

	if (pci_enable_device_mem(pdev))
		goto free;
	pci_set_master(pdev);
	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (pci_request_selected_regions(pdev, bars, "nvme"))
		goto disable;

	INIT_LIST_HEAD(&dev->namespaces);
	dev->pci_dev = pdev;
	pci_set_drvdata(pdev, dev);
	dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	nvme_set_instance(dev);
	dev->entry[0].vector = pdev->irq;

	result = nvme_setup_prp_pools(dev);
	if (result)
		goto disable_msix;

	dev->bar = ioremap(pci_resource_start(pdev, 0), 8192);
	if (!dev->bar) {
		result = -ENOMEM;
		goto disable_msix;
	}

	result = nvme_configure_admin_queue(dev);
	if (result)
		goto unmap;
	dev->queue_count++;

	spin_lock(&dev_list_lock);
	list_add(&dev->node, &dev_list);
	spin_unlock(&dev_list_lock);

	result = nvme_dev_add(dev);
	if (result)
		goto delete;

	return 0;

 delete:
	spin_lock(&dev_list_lock);
	list_del(&dev->node);
	spin_unlock(&dev_list_lock);

	nvme_free_queues(dev);
 unmap:
	iounmap(dev->bar);
 disable_msix:
	pci_disable_msix(pdev);
	nvme_release_instance(dev);
	nvme_release_prp_pools(dev);
 disable:
	pci_disable_device(pdev);
	pci_release_regions(pdev);
 free:
	kfree(dev->queues);
	kfree(dev->entry);
	kfree(dev);
	return result;
}

static void __devexit nvme_remove(struct pci_dev *pdev)
{
	struct nvme_dev *dev = pci_get_drvdata(pdev);
	nvme_dev_remove(dev);
	pci_disable_msix(pdev);
	iounmap(dev->bar);
	nvme_release_instance(dev);
	nvme_release_prp_pools(dev);
	pci_disable_device(pdev);
	pci_release_regions(pdev);
	kfree(dev->queues);
	kfree(dev->entry);
	kfree(dev);
}

/* These functions are yet to be implemented */
#define nvme_error_detected NULL
#define nvme_dump_registers NULL
#define nvme_link_reset NULL
#define nvme_slot_reset NULL
#define nvme_error_resume NULL
#define nvme_suspend NULL
#define nvme_resume NULL

static struct pci_error_handlers nvme_err_handler = {
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
	.remove		= __devexit_p(nvme_remove),
	.suspend	= nvme_suspend,
	.resume		= nvme_resume,
	.err_handler	= &nvme_err_handler,
};

static int __init nvme_init(void)
{
	int result = -EBUSY;

	nvme_thread = kthread_run(nvme_kthread, NULL, "nvme");
	if (IS_ERR(nvme_thread))
		return PTR_ERR(nvme_thread);

	nvme_major = register_blkdev(nvme_major, "nvme");
	if (nvme_major <= 0)
		goto kill_kthread;

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
MODULE_VERSION("0.5");
module_init(nvme_init);
module_exit(nvme_exit);
