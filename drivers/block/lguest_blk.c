/* A simple block driver for lguest.
 *
 * Copyright 2006 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
//#define DEBUG
#include <linux/init.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/lguest_bus.h>

static char next_block_index = 'a';

struct blockdev
{
	spinlock_t lock;

	/* The disk structure for the kernel. */
	struct gendisk *disk;

	/* The major number for this disk. */
	int major;
	int irq;

	unsigned long phys_addr;
	/* The mapped block page. */
	struct lguest_block_page *lb_page;

	/* We only have a single request outstanding at a time. */
	struct lguest_dma dma;
	struct request *req;
};

/* Jens gave me this nice helper to end all chunks of a request. */
static void end_entire_request(struct request *req, int uptodate)
{
	if (end_that_request_first(req, uptodate, req->hard_nr_sectors))
		BUG();
	add_disk_randomness(req->rq_disk);
	blkdev_dequeue_request(req);
	end_that_request_last(req, uptodate);
}

static irqreturn_t lgb_irq(int irq, void *_bd)
{
	struct blockdev *bd = _bd;
	unsigned long flags;

	if (!bd->req) {
		pr_debug("No work!\n");
		return IRQ_NONE;
	}

	if (!bd->lb_page->result) {
		pr_debug("No result!\n");
		return IRQ_NONE;
	}

	spin_lock_irqsave(&bd->lock, flags);
	end_entire_request(bd->req, bd->lb_page->result == 1);
	bd->req = NULL;
	bd->dma.used_len = 0;
	blk_start_queue(bd->disk->queue);
	spin_unlock_irqrestore(&bd->lock, flags);
	return IRQ_HANDLED;
}

static unsigned int req_to_dma(struct request *req, struct lguest_dma *dma)
{
	unsigned int i = 0, idx, len = 0;
	struct bio *bio;

	rq_for_each_bio(bio, req) {
		struct bio_vec *bvec;
		bio_for_each_segment(bvec, bio, idx) {
			BUG_ON(i == LGUEST_MAX_DMA_SECTIONS);
			BUG_ON(!bvec->bv_len);
			dma->addr[i] = page_to_phys(bvec->bv_page)
				+ bvec->bv_offset;
			dma->len[i] = bvec->bv_len;
			len += bvec->bv_len;
			i++;
		}
	}
	if (i < LGUEST_MAX_DMA_SECTIONS)
		dma->len[i] = 0;
	return len;
}

static void empty_dma(struct lguest_dma *dma)
{
	dma->len[0] = 0;
}

static void setup_req(struct blockdev *bd,
		      int type, struct request *req, struct lguest_dma *dma)
{
	bd->lb_page->type = type;
	bd->lb_page->sector = req->sector;
	bd->lb_page->result = 0;
	bd->req = req;
	bd->lb_page->bytes = req_to_dma(req, dma);
}

static void do_write(struct blockdev *bd, struct request *req)
{
	struct lguest_dma send;

	pr_debug("lgb: WRITE sector %li\n", (long)req->sector);
	setup_req(bd, 1, req, &send);

	lguest_send_dma(bd->phys_addr, &send);
}

static void do_read(struct blockdev *bd, struct request *req)
{
	struct lguest_dma ping;

	pr_debug("lgb: READ sector %li\n", (long)req->sector);
	setup_req(bd, 0, req, &bd->dma);

	empty_dma(&ping);
	lguest_send_dma(bd->phys_addr, &ping);
}

static void do_lgb_request(request_queue_t *q)
{
	struct blockdev *bd;
	struct request *req;

again:
	req = elv_next_request(q);
	if (!req)
		return;

	bd = req->rq_disk->private_data;
	/* Sometimes we get repeated requests after blk_stop_queue. */
	if (bd->req)
		return;

	if (!blk_fs_request(req)) {
		pr_debug("Got non-command 0x%08x\n", req->cmd_type);
		req->errors++;
		end_entire_request(req, 0);
		goto again;
	}

	if (rq_data_dir(req) == WRITE)
		do_write(bd, req);
	else
		do_read(bd, req);

	/* Wait for interrupt to tell us it's done. */
	blk_stop_queue(q);
}

static struct block_device_operations lguestblk_fops = {
	.owner = THIS_MODULE,
};

static int lguestblk_probe(struct lguest_device *lgdev)
{
	struct blockdev *bd;
	int err;
	int irqflags = IRQF_SHARED;

	bd = kmalloc(sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	spin_lock_init(&bd->lock);
	bd->irq = lgdev_irq(lgdev);
	bd->req = NULL;
	bd->dma.used_len = 0;
	bd->dma.len[0] = 0;
	bd->phys_addr = (lguest_devices[lgdev->index].pfn << PAGE_SHIFT);

	bd->lb_page = lguest_map(bd->phys_addr, 1);
	if (!bd->lb_page) {
		err = -ENOMEM;
		goto out_free_bd;
	}

	bd->major = register_blkdev(0, "lguestblk");
	if (bd->major < 0) {
		err = bd->major;
		goto out_unmap;
	}

	bd->disk = alloc_disk(1);
	if (!bd->disk) {
		err = -ENOMEM;
		goto out_unregister_blkdev;
	}

	bd->disk->queue = blk_init_queue(do_lgb_request, &bd->lock);
	if (!bd->disk->queue) {
		err = -ENOMEM;
		goto out_put_disk;
	}

	/* We can only handle a certain number of sg entries */
	blk_queue_max_hw_segments(bd->disk->queue, LGUEST_MAX_DMA_SECTIONS);
	/* Buffers must not cross page boundaries */
	blk_queue_segment_boundary(bd->disk->queue, PAGE_SIZE-1);

	sprintf(bd->disk->disk_name, "lgb%c", next_block_index++);
	if (lguest_devices[lgdev->index].features & LGUEST_DEVICE_F_RANDOMNESS)
		irqflags |= IRQF_SAMPLE_RANDOM;
	err = request_irq(bd->irq, lgb_irq, irqflags, bd->disk->disk_name, bd);
	if (err)
		goto out_cleanup_queue;

	err = lguest_bind_dma(bd->phys_addr, &bd->dma, 1, bd->irq);
	if (err)
		goto out_free_irq;

	bd->disk->major = bd->major;
	bd->disk->first_minor = 0;
	bd->disk->private_data = bd;
	bd->disk->fops = &lguestblk_fops;
	/* This is initialized to the disk size by the other end. */
	set_capacity(bd->disk, bd->lb_page->num_sectors);
	add_disk(bd->disk);

	printk(KERN_INFO "%s: device %i at major %d\n",
	       bd->disk->disk_name, lgdev->index, bd->major);

	lgdev->private = bd;
	return 0;

out_free_irq:
	free_irq(bd->irq, bd);
out_cleanup_queue:
	blk_cleanup_queue(bd->disk->queue);
out_put_disk:
	put_disk(bd->disk);
out_unregister_blkdev:
	unregister_blkdev(bd->major, "lguestblk");
out_unmap:
	lguest_unmap(bd->lb_page);
out_free_bd:
	kfree(bd);
	return err;
}

static struct lguest_driver lguestblk_drv = {
	.name = "lguestblk",
	.owner = THIS_MODULE,
	.device_type = LGUEST_DEVICE_T_BLOCK,
	.probe = lguestblk_probe,
};

static __init int lguestblk_init(void)
{
	return register_lguest_driver(&lguestblk_drv);
}
module_init(lguestblk_init);

MODULE_DESCRIPTION("Lguest block driver");
MODULE_LICENSE("GPL");
