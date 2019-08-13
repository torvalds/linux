// SPDX-License-Identifier: GPL-2.0-or-later
/*
* Filename: dev.c
*
* Authors: Joshua Morris <josh.h.morris@us.ibm.com>
*	Philip Kelleher <pjk1939@linux.vnet.ibm.com>
*
* (C) Copyright 2013 IBM Corporation
*/

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/bio.h>

#include <linux/fs.h>

#include "rsxx_priv.h"

static unsigned int blkdev_minors = 64;
module_param(blkdev_minors, uint, 0444);
MODULE_PARM_DESC(blkdev_minors, "Number of minors(partitions)");

/*
 * For now I'm making this tweakable in case any applications hit this limit.
 * If you see a "bio too big" error in the log you will need to raise this
 * value.
 */
static unsigned int blkdev_max_hw_sectors = 1024;
module_param(blkdev_max_hw_sectors, uint, 0444);
MODULE_PARM_DESC(blkdev_max_hw_sectors, "Max hw sectors for a single BIO");

static unsigned int enable_blkdev = 1;
module_param(enable_blkdev , uint, 0444);
MODULE_PARM_DESC(enable_blkdev, "Enable block device interfaces");


struct rsxx_bio_meta {
	struct bio	*bio;
	atomic_t	pending_dmas;
	atomic_t	error;
	unsigned long	start_time;
};

static struct kmem_cache *bio_meta_pool;

/*----------------- Block Device Operations -----------------*/
static int rsxx_blkdev_ioctl(struct block_device *bdev,
				 fmode_t mode,
				 unsigned int cmd,
				 unsigned long arg)
{
	struct rsxx_cardinfo *card = bdev->bd_disk->private_data;

	switch (cmd) {
	case RSXX_GETREG:
		return rsxx_reg_access(card, (void __user *)arg, 1);
	case RSXX_SETREG:
		return rsxx_reg_access(card, (void __user *)arg, 0);
	}

	return -ENOTTY;
}

static int rsxx_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct rsxx_cardinfo *card = bdev->bd_disk->private_data;
	u64 blocks = card->size8 >> 9;

	/*
	 * get geometry: Fake it. I haven't found any drivers that set
	 * geo->start, so we won't either.
	 */
	if (card->size8) {
		geo->heads = 64;
		geo->sectors = 16;
		do_div(blocks, (geo->heads * geo->sectors));
		geo->cylinders = blocks;
	} else {
		geo->heads = 0;
		geo->sectors = 0;
		geo->cylinders = 0;
	}
	return 0;
}

static const struct block_device_operations rsxx_fops = {
	.owner		= THIS_MODULE,
	.getgeo		= rsxx_getgeo,
	.ioctl		= rsxx_blkdev_ioctl,
};

static void disk_stats_start(struct rsxx_cardinfo *card, struct bio *bio)
{
	generic_start_io_acct(card->queue, bio_op(bio), bio_sectors(bio),
			     &card->gendisk->part0);
}

static void disk_stats_complete(struct rsxx_cardinfo *card,
				struct bio *bio,
				unsigned long start_time)
{
	generic_end_io_acct(card->queue, bio_op(bio),
			    &card->gendisk->part0, start_time);
}

static void bio_dma_done_cb(struct rsxx_cardinfo *card,
			    void *cb_data,
			    unsigned int error)
{
	struct rsxx_bio_meta *meta = cb_data;

	if (error)
		atomic_set(&meta->error, 1);

	if (atomic_dec_and_test(&meta->pending_dmas)) {
		if (!card->eeh_state && card->gendisk)
			disk_stats_complete(card, meta->bio, meta->start_time);

		if (atomic_read(&meta->error))
			bio_io_error(meta->bio);
		else
			bio_endio(meta->bio);
		kmem_cache_free(bio_meta_pool, meta);
	}
}

static blk_qc_t rsxx_make_request(struct request_queue *q, struct bio *bio)
{
	struct rsxx_cardinfo *card = q->queuedata;
	struct rsxx_bio_meta *bio_meta;
	blk_status_t st = BLK_STS_IOERR;

	blk_queue_split(q, &bio);

	might_sleep();

	if (!card)
		goto req_err;

	if (bio_end_sector(bio) > get_capacity(card->gendisk))
		goto req_err;

	if (unlikely(card->halt))
		goto req_err;

	if (unlikely(card->dma_fault))
		goto req_err;

	if (bio->bi_iter.bi_size == 0) {
		dev_err(CARD_TO_DEV(card), "size zero BIO!\n");
		goto req_err;
	}

	bio_meta = kmem_cache_alloc(bio_meta_pool, GFP_KERNEL);
	if (!bio_meta) {
		st = BLK_STS_RESOURCE;
		goto req_err;
	}

	bio_meta->bio = bio;
	atomic_set(&bio_meta->error, 0);
	atomic_set(&bio_meta->pending_dmas, 0);
	bio_meta->start_time = jiffies;

	if (!unlikely(card->halt))
		disk_stats_start(card, bio);

	dev_dbg(CARD_TO_DEV(card), "BIO[%c]: meta: %p addr8: x%llx size: %d\n",
		 bio_data_dir(bio) ? 'W' : 'R', bio_meta,
		 (u64)bio->bi_iter.bi_sector << 9, bio->bi_iter.bi_size);

	st = rsxx_dma_queue_bio(card, bio, &bio_meta->pending_dmas,
				    bio_dma_done_cb, bio_meta);
	if (st)
		goto queue_err;

	return BLK_QC_T_NONE;

queue_err:
	kmem_cache_free(bio_meta_pool, bio_meta);
req_err:
	if (st)
		bio->bi_status = st;
	bio_endio(bio);
	return BLK_QC_T_NONE;
}

/*----------------- Device Setup -------------------*/
static bool rsxx_discard_supported(struct rsxx_cardinfo *card)
{
	unsigned char pci_rev;

	pci_read_config_byte(card->dev, PCI_REVISION_ID, &pci_rev);

	return (pci_rev >= RSXX_DISCARD_SUPPORT);
}

int rsxx_attach_dev(struct rsxx_cardinfo *card)
{
	mutex_lock(&card->dev_lock);

	/* The block device requires the stripe size from the config. */
	if (enable_blkdev) {
		if (card->config_valid)
			set_capacity(card->gendisk, card->size8 >> 9);
		else
			set_capacity(card->gendisk, 0);
		device_add_disk(CARD_TO_DEV(card), card->gendisk, NULL);
		card->bdev_attached = 1;
	}

	mutex_unlock(&card->dev_lock);

	return 0;
}

void rsxx_detach_dev(struct rsxx_cardinfo *card)
{
	mutex_lock(&card->dev_lock);

	if (card->bdev_attached) {
		del_gendisk(card->gendisk);
		card->bdev_attached = 0;
	}

	mutex_unlock(&card->dev_lock);
}

int rsxx_setup_dev(struct rsxx_cardinfo *card)
{
	unsigned short blk_size;

	mutex_init(&card->dev_lock);

	if (!enable_blkdev)
		return 0;

	card->major = register_blkdev(0, DRIVER_NAME);
	if (card->major < 0) {
		dev_err(CARD_TO_DEV(card), "Failed to get major number\n");
		return -ENOMEM;
	}

	card->queue = blk_alloc_queue(GFP_KERNEL);
	if (!card->queue) {
		dev_err(CARD_TO_DEV(card), "Failed queue alloc\n");
		unregister_blkdev(card->major, DRIVER_NAME);
		return -ENOMEM;
	}

	card->gendisk = alloc_disk(blkdev_minors);
	if (!card->gendisk) {
		dev_err(CARD_TO_DEV(card), "Failed disk alloc\n");
		blk_cleanup_queue(card->queue);
		unregister_blkdev(card->major, DRIVER_NAME);
		return -ENOMEM;
	}

	if (card->config_valid) {
		blk_size = card->config.data.block_size;
		blk_queue_dma_alignment(card->queue, blk_size - 1);
		blk_queue_logical_block_size(card->queue, blk_size);
	}

	blk_queue_make_request(card->queue, rsxx_make_request);
	blk_queue_max_hw_sectors(card->queue, blkdev_max_hw_sectors);
	blk_queue_physical_block_size(card->queue, RSXX_HW_BLK_SIZE);

	blk_queue_flag_set(QUEUE_FLAG_NONROT, card->queue);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, card->queue);
	if (rsxx_discard_supported(card)) {
		blk_queue_flag_set(QUEUE_FLAG_DISCARD, card->queue);
		blk_queue_max_discard_sectors(card->queue,
						RSXX_HW_BLK_SIZE >> 9);
		card->queue->limits.discard_granularity = RSXX_HW_BLK_SIZE;
		card->queue->limits.discard_alignment   = RSXX_HW_BLK_SIZE;
	}

	card->queue->queuedata = card;

	snprintf(card->gendisk->disk_name, sizeof(card->gendisk->disk_name),
		 "rsxx%d", card->disk_id);
	card->gendisk->major = card->major;
	card->gendisk->first_minor = 0;
	card->gendisk->fops = &rsxx_fops;
	card->gendisk->private_data = card;
	card->gendisk->queue = card->queue;

	return 0;
}

void rsxx_destroy_dev(struct rsxx_cardinfo *card)
{
	if (!enable_blkdev)
		return;

	put_disk(card->gendisk);
	card->gendisk = NULL;

	blk_cleanup_queue(card->queue);
	card->queue->queuedata = NULL;
	unregister_blkdev(card->major, DRIVER_NAME);
}

int rsxx_dev_init(void)
{
	bio_meta_pool = KMEM_CACHE(rsxx_bio_meta, SLAB_HWCACHE_ALIGN);
	if (!bio_meta_pool)
		return -ENOMEM;

	return 0;
}

void rsxx_dev_cleanup(void)
{
	kmem_cache_destroy(bio_meta_pool);
}


