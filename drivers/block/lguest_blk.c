/*D:400
 * The Guest block driver
 *
 * This is a simple block driver, which appears as /dev/lgba, lgbb, lgbc etc.
 * The mechanism is simple: we place the information about the request in the
 * device page, then use SEND_DMA (containing the data for a write, or an empty
 * "ping" DMA for a read).
 :*/
/* Copyright 2006 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
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

/*D:420 Here is the structure which holds all the information we need about
 * each Guest block device.
 *
 * I'm sure at this stage, you're wondering "hey, where was the adventure I was
 * promised?" and thinking "Rusty sucks, I shall say nasty things about him on
 * my blog".  I think Real adventures have boring bits, too, and you're in the
 * middle of one.  But it gets better.  Just not quite yet. */
struct blockdev
{
	/* The block queue infrastructure wants a spinlock: it is held while it
	 * calls our block request function.  We grab it in our interrupt
	 * handler so the responses don't mess with new requests. */
	spinlock_t lock;

	/* The disk structure registered with kernel. */
	struct gendisk *disk;

	/* The major device number for this disk, and the interrupt.  We only
	 * really keep them here for completeness; we'd need them if we
	 * supported device unplugging. */
	int major;
	int irq;

	/* The physical address of this device's memory page */
	unsigned long phys_addr;
	/* The mapped memory page for convenient acces. */
	struct lguest_block_page *lb_page;

	/* We only have a single request outstanding at a time: this is it. */
	struct lguest_dma dma;
	struct request *req;
};

/*D:495 We originally used end_request() throughout the driver, but it turns
 * out that end_request() is deprecated, and doesn't actually end the request
 * (which seems like a good reason to deprecate it!).  It simply ends the first
 * bio.  So if we had 3 bios in a "struct request" we would do all 3,
 * end_request(), do 2, end_request(), do 1 and end_request(): twice as much
 * work as we needed to do.
 *
 * This reinforced to me that I do not understand the block layer.
 *
 * Nonetheless, Jens Axboe gave me this nice helper to end all chunks of a
 * request.  This improved disk speed by 130%. */
static void end_entire_request(struct request *req, int uptodate)
{
	if (end_that_request_first(req, uptodate, req->hard_nr_sectors))
		BUG();
	add_disk_randomness(req->rq_disk);
	blkdev_dequeue_request(req);
	end_that_request_last(req, uptodate);
}

/* I'm told there are only two stories in the world worth telling: love and
 * hate.  So there used to be a love scene here like this:
 *
 *  Launcher:	We could make beautiful I/O together, you and I.
 *  Guest:	My, that's a big disk!
 *
 * Unfortunately, it was just too raunchy for our otherwise-gentle tale. */

/*D:490 This is the interrupt handler, called when a block read or write has
 * been completed for us. */
static irqreturn_t lgb_irq(int irq, void *_bd)
{
	/* We handed our "struct blockdev" as the argument to request_irq(), so
	 * it is passed through to us here.  This tells us which device we're
	 * dealing with in case we have more than one. */
	struct blockdev *bd = _bd;
	unsigned long flags;

	/* We weren't doing anything?  Strange, but could happen if we shared
	 * interrupts (we don't!). */
	if (!bd->req) {
		pr_debug("No work!\n");
		return IRQ_NONE;
	}

	/* Not done yet?  That's equally strange. */
	if (!bd->lb_page->result) {
		pr_debug("No result!\n");
		return IRQ_NONE;
	}

	/* We have to grab the lock before ending the request. */
	spin_lock_irqsave(&bd->lock, flags);
	/* "result" is 1 for success, 2 for failure: end_entire_request() wants
	 * to know whether this succeeded or not. */
	end_entire_request(bd->req, bd->lb_page->result == 1);
	/* Clear out request, it's done. */
	bd->req = NULL;
	/* Reset incoming DMA for next time. */
	bd->dma.used_len = 0;
	/* Ready for more reads or writes */
	blk_start_queue(bd->disk->queue);
	spin_unlock_irqrestore(&bd->lock, flags);

	/* The interrupt was for us, we dealt with it. */
	return IRQ_HANDLED;
}

/*D:480 The block layer's "struct request" contains a number of "struct bio"s,
 * each of which contains "struct bio_vec"s, each of which contains a page, an
 * offset and a length.
 *
 * Fortunately there are iterators to help us walk through the "struct
 * request".  Even more fortunately, there were plenty of places to steal the
 * code from.  We pack the "struct request" into our "struct lguest_dma" and
 * return the total length. */
static unsigned int req_to_dma(struct request *req, struct lguest_dma *dma)
{
	unsigned int i = 0, idx, len = 0;
	struct bio *bio;

	rq_for_each_bio(bio, req) {
		struct bio_vec *bvec;
		bio_for_each_segment(bvec, bio, idx) {
			/* We told the block layer not to give us too many. */
			BUG_ON(i == LGUEST_MAX_DMA_SECTIONS);
			/* If we had a zero-length segment, it would look like
			 * the end of the data referred to by the "struct
			 * lguest_dma", so make sure that doesn't happen. */
			BUG_ON(!bvec->bv_len);
			/* Convert page & offset to a physical address */
			dma->addr[i] = page_to_phys(bvec->bv_page)
				+ bvec->bv_offset;
			dma->len[i] = bvec->bv_len;
			len += bvec->bv_len;
			i++;
		}
	}
	/* If the array isn't full, we mark the end with a 0 length */
	if (i < LGUEST_MAX_DMA_SECTIONS)
		dma->len[i] = 0;
	return len;
}

/* This creates an empty DMA, useful for prodding the Host without sending data
 * (ie. when we want to do a read) */
static void empty_dma(struct lguest_dma *dma)
{
	dma->len[0] = 0;
}

/*D:470 Setting up a request is fairly easy: */
static void setup_req(struct blockdev *bd,
		      int type, struct request *req, struct lguest_dma *dma)
{
	/* The type is 1 (write) or 0 (read). */
	bd->lb_page->type = type;
	/* The sector on disk where the read or write starts. */
	bd->lb_page->sector = req->sector;
	/* The result is initialized to 0 (unfinished). */
	bd->lb_page->result = 0;
	/* The current request (so we can end it in the interrupt handler). */
	bd->req = req;
	/* The number of bytes: returned as a side-effect of req_to_dma(),
	 * which packs the block layer's "struct request" into our "struct
	 * lguest_dma" */
	bd->lb_page->bytes = req_to_dma(req, dma);
}

/*D:450 Write is pretty straightforward: we pack the request into a "struct
 * lguest_dma", then use SEND_DMA to send the request. */
static void do_write(struct blockdev *bd, struct request *req)
{
	struct lguest_dma send;

	pr_debug("lgb: WRITE sector %li\n", (long)req->sector);
	setup_req(bd, 1, req, &send);

	lguest_send_dma(bd->phys_addr, &send);
}

/* Read is similar to write, except we pack the request into our receive
 * "struct lguest_dma" and send through an empty DMA just to tell the Host that
 * there's a request pending. */
static void do_read(struct blockdev *bd, struct request *req)
{
	struct lguest_dma ping;

	pr_debug("lgb: READ sector %li\n", (long)req->sector);
	setup_req(bd, 0, req, &bd->dma);

	empty_dma(&ping);
	lguest_send_dma(bd->phys_addr, &ping);
}

/*D:440 This where requests come in: we get handed the request queue and are
 * expected to pull a "struct request" off it until we've finished them or
 * we're waiting for a reply: */
static void do_lgb_request(struct request_queue *q)
{
	struct blockdev *bd;
	struct request *req;

again:
	/* This sometimes returns NULL even on the very first time around.  I
	 * wonder if it's something to do with letting elves handle the request
	 * queue... */
	req = elv_next_request(q);
	if (!req)
		return;

	/* We attached the struct blockdev to the disk: get it back */
	bd = req->rq_disk->private_data;
	/* Sometimes we get repeated requests after blk_stop_queue(), but we
	 * can only handle one at a time. */
	if (bd->req)
		return;

	/* We only do reads and writes: no tricky business! */
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

	/* We've put out the request, so stop any more coming in until we get
	 * an interrupt, which takes us to lgb_irq() to re-enable the queue. */
	blk_stop_queue(q);
}

/*D:430 This is the "struct block_device_operations" we attach to the disk at
 * the end of lguestblk_probe().  It doesn't seem to want much. */
static struct block_device_operations lguestblk_fops = {
	.owner = THIS_MODULE,
};

/*D:425 Setting up a disk device seems to involve a lot of code.  I'm not sure
 * quite why.  I do know that the IDE code sent two or three of the maintainers
 * insane, perhaps this is the fringe of the same disease?
 *
 * As in the console code, the probe function gets handed the generic
 * lguest_device from lguest_bus.c: */
static int lguestblk_probe(struct lguest_device *lgdev)
{
	struct blockdev *bd;
	int err;
	int irqflags = IRQF_SHARED;

	/* First we allocate our own "struct blockdev" and initialize the easy
	 * fields. */
	bd = kmalloc(sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	spin_lock_init(&bd->lock);
	bd->irq = lgdev_irq(lgdev);
	bd->req = NULL;
	bd->dma.used_len = 0;
	bd->dma.len[0] = 0;
	/* The descriptor in the lguest_devices array provided by the Host
	 * gives the Guest the physical page number of the device's page. */
	bd->phys_addr = (lguest_devices[lgdev->index].pfn << PAGE_SHIFT);

	/* We use lguest_map() to get a pointer to the device page */
	bd->lb_page = lguest_map(bd->phys_addr, 1);
	if (!bd->lb_page) {
		err = -ENOMEM;
		goto out_free_bd;
	}

	/* We need a major device number: 0 means "assign one dynamically". */
	bd->major = register_blkdev(0, "lguestblk");
	if (bd->major < 0) {
		err = bd->major;
		goto out_unmap;
	}

	/* This allocates a "struct gendisk" where we pack all the information
	 * about the disk which the rest of Linux sees.  We ask for one minor
	 * number; I do wonder if we should be asking for more. */
	bd->disk = alloc_disk(1);
	if (!bd->disk) {
		err = -ENOMEM;
		goto out_unregister_blkdev;
	}

	/* Every disk needs a queue for requests to come in: we set up the
	 * queue with a callback function (the core of our driver) and the lock
	 * to use. */
	bd->disk->queue = blk_init_queue(do_lgb_request, &bd->lock);
	if (!bd->disk->queue) {
		err = -ENOMEM;
		goto out_put_disk;
	}

	/* We can only handle a certain number of pointers in our SEND_DMA
	 * call, so we set that with blk_queue_max_hw_segments().  This is not
	 * to be confused with blk_queue_max_phys_segments() of course!  I
	 * know, who could possibly confuse the two?
	 *
	 * Well, it's simple to tell them apart: this one seems to work and the
	 * other one didn't. */
	blk_queue_max_hw_segments(bd->disk->queue, LGUEST_MAX_DMA_SECTIONS);

	/* Due to technical limitations of our Host (and simple coding) we
	 * can't have a single buffer which crosses a page boundary.  Tell it
	 * here.  This means that our maximum request size is 16
	 * (LGUEST_MAX_DMA_SECTIONS) pages. */
	blk_queue_segment_boundary(bd->disk->queue, PAGE_SIZE-1);

	/* We name our disk: this becomes the device name when udev does its
	 * magic thing and creates the device node, such as /dev/lgba.
	 * next_block_index is a global which starts at 'a'.  Unfortunately
	 * this simple increment logic means that the 27th disk will be called
	 * "/dev/lgb{".  In that case, I recommend having at least 29 disks, so
	 * your /dev directory will be balanced. */
	sprintf(bd->disk->disk_name, "lgb%c", next_block_index++);

	/* We look to the device descriptor again to see if this device's
	 * interrupts are expected to be random.  If they are, we tell the irq
	 * subsystem.  At the moment this bit is always set. */
	if (lguest_devices[lgdev->index].features & LGUEST_DEVICE_F_RANDOMNESS)
		irqflags |= IRQF_SAMPLE_RANDOM;

	/* Now we have the name and irqflags, we can request the interrupt; we
	 * give it the "struct blockdev" we have set up to pass to lgb_irq()
	 * when there is an interrupt. */
	err = request_irq(bd->irq, lgb_irq, irqflags, bd->disk->disk_name, bd);
	if (err)
		goto out_cleanup_queue;

	/* We bind our one-entry DMA pool to the key for this block device so
	 * the Host can reply to our requests.  The key is equal to the
	 * physical address of the device's page, which is conveniently
	 * unique. */
	err = lguest_bind_dma(bd->phys_addr, &bd->dma, 1, bd->irq);
	if (err)
		goto out_free_irq;

	/* We finish our disk initialization and add the disk to the system. */
	bd->disk->major = bd->major;
	bd->disk->first_minor = 0;
	bd->disk->private_data = bd;
	bd->disk->fops = &lguestblk_fops;
	/* This is initialized to the disk size by the Launcher. */
	set_capacity(bd->disk, bd->lb_page->num_sectors);
	add_disk(bd->disk);

	printk(KERN_INFO "%s: device %i at major %d\n",
	       bd->disk->disk_name, lgdev->index, bd->major);

	/* We don't need to keep the "struct blockdev" around, but if we ever
	 * implemented device removal, we'd need this. */
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

/*D:410 The boilerplate code for registering the lguest block driver is just
 * like the console: */
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
