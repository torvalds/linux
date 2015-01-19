/*
 * Block driver for media (i.e., flash cards)
 */

#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/proc_fs.h>
#include <linux/genhd.h>
#include <linux/kthread.h>
#include <linux/cardreader/card_block.h>
#include <linux/cardreader/cardreader.h>
#include <asm/system.h>
#include <asm/uaccess.h>


static int major;
#define CARD_SHIFT	4
#define CARD_QUEUE_EXIT		(1 << 0)
#define CARD_QUEUE_SUSPENDED	(1 << 1)

#define CARD_QUEUE_BOUNCESZ	(512*256)

#define CARD_NUM_MINORS	(256 >> CARD_SHIFT)
//static unsigned long dev_use[CARD_NUM_MINORS / (8 * sizeof(unsigned long))];
static unsigned long dev_use[1];

#define CARD_INAND_START_MINOR		40
#define MAX_MTD_DEVICES 32
static int card_blk_issue_rq(struct card_queue *cq, struct request *req);
static int card_blk_probe(struct memory_card *card);
static int card_blk_prep_rq(struct card_queue *cq, struct request *req);
void card_queue_resume(struct card_queue *cq);
struct card_blk_data {
	spinlock_t lock;
	struct gendisk *disk;
	struct card_queue queue;

	unsigned int usage;
	unsigned int block_bits;
	unsigned int read_only;
};

static DEFINE_MUTEX(open_lock);

/*wait device delete*/
struct completion card_devdel_comp;

/*sdio irq flag*/
unsigned char sdio_irq_handled=0;

struct card_queue_list {
	int cq_num;
	unsigned cq_flag;
	struct card_queue *cq;
	struct card_queue_list *cq_next;
};

void card_cleanup_queue(struct card_queue *cq)
{
	struct request_queue *q = cq->queue;
	unsigned long flags;
	
	card_queue_resume(cq);

	/*should unregister reboot notifier before kthread stop*/
	unregister_reboot_notifier(&cq->nb);

	/* Then terminate our worker thread */
	kthread_stop(cq->thread);

	/* Empty the queue */   
	spin_lock_irqsave(q->queue_lock, flags);
	q->queuedata = NULL;
	blk_start_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
	
 	if (cq->bounce_sg)
 		kfree(cq->bounce_sg);
 	cq->bounce_sg = NULL;
    if (cq->sg)
	kfree(cq->sg);
	cq->sg = NULL;

	//if (cq->bounce_buf)
	//	kfree(cq->bounce_buf);
	cq->bounce_buf = NULL;

	cq->card = NULL;
}

static struct card_blk_data *card_blk_get(struct gendisk *disk)
{
	struct card_blk_data *card_data;

	mutex_lock(&open_lock);
	card_data = disk->private_data;
	if (card_data && card_data->usage == 0)
		card_data = NULL;
	if (card_data)
		card_data->usage++;
	mutex_unlock(&open_lock);

	return card_data;
}

static void card_blk_put(struct card_blk_data *card_data)
{
	mutex_lock(&open_lock);
	card_data->usage--;
	if (card_data->usage == 0) {
		put_disk(card_data->disk);
		//card_cleanup_queue(&card_data->queue);
		blk_cleanup_queue(card_data->queue.queue);
		card_data->disk->queue = NULL;
		kfree(card_data);
		complete(&card_devdel_comp);
	}
	mutex_unlock(&open_lock);
}

static int card_blk_open(struct block_device *bdev, fmode_t mode)
{
	struct card_blk_data *card_data;
	int ret = -ENXIO;

	card_data = card_blk_get(bdev->bd_disk);
	if (card_data) {
		if (card_data->usage == 2)
			check_disk_change(bdev);
		ret = 0;

             /* 
               * it would return -EROFS when FS/USB open card with O_RDWR. 
               * set sd_mmc_info->write_protected_flag in func sd_mmc_check_wp
               * set card->state |= CARD_STATE_READONLY in func sd_open
               * set card_data->read_only = 1 in func card_blk_alloc
               */
		if ((mode & FMODE_WRITE) && card_data->read_only){
                             card_blk_put(bdev->bd_disk->private_data);
			ret = -EROFS;
                   }
	}

	return ret;
}

static int card_blk_release(struct gendisk *disk, fmode_t mode)
{
	struct card_blk_data *card_data = disk->private_data;

	card_blk_put(card_data);
	return 0;
}

static int card_blk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->cylinders = get_capacity(bdev->bd_disk) / (4 * 16);
	geo->heads = 4;
	geo->sectors = 16;
	return 0;
}

static struct block_device_operations card_ops = {
	.open = card_blk_open,
	.release = card_blk_release,
	.getgeo = card_blk_getgeo,
	.owner = THIS_MODULE,
};

static inline int card_claim_card(struct memory_card *card)
{
	if(card->card_status == CARD_REMOVED)
		return -ENODEV;
	return __card_claim_host(card->host, card);
}

static int card_prep_request(struct request_queue *q, struct request *req)
{
	struct card_queue *cq = q->queuedata;
	int ret = BLKPREP_KILL;

	if (!cq) {
		//printk(KERN_ERR "[card_prep_request] %s: killing request - no device/host\n", req->rq_disk->disk_name);
		return BLKPREP_KILL;
	}
	
	if (blk_special_request(req)) {
		/*
		 * Special commands already have the command
		 * blocks already setup in req->special.
		 */
		BUG_ON(!req->special);

		ret = BLKPREP_OK;
//	} else if (blk_fs_request(req) || blk_pc_request(req)) { //3.0.8
		} else if (blk_account_rq(req) || blk_pc_request(req)) {
		/*
		 * Block I/O requests need translating according
		 * to the protocol.
		 */
		ret = cq->prep_fn(cq, req);
	} else {
		/*
		 * Everything else is invalid.
		 */
		blk_dump_rq_flags(req, "CARD bad request");
	}

	if (ret == BLKPREP_OK)
		req->cmd_flags |= REQ_DONTPREP;

	return ret;
}

static void card_request(struct request_queue *q)
{
	struct card_queue *cq = q->queuedata;
    struct request* req; 

	if (!cq) {
		while ((req = blk_fetch_request(q)) != NULL) {
			req->cmd_flags |= REQ_QUIET;
			__blk_end_request_all(req, -EIO);
		}
		return;
	}
	if (!cq->req) {
		wake_up_process(cq->thread);
	}
}

void card_queue_suspend(struct card_queue *cq)
{
	struct request_queue *q = cq->queue;
	unsigned long flags;

	if (!(cq->flags & CARD_QUEUE_SUSPENDED)) {
		cq->flags |= CARD_QUEUE_SUSPENDED;

		spin_lock_irqsave(q->queue_lock, flags);
		blk_stop_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
		down(&cq->thread_sem);	
	}
}

void card_queue_resume(struct card_queue *cq)
{
	struct request_queue *q = cq->queue;
	unsigned long flags;

	if (cq->flags & CARD_QUEUE_SUSPENDED) {
		cq->flags &= ~CARD_QUEUE_SUSPENDED;

		up(&cq->thread_sem);

		spin_lock_irqsave(q->queue_lock, flags);
		blk_start_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}

static int card_queue_thread(void *d)
{
	struct card_queue *cq = d;
	struct request_queue *q = cq->queue;
	// unsigned char rewait;
	/*
	 * Set iothread to ensure that we aren't put to sleep by
	 * the process freezing.  We handle suspension ourselves.
	 */
	current->flags |= PF_MEMALLOC;


	down(&cq->thread_sem);
	do {
		struct request *req = NULL;

		/*wait sdio handle irq & xfer data*/
    	//for(rewait=3;(!sdio_irq_handled)&&(rewait--);)
    	//	schedule();

    	spin_lock_irq(q->queue_lock);
		set_current_state(TASK_INTERRUPTIBLE);
			q = cq->queue;
				if (!blk_queue_plugged(q)) {
					req = blk_fetch_request(q);
			}
		cq->req = req;
		spin_unlock_irq(q->queue_lock);
		if (!req) {
			if (kthread_should_stop()) {
				set_current_state(TASK_RUNNING);
			break;
			}
			up(&cq->thread_sem);
			schedule();
			down(&cq->thread_sem);
			continue;
		}
              set_current_state(TASK_RUNNING);
		cq->issue_fn(cq, req);
		cond_resched();
	} while (1);
	/*Stop queue*/
	spin_lock_irq(q->queue_lock);
	queue_flag_set_unlocked(QUEUE_FLAG_STOPPED, cq->queue);
	spin_unlock_irq(q->queue_lock);
	up(&cq->thread_sem);
	cq->thread = NULL;
	return 0;
}

#define CONFIG_CARD_BLOCK_BOUNCE 1

#ifdef CONFIG_CARD_BLOCK_BOUNCE
/*
 * Prepare the sg list(s) to be handed of to the host driver
 */
static unsigned int card_queue_map_sg(struct card_queue *cq)
{
	unsigned int sg_len;
	size_t buflen;
	struct scatterlist *sg;
	int i;

	if (!cq->bounce_buf)
		return blk_rq_map_sg(cq->queue, cq->req, cq->sg);

	BUG_ON(!cq->bounce_sg);

	sg_len = blk_rq_map_sg(cq->queue, cq->req, cq->bounce_sg);

	cq->bounce_sg_len = sg_len;

	buflen = 0;
	for_each_sg(cq->bounce_sg, sg, sg_len, i)
		buflen += sg->length;

	sg_init_one(cq->sg, cq->bounce_buf, buflen);

	return 1;
}

/*
 * If writing, bounce the data to the buffer before the request
 * is sent to the host driver
 */
static void card_queue_bounce_pre(struct card_queue *cq)
{
	unsigned long flags;

	if (!cq->bounce_buf)
		return;

	if (rq_data_dir(cq->req) != WRITE)
		return;
	
	local_irq_save(flags);
		
	sg_copy_to_buffer(cq->bounce_sg, cq->bounce_sg_len,
		cq->bounce_buf, cq->sg[0].length);
	
	local_irq_restore(flags);	
}

/*
 * If reading, bounce the data from the buffer after the request
 * has been handled by the host driver
 */
static void card_queue_bounce_post(struct card_queue *cq)
{
	unsigned long flags;

	if (!cq->bounce_buf)
		return;

	if (rq_data_dir(cq->req) != READ)
		return;
	
	local_irq_save(flags);
	
	sg_copy_from_buffer(cq->bounce_sg, cq->bounce_sg_len,
		cq->bounce_buf, cq->sg[0].length);

	local_irq_restore(flags);
	
	bio_flush_dcache_pages(cq->req->bio);
}

/*
 * Alloc bounce buf for read/write numbers of pages in one request
 */
static int card_init_bounce_buf(struct card_queue *cq, 
			struct memory_card *card)
{
	int ret=0;
	struct card_host *host = card->host;
	unsigned int bouncesz;

	bouncesz = CARD_QUEUE_BOUNCESZ;

	if (bouncesz > host->max_req_size)
		bouncesz = host->max_req_size;

	if (bouncesz >= PAGE_CACHE_SIZE) {
		//cq->bounce_buf = kmalloc(bouncesz, GFP_KERNEL);
		cq->bounce_buf = host->dma_buf;
		if (!cq->bounce_buf) {
			printk(KERN_WARNING "%s: unable to "
				"allocate bounce buffer\n", card->name);
		}
	}

	if (cq->bounce_buf) {
		blk_queue_bounce_limit(cq->queue, BLK_BOUNCE_HIGH);
		blk_queue_max_hw_sectors(cq->queue, bouncesz / 512);
		blk_queue_physical_block_size(cq->queue, bouncesz);
		blk_queue_max_segments(cq->queue, bouncesz / PAGE_CACHE_SIZE);
		blk_queue_max_segment_size(cq->queue, bouncesz);

		cq->queue->queuedata = cq;
		cq->req = NULL;
	
		cq->sg = kmalloc(sizeof(struct scatterlist),
			GFP_KERNEL);
		if (!cq->sg) {
			ret = -ENOMEM;
			blk_cleanup_queue(cq->queue);
			return ret;
		}
		sg_init_table(cq->sg, 1);

		cq->bounce_sg = kmalloc(sizeof(struct scatterlist) *
			bouncesz / PAGE_CACHE_SIZE, GFP_KERNEL);
		if (!cq->bounce_sg) {
			ret = -ENOMEM;
			kfree(cq->sg);
			cq->sg = NULL;
			blk_cleanup_queue(cq->queue);
			return ret;
		}
		sg_init_table(cq->bounce_sg, bouncesz / PAGE_CACHE_SIZE);
	}

	return 0;
}

#else

static unsigned int card_queue_map_sg(struct card_queue *cq)
{
}

static void card_queue_bounce_pre(struct card_queue *cq)
{
}

static void card_queue_bounce_post(struct card_queue *cq)
{
}

static int card_init_bounce_buf(struct card_queue *cq, 
			struct memory_card *card)
{
}

#endif

static int card_reboot_notifier(struct notifier_block *nb,
                        unsigned long priority, void * arg)
{
      struct card_queue *cq = container_of(nb, struct card_queue, nb);
      struct memory_card* card = cq->card;

      printk("[card_reboot_notifier] %s\n", card->name);

      /* End thread */
      if(cq->thread)
            kthread_stop(cq->thread);

      printk("[card_reboot_notifier] out and kthread stoped \n");

      return 0;
}

int card_init_queue(struct card_queue *cq, struct memory_card *card,
		    spinlock_t * lock)
{
	struct card_host *host = card->host;
	u64 limit = BLK_BOUNCE_HIGH;
	int ret=0;

	if (host->parent->dma_mask && *host->parent->dma_mask)
		limit = *host->parent->dma_mask;

	cq->card = card;
	cq->queue = blk_init_queue(card_request, lock);
	if (!cq->queue)
		return -ENOMEM;

	blk_queue_prep_rq(cq->queue, card_prep_request);
	card_init_bounce_buf(cq, card);
	
	if(!cq->bounce_buf){
		blk_queue_bounce_limit(cq->queue, limit);
		blk_queue_max_hw_sectors(cq->queue, host->max_sectors);
		//blk_queue_max_hw_phys_segments(cq->queue, host->max_phys_segs);
		blk_queue_max_segments(cq->queue, host->max_hw_segs);
		blk_queue_max_segment_size(cq->queue, host->max_seg_size);

		cq->queue->queuedata = cq;
		cq->req = NULL;

		cq->sg = kmalloc(sizeof(struct scatterlist) * host->max_phys_segs, GFP_KERNEL);
		if (!cq->sg) {
			ret = -ENOMEM;
			blk_cleanup_queue(cq->queue);
			return ret;
		}
	}

	/*change card io scheduler from cfq to deadline*/
	cq->queue->queuedata = cq;
	elevator_exit(cq->queue->elevator);
	cq->queue->elevator = NULL;
	ret = elevator_init(cq->queue, "deadline");
	if (ret) {
             printk("[card_init_queue] elevator_init deadline fail\n");
		blk_cleanup_queue(cq->queue);
		return ret;
	}


	init_MUTEX(&cq->thread_sem);
	cq->thread = kthread_run(card_queue_thread, cq, "%s_queue", card->name);
	if (IS_ERR(cq->thread)) {
		ret = PTR_ERR(cq->thread);
		//goto free_bounce_sg;
	}

	cq->nb.notifier_call = card_reboot_notifier;
	register_reboot_notifier(&cq->nb);

	return ret;
}

static struct card_blk_data *card_blk_alloc(struct memory_card *card)
{
	struct card_blk_data *card_data;
	int devidx, ret;

	devidx = find_first_zero_bit(dev_use, CARD_NUM_MINORS);

	if(card->card_type == CARD_INAND)
		devidx = CARD_INAND_START_MINOR>>CARD_SHIFT;
	
	if (devidx >= CARD_NUM_MINORS)
		return ERR_PTR(-ENOSPC);
	__set_bit(devidx, dev_use);

	card_data = kmalloc(sizeof(struct card_blk_data), GFP_KERNEL);
	if (!card_data) {
		ret = -ENOMEM;
		return ERR_PTR(ret);
	}

	memset(card_data, 0, sizeof(struct card_blk_data));

	if(card->state & CARD_STATE_READONLY)
		card_data->read_only = 1;

	card_data->block_bits = 9;

	card_data->disk = alloc_disk(1 << CARD_SHIFT);
	if (card_data->disk == NULL) {
		ret = -ENOMEM;
		kfree(card_data);
		return ERR_PTR(ret);
	}

	spin_lock_init(&card_data->lock);
	card_data->usage = 1;

	ret = card_init_queue(&card_data->queue, card, &card_data->lock);
	if (ret) {
		put_disk(card_data->disk);
		return ERR_PTR(ret);
	}

	card_data->queue.prep_fn = card_blk_prep_rq;
	card_data->queue.issue_fn = card_blk_issue_rq;
	card_data->queue.data = card_data;

	card_data->disk->major = major;
	card_data->disk->minors = 1 << CARD_SHIFT;
	card_data->disk->first_minor = devidx << CARD_SHIFT;
	card_data->disk->fops = &card_ops;
	card_data->disk->private_data = card_data;
	card_data->disk->queue = card_data->queue.queue;
	card_data->disk->driverfs_dev = &card->dev;

	sprintf(card_data->disk->disk_name, "cardblk%s", card->name);

	blk_queue_logical_block_size(card_data->queue.queue, 1 << card_data->block_bits);

	set_capacity(card_data->disk, card->capacity);

	return card_data;
}

static int card_blk_prep_rq(struct card_queue *cq, struct request *req)
{
	struct card_blk_data *card_data = cq->data;
	int stat = BLKPREP_OK;

	WARN_ON(!cq->queue->queuedata);
	/*
	 * If we have no device, we haven't finished initialising.
	 */
	if (!card_data || !cq->card || !cq->queue->queuedata) {
		printk(KERN_ERR "%s: killing request - no device/host\n", req->rq_disk->disk_name);
		stat = BLKPREP_KILL;
	}

	return stat;
}

static int card_blk_issue_rq(struct card_queue *cq, struct request *req)
{
	struct card_blk_data *card_data = cq->data;
	struct memory_card *card = card_data->queue.card;
	struct card_blk_request brq;
	int ret;

	if (card_claim_card(card)) {
		spin_lock_irq(&card_data->lock);
		ret = 1;
		while (ret) {
			req->cmd_flags |= REQ_QUIET;
			ret = __blk_end_request(req, -EIO, (1 << card_data->block_bits));
		}
		spin_unlock_irq(&card_data->lock);
		return 0;
	}

	do {
		brq.crq.cmd = rq_data_dir(req);
		brq.crq.buf = cq->bounce_buf;
		//	brq.crq.buf = req->buffer;

		brq.card_data.lba = blk_rq_pos(req);
		brq.card_data.blk_size = 1 << card_data->block_bits;
		brq.card_data.blk_nums = blk_rq_sectors(req);

		brq.card_data.sg = cq->sg;

		brq.card_data.sg_len = card_queue_map_sg(cq);
		//brq.card_data.sg_len = blk_rq_map_sg(req->q, req, brq.card_data.sg);

		card->host->card_type = card->card_type;
		
		card_queue_bounce_pre(cq);

		card_wait_for_req(card->host, &brq);
		
		card_queue_bounce_post(cq);
			
		/*
		 *the request issue failed
		 */
		if (brq.card_data.error) {
			card_release_host(card->host);

			spin_lock_irq(&card_data->lock);
			ret = 1;
			while (ret) {
			    req->cmd_flags |= REQ_QUIET;
				ret = __blk_end_request(req, -EIO, (1 << card_data->block_bits));
			}
			spin_unlock_irq(&card_data->lock);

			/*add_disk_randomness(req->rq_disk);
			   blkdev_dequeue_request(req);
			   end_that_request_last(req, 0);
			   spin_unlock_irq(&card_data->lock); */

			return 0;
		}
		/*
		 * A block was successfully transferred.
		 */
		spin_lock_irq(&card_data->lock);
		brq.card_data.bytes_xfered = brq.card_data.blk_size * brq.card_data.blk_nums;
		ret = __blk_end_request(req, 0, brq.card_data.bytes_xfered);
		//if(!ret) 
		//{
		/*
		 * The whole request completed successfully.
		 */
		/*add_disk_randomness(req->rq_disk);
		   blkdev_dequeue_request(req);
		   end_that_request_last(req, 1);
		   } */
		spin_unlock_irq(&card_data->lock);
	} while (ret);

	card_release_host(card->host);
	//printk("card request completely %d sector num: %d communiction dir %d\n", brq.card_data.lba, brq.card_data.blk_nums, brq.crq.cmd);
	return 1;
}

static void card_blk_remove(struct memory_card *card)
{
	struct card_blk_data *card_data = card_get_drvdata(card);

	if (card_data) {
		int devidx;

		del_gendisk(card_data->disk);

		/*
		 * I think this is needed.
		 */
		//queue_flag_set_unlocked(QUEUE_FLAG_DEAD, card_data->queue.queue);
		//queue_flag_set_unlocked(QUEUE_FLAG_STOPPED, card_data->queue.queue);
		//card_data->queue.queue->queuedata = NULL;
		card_cleanup_queue(&card_data->queue);
		//card_data->disk->queue = NULL;

		devidx = card_data->disk->first_minor >> CARD_SHIFT;
		__clear_bit(devidx, dev_use);
		card_blk_put(card_data);
	}
	card_set_drvdata(card, NULL);
}

#ifdef CONFIG_PM
static int card_blk_suspend(struct memory_card *card, pm_message_t state)
{
	struct card_blk_data *card_data = card_get_drvdata(card);
	struct card_host *host = card->host;
	
	if (card_data) 
	{
		card_queue_suspend(&card_data->queue);
	}
	if(!host->sdio_task_state)
	{
		host->sdio_task_state = 1;
	}
	if(!host->card_task_state)
	{
		host->card_task_state = 1;
	}
	if(card->card_suspend)
	{
		card->card_suspend(card);
	}
	if(card->card_type == CARD_SDIO)
		return 0;
		
	card->unit_state = CARD_UNIT_RESUMED;
	return 0;
}

static int card_blk_resume(struct memory_card *card)
{
	struct card_blk_data *card_data = card_get_drvdata(card);
	struct card_host *host = card->host;
	
	if(card->card_resume)
	{
		card->card_resume(card);
	}
	if(host->card_task_state)
	{
		host->card_task_state = 0;
		if(host->card_task)
			wake_up_process(host->card_task);
	}
	if((host->sdio_task_state)&&((card->card_type == CARD_SDIO)))
	{
		host->sdio_task_state = 0;
		if(host->sdio_irq_thread)
			wake_up_process(host->sdio_irq_thread);
	}
	if (card_data) {
		//mmc_blk_set_blksize(md, card);
		card_queue_resume(&card_data->queue);
	}
	return 0;
}
#else
#define	card_blk_suspend	NULL
#define card_blk_resume		NULL
#endif

#ifdef CONFIG_PROC_FS

/*====================================================================*/
/* Support for /proc/mtd */

static struct proc_dir_entry *proc_card;
struct mtd_partition *card_table[MAX_MTD_DEVICES];

static inline int card_proc_info (char *buf, char* dev_name, int i)
{
	struct mtd_partition *this = card_table[i];

	if (!this)
		return 0;

	return sprintf(buf, "%s%d: %8.8llx %8.8x \"%s\"\n", dev_name,
		        i+1,(unsigned long long)this->size,
		       CARD_QUEUE_BOUNCESZ, this->name);
}

static int card_read_proc (char *page, char **start, off_t off, int count,
			  int *eof, void *data_unused)
{
	int len, l, i;
        off_t   begin = 0;

	len = sprintf(page, "dev:    size   erasesize  name\n");
        for (i=0; i< MAX_MTD_DEVICES; i++) {

                l = card_proc_info(page + len, "inand", i);
                len += l;
                if (len+begin > off+count)
                        goto done;
                if (len+begin < off) {
                        begin += len;
                        len = 0;
                }
        }

        *eof = 1;

done:
        if (off >= len+begin)
                return 0;
        *start = page + (off-begin);
        return ((count < begin+len-off) ? count : begin+len-off);
}

#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_INAND_LP
#define INAND_LAST_PART_MAJOR       202

/**
 * add_last_partition : add card last partition as a full device, refer to
 * board-****.c  inand_partition_info[] last partition
 * @card: inand_card_lp
 * @size: set last partition capacity
 */
int add_last_partition(struct memory_card* card, uint64_t offset ,uint64_t size)
{
      struct card_blk_data *card_data;
      int ret;

      card_data = kmalloc(sizeof(struct card_blk_data), GFP_KERNEL);
      if (!card_data) {
            ret = -ENOMEM;
            return ret;
      }

      memset(card_data, 0, sizeof(struct card_blk_data));

      if(card->state & CARD_STATE_READONLY)
            card_data->read_only = 1;

      card_data->block_bits = 9;

      card_data->disk = alloc_disk(1 << CARD_SHIFT);
      if (card_data->disk == NULL) {
            ret = -ENOMEM;
            kfree(card_data);
            return ret;
      }

      spin_lock_init(&card_data->lock);
      card_data->usage = 1;

      ret = card_init_queue(&card_data->queue, card, &card_data->lock);
      if (ret) {
            put_disk(card_data->disk);
            return ret;
      }

      card->part_offset=offset;
      card_data->queue.prep_fn = card_blk_prep_rq;
      card_data->queue.issue_fn = card_blk_issue_rq;
      card_data->queue.data = card_data;

      card_data->disk->major = INAND_LAST_PART_MAJOR;
      card_data->disk->minors = 1 << CARD_SHIFT;
      card_data->disk->first_minor = 0;
      card_data->disk->fops = &card_ops;
      card_data->disk->private_data = card_data;
      card_data->disk->queue = card_data->queue.queue;
      card_data->disk->driverfs_dev = &card->dev;

      sprintf(card_data->disk->disk_name, "cardblk%s", card->name);

      blk_queue_logical_block_size(card_data->queue.queue, 1 << card_data->block_bits);

      set_capacity(card_data->disk, size);
      card_set_drvdata(card, card_data);
      add_disk(card_data->disk);
      return 0;
}

int card_init_inand_lp(struct memory_card* card)
{
      struct aml_card_info *pinfo = card->card_plat_info;
      struct mtd_partition * part = pinfo->partitions;
      int i, err=0, nr_part = pinfo->nr_partitions;
      uint64_t offset=0, size, cur_offset=0;

      for(i=0; i<nr_part; i++)
      {
            if (part[i].size == MTDPART_SIZ_FULL)
            {
                  /*
                  add last partition as a full device for fdisk error 22,
                  and register a new card in bsp
                  */
                  if(part[i].offset == MTDPART_OFS_APPEND)
                        size = card->capacity- cur_offset;
                  else
                        size = card->capacity - part[i].offset;
                  printk("[%s] (sectors) capacity %d, offset %lld, size%lld\n",
                                    card->name, card->capacity, offset, size);
                  err = add_last_partition(card, cur_offset, size);
            }
            else{
                  offset = part[i].offset>>9;
                  size = part[i].size>>9;
                  cur_offset = offset + size;
            }
      }
      return err;
}

void card_remove_inand_lp(struct card_host* host)
{
      struct card_blk_data* card_data=card_get_drvdata(host->card);
      del_gendisk(card_data->disk);
      put_disk(card_data->disk);
      card_remove_card(host->card);
      host->card = NULL;
}
#else
int card_init_inand_lp(struct memory_card* card)
{
      return 0;
}

void card_remove_inand_lp(struct card_host* host)
{
      return;
}
#endif


/**
 * add_card_partition : add card partition , refer to 
 * board-****.c  inand_partition_info[]
 * @disk: add partitions in which disk
 * @part: partition table
 * @nr_part: partition numbers
 */
int add_card_partition(struct memory_card* card, struct gendisk * disk,
                              struct mtd_partition * part, unsigned int nr_part)
{
	unsigned int i;
	struct hd_struct * ret=NULL;
	uint64_t cur_offset=0;
	uint64_t offset, size;
	
	if(!part)
		return 0;

	for(i=0; i<nr_part; i++){
		offset = part[i].offset>>9;
		size = part[i].size>>9;
		if (part[i].offset== MTDPART_OFS_APPEND)
			offset = cur_offset;
		if (part[i].size == MTDPART_SIZ_FULL)
		{
			size = disk->part0.nr_sects - offset;
#ifdef CONFIG_INAND_LP
			printk("[%s%d] %20s  offset 0x%012llx, len 0x%012llx %s\n",
					disk->disk_name, 1+i, part[i].name, offset<<9, size<<9,
					IS_ERR(ret) ? "add fail":"");
			break;
#endif
		}
		ret = add_partition(disk, 1+i, offset, size, 0,NULL);//change by leo
		printk("[%s%d] %20s  offset 0x%012llx, len 0x%012llx %s\n",
				disk->disk_name, 1+i, part[i].name, offset<<9, size<<9,
				IS_ERR(ret) ? "add fail":"");

		//if(IS_ERR(ret)){
		//	printk("errno = %d, offset = %x, size = %x, disk->part0.nr_sects = %x\n", ret, offset, size);
		//	return ERR_PTR(ret);
		//}
		cur_offset = offset + size;
		
		card_table[i] = &part[i];
		card_table[i]->offset = offset<<9;
		card_table[i]->size = size<<9;
	}

#ifdef CONFIG_PROC_FS
	if (!proc_card && (proc_card = create_proc_entry( "inand", 0, NULL )))
		proc_card->read_proc = card_read_proc;
#endif /* CONFIG_PROC_FS */

	return 0;
}


static int card_blk_probe(struct memory_card *card)
{
	struct card_blk_data *card_data;
	struct aml_card_info *pinfo = card->card_plat_info;

	card_data = card_blk_alloc(card);
	if (IS_ERR(card_data))
		return PTR_ERR(card_data);

	card_set_drvdata(card, card_data);

	add_disk(card_data->disk);

	if (pinfo->nr_partitions > 1){
		struct disk_part_iter piter;
		struct hd_struct *part;

		disk_part_iter_init(&piter, card_data->disk, DISK_PITER_INCL_EMPTY);
		while ((part = disk_part_iter_next(&piter))){
			printk("Delete invalid mbr partition part %x, part->partno %d\n",
					part, part->partno);
			delete_partition(card_data->disk, part->partno);
		}
		disk_part_iter_exit(&piter);
	}

	add_card_partition(card, card_data->disk, pinfo->partitions,
			pinfo->nr_partitions);

	return 0;
}

static struct card_driver card_driver = {
	.drv = {
		.name = "cardblk",
		},
	.probe = card_blk_probe,
	.remove = card_blk_remove,
	.suspend = card_blk_suspend,
	.resume = card_blk_resume,
};

static int __init card_blk_init(void)
{
	int res = -ENOMEM;

	res = register_blkdev(major, "memorycard");
	if (res < 0) {
		printk(KERN_WARNING
		       "Unable to get major %d for Memory Card media: %d\n",
		       major, res);
		return res;
	}
	if (major == 0)
		major = res;
    printk(KERN_WARNING
		       "Memory Card media Major: %d\n",
		       major);
	return card_register_driver(&card_driver);
}

static void __exit card_blk_exit(void)
{
	card_unregister_driver(&card_driver);
	unregister_blkdev(major, "memorycard");
}

module_init(card_blk_init);
module_exit(card_blk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Memory Card block device driver");

module_param(major, int, 0444);
MODULE_PARM_DESC(major, "specify the major device number for Memory block driver");
