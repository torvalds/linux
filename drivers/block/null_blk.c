#include <linux/module.h>

#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/blk-mq.h>
#include <linux/hrtimer.h>
#include <linux/lightnvm.h>

struct nullb_cmd {
	struct list_head list;
	struct llist_node ll_list;
	call_single_data_t csd;
	struct request *rq;
	struct bio *bio;
	unsigned int tag;
	struct nullb_queue *nq;
	struct hrtimer timer;
};

struct nullb_queue {
	unsigned long *tag_map;
	wait_queue_head_t wait;
	unsigned int queue_depth;

	struct nullb_cmd *cmds;
};

struct nullb {
	struct list_head list;
	unsigned int index;
	struct request_queue *q;
	struct gendisk *disk;
	struct nvm_dev *ndev;
	struct blk_mq_tag_set *tag_set;
	struct blk_mq_tag_set __tag_set;
	struct hrtimer timer;
	unsigned int queue_depth;
	spinlock_t lock;

	struct nullb_queue *queues;
	unsigned int nr_queues;
	char disk_name[DISK_NAME_LEN];
};

static LIST_HEAD(nullb_list);
static struct mutex lock;
static int null_major;
static int nullb_indexes;
static struct kmem_cache *ppa_cache;
static struct blk_mq_tag_set tag_set;

enum {
	NULL_IRQ_NONE		= 0,
	NULL_IRQ_SOFTIRQ	= 1,
	NULL_IRQ_TIMER		= 2,
};

enum {
	NULL_Q_BIO		= 0,
	NULL_Q_RQ		= 1,
	NULL_Q_MQ		= 2,
};

static int submit_queues;
module_param(submit_queues, int, S_IRUGO);
MODULE_PARM_DESC(submit_queues, "Number of submission queues");

static int home_node = NUMA_NO_NODE;
module_param(home_node, int, S_IRUGO);
MODULE_PARM_DESC(home_node, "Home node for the device");

static int queue_mode = NULL_Q_MQ;

static int null_param_store_val(const char *str, int *val, int min, int max)
{
	int ret, new_val;

	ret = kstrtoint(str, 10, &new_val);
	if (ret)
		return -EINVAL;

	if (new_val < min || new_val > max)
		return -EINVAL;

	*val = new_val;
	return 0;
}

static int null_set_queue_mode(const char *str, const struct kernel_param *kp)
{
	return null_param_store_val(str, &queue_mode, NULL_Q_BIO, NULL_Q_MQ);
}

static const struct kernel_param_ops null_queue_mode_param_ops = {
	.set	= null_set_queue_mode,
	.get	= param_get_int,
};

device_param_cb(queue_mode, &null_queue_mode_param_ops, &queue_mode, S_IRUGO);
MODULE_PARM_DESC(queue_mode, "Block interface to use (0=bio,1=rq,2=multiqueue)");

static int gb = 250;
module_param(gb, int, S_IRUGO);
MODULE_PARM_DESC(gb, "Size in GB");

static int bs = 512;
module_param(bs, int, S_IRUGO);
MODULE_PARM_DESC(bs, "Block size (in bytes)");

static int nr_devices = 1;
module_param(nr_devices, int, S_IRUGO);
MODULE_PARM_DESC(nr_devices, "Number of devices to register");

static bool use_lightnvm;
module_param(use_lightnvm, bool, S_IRUGO);
MODULE_PARM_DESC(use_lightnvm, "Register as a LightNVM device");

static bool blocking;
module_param(blocking, bool, S_IRUGO);
MODULE_PARM_DESC(blocking, "Register as a blocking blk-mq driver device");

static bool shared_tags;
module_param(shared_tags, bool, S_IRUGO);
MODULE_PARM_DESC(shared_tags, "Share tag set between devices for blk-mq");

static int irqmode = NULL_IRQ_SOFTIRQ;

static int null_set_irqmode(const char *str, const struct kernel_param *kp)
{
	return null_param_store_val(str, &irqmode, NULL_IRQ_NONE,
					NULL_IRQ_TIMER);
}

static const struct kernel_param_ops null_irqmode_param_ops = {
	.set	= null_set_irqmode,
	.get	= param_get_int,
};

device_param_cb(irqmode, &null_irqmode_param_ops, &irqmode, S_IRUGO);
MODULE_PARM_DESC(irqmode, "IRQ completion handler. 0-none, 1-softirq, 2-timer");

static unsigned long completion_nsec = 10000;
module_param(completion_nsec, ulong, S_IRUGO);
MODULE_PARM_DESC(completion_nsec, "Time in ns to complete a request in hardware. Default: 10,000ns");

static int hw_queue_depth = 64;
module_param(hw_queue_depth, int, S_IRUGO);
MODULE_PARM_DESC(hw_queue_depth, "Queue depth for each hardware queue. Default: 64");

static bool use_per_node_hctx = false;
module_param(use_per_node_hctx, bool, S_IRUGO);
MODULE_PARM_DESC(use_per_node_hctx, "Use per-node allocation for hardware context queues. Default: false");

static void put_tag(struct nullb_queue *nq, unsigned int tag)
{
	clear_bit_unlock(tag, nq->tag_map);

	if (waitqueue_active(&nq->wait))
		wake_up(&nq->wait);
}

static unsigned int get_tag(struct nullb_queue *nq)
{
	unsigned int tag;

	do {
		tag = find_first_zero_bit(nq->tag_map, nq->queue_depth);
		if (tag >= nq->queue_depth)
			return -1U;
	} while (test_and_set_bit_lock(tag, nq->tag_map));

	return tag;
}

static void free_cmd(struct nullb_cmd *cmd)
{
	put_tag(cmd->nq, cmd->tag);
}

static enum hrtimer_restart null_cmd_timer_expired(struct hrtimer *timer);

static struct nullb_cmd *__alloc_cmd(struct nullb_queue *nq)
{
	struct nullb_cmd *cmd;
	unsigned int tag;

	tag = get_tag(nq);
	if (tag != -1U) {
		cmd = &nq->cmds[tag];
		cmd->tag = tag;
		cmd->nq = nq;
		if (irqmode == NULL_IRQ_TIMER) {
			hrtimer_init(&cmd->timer, CLOCK_MONOTONIC,
				     HRTIMER_MODE_REL);
			cmd->timer.function = null_cmd_timer_expired;
		}
		return cmd;
	}

	return NULL;
}

static struct nullb_cmd *alloc_cmd(struct nullb_queue *nq, int can_wait)
{
	struct nullb_cmd *cmd;
	DEFINE_WAIT(wait);

	cmd = __alloc_cmd(nq);
	if (cmd || !can_wait)
		return cmd;

	do {
		prepare_to_wait(&nq->wait, &wait, TASK_UNINTERRUPTIBLE);
		cmd = __alloc_cmd(nq);
		if (cmd)
			break;

		io_schedule();
	} while (1);

	finish_wait(&nq->wait, &wait);
	return cmd;
}

static void end_cmd(struct nullb_cmd *cmd)
{
	struct request_queue *q = NULL;

	if (cmd->rq)
		q = cmd->rq->q;

	switch (queue_mode)  {
	case NULL_Q_MQ:
		blk_mq_end_request(cmd->rq, BLK_STS_OK);
		return;
	case NULL_Q_RQ:
		INIT_LIST_HEAD(&cmd->rq->queuelist);
		blk_end_request_all(cmd->rq, BLK_STS_OK);
		break;
	case NULL_Q_BIO:
		bio_endio(cmd->bio);
		break;
	}

	free_cmd(cmd);

	/* Restart queue if needed, as we are freeing a tag */
	if (queue_mode == NULL_Q_RQ && blk_queue_stopped(q)) {
		unsigned long flags;

		spin_lock_irqsave(q->queue_lock, flags);
		blk_start_queue_async(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}

static enum hrtimer_restart null_cmd_timer_expired(struct hrtimer *timer)
{
	end_cmd(container_of(timer, struct nullb_cmd, timer));

	return HRTIMER_NORESTART;
}

static void null_cmd_end_timer(struct nullb_cmd *cmd)
{
	ktime_t kt = completion_nsec;

	hrtimer_start(&cmd->timer, kt, HRTIMER_MODE_REL);
}

static void null_softirq_done_fn(struct request *rq)
{
	if (queue_mode == NULL_Q_MQ)
		end_cmd(blk_mq_rq_to_pdu(rq));
	else
		end_cmd(rq->special);
}

static inline void null_handle_cmd(struct nullb_cmd *cmd)
{
	/* Complete IO by inline, softirq or timer */
	switch (irqmode) {
	case NULL_IRQ_SOFTIRQ:
		switch (queue_mode)  {
		case NULL_Q_MQ:
			blk_mq_complete_request(cmd->rq);
			break;
		case NULL_Q_RQ:
			blk_complete_request(cmd->rq);
			break;
		case NULL_Q_BIO:
			/*
			 * XXX: no proper submitting cpu information available.
			 */
			end_cmd(cmd);
			break;
		}
		break;
	case NULL_IRQ_NONE:
		end_cmd(cmd);
		break;
	case NULL_IRQ_TIMER:
		null_cmd_end_timer(cmd);
		break;
	}
}

static struct nullb_queue *nullb_to_queue(struct nullb *nullb)
{
	int index = 0;

	if (nullb->nr_queues != 1)
		index = raw_smp_processor_id() / ((nr_cpu_ids + nullb->nr_queues - 1) / nullb->nr_queues);

	return &nullb->queues[index];
}

static blk_qc_t null_queue_bio(struct request_queue *q, struct bio *bio)
{
	struct nullb *nullb = q->queuedata;
	struct nullb_queue *nq = nullb_to_queue(nullb);
	struct nullb_cmd *cmd;

	cmd = alloc_cmd(nq, 1);
	cmd->bio = bio;

	null_handle_cmd(cmd);
	return BLK_QC_T_NONE;
}

static int null_rq_prep_fn(struct request_queue *q, struct request *req)
{
	struct nullb *nullb = q->queuedata;
	struct nullb_queue *nq = nullb_to_queue(nullb);
	struct nullb_cmd *cmd;

	cmd = alloc_cmd(nq, 0);
	if (cmd) {
		cmd->rq = req;
		req->special = cmd;
		return BLKPREP_OK;
	}
	blk_stop_queue(q);

	return BLKPREP_DEFER;
}

static void null_request_fn(struct request_queue *q)
{
	struct request *rq;

	while ((rq = blk_fetch_request(q)) != NULL) {
		struct nullb_cmd *cmd = rq->special;

		spin_unlock_irq(q->queue_lock);
		null_handle_cmd(cmd);
		spin_lock_irq(q->queue_lock);
	}
}

static blk_status_t null_queue_rq(struct blk_mq_hw_ctx *hctx,
			 const struct blk_mq_queue_data *bd)
{
	struct nullb_cmd *cmd = blk_mq_rq_to_pdu(bd->rq);

	might_sleep_if(hctx->flags & BLK_MQ_F_BLOCKING);

	if (irqmode == NULL_IRQ_TIMER) {
		hrtimer_init(&cmd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		cmd->timer.function = null_cmd_timer_expired;
	}
	cmd->rq = bd->rq;
	cmd->nq = hctx->driver_data;

	blk_mq_start_request(bd->rq);

	null_handle_cmd(cmd);
	return BLK_STS_OK;
}

static const struct blk_mq_ops null_mq_ops = {
	.queue_rq       = null_queue_rq,
	.complete	= null_softirq_done_fn,
};

static void cleanup_queue(struct nullb_queue *nq)
{
	kfree(nq->tag_map);
	kfree(nq->cmds);
}

static void cleanup_queues(struct nullb *nullb)
{
	int i;

	for (i = 0; i < nullb->nr_queues; i++)
		cleanup_queue(&nullb->queues[i]);

	kfree(nullb->queues);
}

#ifdef CONFIG_NVM

static void null_lnvm_end_io(struct request *rq, blk_status_t status)
{
	struct nvm_rq *rqd = rq->end_io_data;

	/* XXX: lighnvm core seems to expect NVM_RSP_* values here.. */
	rqd->error = status ? -EIO : 0;
	nvm_end_io(rqd);

	blk_put_request(rq);
}

static int null_lnvm_submit_io(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	struct request_queue *q = dev->q;
	struct request *rq;
	struct bio *bio = rqd->bio;

	rq = blk_mq_alloc_request(q,
		op_is_write(bio_op(bio)) ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(rq))
		return -ENOMEM;

	blk_init_request_from_bio(rq, bio);

	rq->end_io_data = rqd;

	blk_execute_rq_nowait(q, NULL, rq, 0, null_lnvm_end_io);

	return 0;
}

static int null_lnvm_id(struct nvm_dev *dev, struct nvm_id *id)
{
	sector_t size = gb * 1024 * 1024 * 1024ULL;
	sector_t blksize;
	struct nvm_id_group *grp;

	id->ver_id = 0x1;
	id->vmnt = 0;
	id->cap = 0x2;
	id->dom = 0x1;

	id->ppaf.blk_offset = 0;
	id->ppaf.blk_len = 16;
	id->ppaf.pg_offset = 16;
	id->ppaf.pg_len = 16;
	id->ppaf.sect_offset = 32;
	id->ppaf.sect_len = 8;
	id->ppaf.pln_offset = 40;
	id->ppaf.pln_len = 8;
	id->ppaf.lun_offset = 48;
	id->ppaf.lun_len = 8;
	id->ppaf.ch_offset = 56;
	id->ppaf.ch_len = 8;

	sector_div(size, bs); /* convert size to pages */
	size >>= 8; /* concert size to pgs pr blk */
	grp = &id->grp;
	grp->mtype = 0;
	grp->fmtype = 0;
	grp->num_ch = 1;
	grp->num_pg = 256;
	blksize = size;
	size >>= 16;
	grp->num_lun = size + 1;
	sector_div(blksize, grp->num_lun);
	grp->num_blk = blksize;
	grp->num_pln = 1;

	grp->fpg_sz = bs;
	grp->csecs = bs;
	grp->trdt = 25000;
	grp->trdm = 25000;
	grp->tprt = 500000;
	grp->tprm = 500000;
	grp->tbet = 1500000;
	grp->tbem = 1500000;
	grp->mpos = 0x010101; /* single plane rwe */
	grp->cpar = hw_queue_depth;

	return 0;
}

static void *null_lnvm_create_dma_pool(struct nvm_dev *dev, char *name)
{
	mempool_t *virtmem_pool;

	virtmem_pool = mempool_create_slab_pool(64, ppa_cache);
	if (!virtmem_pool) {
		pr_err("null_blk: Unable to create virtual memory pool\n");
		return NULL;
	}

	return virtmem_pool;
}

static void null_lnvm_destroy_dma_pool(void *pool)
{
	mempool_destroy(pool);
}

static void *null_lnvm_dev_dma_alloc(struct nvm_dev *dev, void *pool,
				gfp_t mem_flags, dma_addr_t *dma_handler)
{
	return mempool_alloc(pool, mem_flags);
}

static void null_lnvm_dev_dma_free(void *pool, void *entry,
							dma_addr_t dma_handler)
{
	mempool_free(entry, pool);
}

static struct nvm_dev_ops null_lnvm_dev_ops = {
	.identity		= null_lnvm_id,
	.submit_io		= null_lnvm_submit_io,

	.create_dma_pool	= null_lnvm_create_dma_pool,
	.destroy_dma_pool	= null_lnvm_destroy_dma_pool,
	.dev_dma_alloc		= null_lnvm_dev_dma_alloc,
	.dev_dma_free		= null_lnvm_dev_dma_free,

	/* Simulate nvme protocol restriction */
	.max_phys_sect		= 64,
};

static int null_nvm_register(struct nullb *nullb)
{
	struct nvm_dev *dev;
	int rv;

	dev = nvm_alloc_dev(0);
	if (!dev)
		return -ENOMEM;

	dev->q = nullb->q;
	memcpy(dev->name, nullb->disk_name, DISK_NAME_LEN);
	dev->ops = &null_lnvm_dev_ops;

	rv = nvm_register(dev);
	if (rv) {
		kfree(dev);
		return rv;
	}
	nullb->ndev = dev;
	return 0;
}

static void null_nvm_unregister(struct nullb *nullb)
{
	nvm_unregister(nullb->ndev);
}
#else
static int null_nvm_register(struct nullb *nullb)
{
	pr_err("null_blk: CONFIG_NVM needs to be enabled for LightNVM\n");
	return -EINVAL;
}
static void null_nvm_unregister(struct nullb *nullb) {}
#endif /* CONFIG_NVM */

static void null_del_dev(struct nullb *nullb)
{
	list_del_init(&nullb->list);

	if (use_lightnvm)
		null_nvm_unregister(nullb);
	else
		del_gendisk(nullb->disk);
	blk_cleanup_queue(nullb->q);
	if (queue_mode == NULL_Q_MQ && nullb->tag_set == &nullb->__tag_set)
		blk_mq_free_tag_set(nullb->tag_set);
	if (!use_lightnvm)
		put_disk(nullb->disk);
	cleanup_queues(nullb);
	kfree(nullb);
}

static int null_open(struct block_device *bdev, fmode_t mode)
{
	return 0;
}

static void null_release(struct gendisk *disk, fmode_t mode)
{
}

static const struct block_device_operations null_fops = {
	.owner =	THIS_MODULE,
	.open =		null_open,
	.release =	null_release,
};

static void null_init_queue(struct nullb *nullb, struct nullb_queue *nq)
{
	BUG_ON(!nullb);
	BUG_ON(!nq);

	init_waitqueue_head(&nq->wait);
	nq->queue_depth = nullb->queue_depth;
}

static void null_init_queues(struct nullb *nullb)
{
	struct request_queue *q = nullb->q;
	struct blk_mq_hw_ctx *hctx;
	struct nullb_queue *nq;
	int i;

	queue_for_each_hw_ctx(q, hctx, i) {
		if (!hctx->nr_ctx || !hctx->tags)
			continue;
		nq = &nullb->queues[i];
		hctx->driver_data = nq;
		null_init_queue(nullb, nq);
		nullb->nr_queues++;
	}
}

static int setup_commands(struct nullb_queue *nq)
{
	struct nullb_cmd *cmd;
	int i, tag_size;

	nq->cmds = kzalloc(nq->queue_depth * sizeof(*cmd), GFP_KERNEL);
	if (!nq->cmds)
		return -ENOMEM;

	tag_size = ALIGN(nq->queue_depth, BITS_PER_LONG) / BITS_PER_LONG;
	nq->tag_map = kzalloc(tag_size * sizeof(unsigned long), GFP_KERNEL);
	if (!nq->tag_map) {
		kfree(nq->cmds);
		return -ENOMEM;
	}

	for (i = 0; i < nq->queue_depth; i++) {
		cmd = &nq->cmds[i];
		INIT_LIST_HEAD(&cmd->list);
		cmd->ll_list.next = NULL;
		cmd->tag = -1U;
	}

	return 0;
}

static int setup_queues(struct nullb *nullb)
{
	nullb->queues = kzalloc(submit_queues * sizeof(struct nullb_queue),
								GFP_KERNEL);
	if (!nullb->queues)
		return -ENOMEM;

	nullb->nr_queues = 0;
	nullb->queue_depth = hw_queue_depth;

	return 0;
}

static int init_driver_queues(struct nullb *nullb)
{
	struct nullb_queue *nq;
	int i, ret = 0;

	for (i = 0; i < submit_queues; i++) {
		nq = &nullb->queues[i];

		null_init_queue(nullb, nq);

		ret = setup_commands(nq);
		if (ret)
			return ret;
		nullb->nr_queues++;
	}
	return 0;
}

static int null_gendisk_register(struct nullb *nullb)
{
	struct gendisk *disk;
	sector_t size;

	disk = nullb->disk = alloc_disk_node(1, home_node);
	if (!disk)
		return -ENOMEM;
	size = gb * 1024 * 1024 * 1024ULL;
	set_capacity(disk, size >> 9);

	disk->flags |= GENHD_FL_EXT_DEVT | GENHD_FL_SUPPRESS_PARTITION_INFO;
	disk->major		= null_major;
	disk->first_minor	= nullb->index;
	disk->fops		= &null_fops;
	disk->private_data	= nullb;
	disk->queue		= nullb->q;
	strncpy(disk->disk_name, nullb->disk_name, DISK_NAME_LEN);

	add_disk(disk);
	return 0;
}

static int null_init_tag_set(struct blk_mq_tag_set *set)
{
	set->ops = &null_mq_ops;
	set->nr_hw_queues = submit_queues;
	set->queue_depth = hw_queue_depth;
	set->numa_node = home_node;
	set->cmd_size	= sizeof(struct nullb_cmd);
	set->flags = BLK_MQ_F_SHOULD_MERGE;
	set->driver_data = NULL;

	if (blocking)
		set->flags |= BLK_MQ_F_BLOCKING;

	return blk_mq_alloc_tag_set(set);
}

static int null_add_dev(void)
{
	struct nullb *nullb;
	int rv;

	nullb = kzalloc_node(sizeof(*nullb), GFP_KERNEL, home_node);
	if (!nullb) {
		rv = -ENOMEM;
		goto out;
	}

	spin_lock_init(&nullb->lock);

	if (queue_mode == NULL_Q_MQ && use_per_node_hctx)
		submit_queues = nr_online_nodes;

	rv = setup_queues(nullb);
	if (rv)
		goto out_free_nullb;

	if (queue_mode == NULL_Q_MQ) {
		if (shared_tags) {
			nullb->tag_set = &tag_set;
			rv = 0;
		} else {
			nullb->tag_set = &nullb->__tag_set;
			rv = null_init_tag_set(nullb->tag_set);
		}

		if (rv)
			goto out_cleanup_queues;

		nullb->q = blk_mq_init_queue(nullb->tag_set);
		if (IS_ERR(nullb->q)) {
			rv = -ENOMEM;
			goto out_cleanup_tags;
		}
		null_init_queues(nullb);
	} else if (queue_mode == NULL_Q_BIO) {
		nullb->q = blk_alloc_queue_node(GFP_KERNEL, home_node);
		if (!nullb->q) {
			rv = -ENOMEM;
			goto out_cleanup_queues;
		}
		blk_queue_make_request(nullb->q, null_queue_bio);
		rv = init_driver_queues(nullb);
		if (rv)
			goto out_cleanup_blk_queue;
	} else {
		nullb->q = blk_init_queue_node(null_request_fn, &nullb->lock, home_node);
		if (!nullb->q) {
			rv = -ENOMEM;
			goto out_cleanup_queues;
		}
		blk_queue_prep_rq(nullb->q, null_rq_prep_fn);
		blk_queue_softirq_done(nullb->q, null_softirq_done_fn);
		rv = init_driver_queues(nullb);
		if (rv)
			goto out_cleanup_blk_queue;
	}

	nullb->q->queuedata = nullb;
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, nullb->q);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, nullb->q);

	mutex_lock(&lock);
	nullb->index = nullb_indexes++;
	mutex_unlock(&lock);

	blk_queue_logical_block_size(nullb->q, bs);
	blk_queue_physical_block_size(nullb->q, bs);

	sprintf(nullb->disk_name, "nullb%d", nullb->index);

	if (use_lightnvm)
		rv = null_nvm_register(nullb);
	else
		rv = null_gendisk_register(nullb);

	if (rv)
		goto out_cleanup_blk_queue;

	mutex_lock(&lock);
	list_add_tail(&nullb->list, &nullb_list);
	mutex_unlock(&lock);

	return 0;
out_cleanup_blk_queue:
	blk_cleanup_queue(nullb->q);
out_cleanup_tags:
	if (queue_mode == NULL_Q_MQ && nullb->tag_set == &nullb->__tag_set)
		blk_mq_free_tag_set(nullb->tag_set);
out_cleanup_queues:
	cleanup_queues(nullb);
out_free_nullb:
	kfree(nullb);
out:
	return rv;
}

static int __init null_init(void)
{
	int ret = 0;
	unsigned int i;
	struct nullb *nullb;

	if (bs > PAGE_SIZE) {
		pr_warn("null_blk: invalid block size\n");
		pr_warn("null_blk: defaults block size to %lu\n", PAGE_SIZE);
		bs = PAGE_SIZE;
	}

	if (use_lightnvm && bs != 4096) {
		pr_warn("null_blk: LightNVM only supports 4k block size\n");
		pr_warn("null_blk: defaults block size to 4k\n");
		bs = 4096;
	}

	if (use_lightnvm && queue_mode != NULL_Q_MQ) {
		pr_warn("null_blk: LightNVM only supported for blk-mq\n");
		pr_warn("null_blk: defaults queue mode to blk-mq\n");
		queue_mode = NULL_Q_MQ;
	}

	if (queue_mode == NULL_Q_MQ && use_per_node_hctx) {
		if (submit_queues < nr_online_nodes) {
			pr_warn("null_blk: submit_queues param is set to %u.",
							nr_online_nodes);
			submit_queues = nr_online_nodes;
		}
	} else if (submit_queues > nr_cpu_ids)
		submit_queues = nr_cpu_ids;
	else if (!submit_queues)
		submit_queues = 1;

	if (queue_mode == NULL_Q_MQ && shared_tags) {
		ret = null_init_tag_set(&tag_set);
		if (ret)
			return ret;
	}

	mutex_init(&lock);

	null_major = register_blkdev(0, "nullb");
	if (null_major < 0) {
		ret = null_major;
		goto err_tagset;
	}

	if (use_lightnvm) {
		ppa_cache = kmem_cache_create("ppa_cache", 64 * sizeof(u64),
								0, 0, NULL);
		if (!ppa_cache) {
			pr_err("null_blk: unable to create ppa cache\n");
			ret = -ENOMEM;
			goto err_ppa;
		}
	}

	for (i = 0; i < nr_devices; i++) {
		ret = null_add_dev();
		if (ret)
			goto err_dev;
	}

	pr_info("null: module loaded\n");
	return 0;

err_dev:
	while (!list_empty(&nullb_list)) {
		nullb = list_entry(nullb_list.next, struct nullb, list);
		null_del_dev(nullb);
	}
	kmem_cache_destroy(ppa_cache);
err_ppa:
	unregister_blkdev(null_major, "nullb");
err_tagset:
	if (queue_mode == NULL_Q_MQ && shared_tags)
		blk_mq_free_tag_set(&tag_set);
	return ret;
}

static void __exit null_exit(void)
{
	struct nullb *nullb;

	unregister_blkdev(null_major, "nullb");

	mutex_lock(&lock);
	while (!list_empty(&nullb_list)) {
		nullb = list_entry(nullb_list.next, struct nullb, list);
		null_del_dev(nullb);
	}
	mutex_unlock(&lock);

	if (queue_mode == NULL_Q_MQ && shared_tags)
		blk_mq_free_tag_set(&tag_set);

	kmem_cache_destroy(ppa_cache);
}

module_init(null_init);
module_exit(null_exit);

MODULE_AUTHOR("Jens Axboe <jaxboe@fusionio.com>");
MODULE_LICENSE("GPL");
