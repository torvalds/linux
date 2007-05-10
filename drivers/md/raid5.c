/*
 * raid5.c : Multiple Devices driver for Linux
 *	   Copyright (C) 1996, 1997 Ingo Molnar, Miguel de Icaza, Gadi Oxman
 *	   Copyright (C) 1999, 2000 Ingo Molnar
 *	   Copyright (C) 2002, 2003 H. Peter Anvin
 *
 * RAID-4/5/6 management functions.
 * Thanks to Penguin Computing for making the RAID-6 development possible
 * by donating a test server!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * BITMAP UNPLUGGING:
 *
 * The sequencing for updating the bitmap reliably is a little
 * subtle (and I got it wrong the first time) so it deserves some
 * explanation.
 *
 * We group bitmap updates into batches.  Each batch has a number.
 * We may write out several batches at once, but that isn't very important.
 * conf->bm_write is the number of the last batch successfully written.
 * conf->bm_flush is the number of the last batch that was closed to
 *    new additions.
 * When we discover that we will need to write to any block in a stripe
 * (in add_stripe_bio) we update the in-memory bitmap and record in sh->bm_seq
 * the number of the batch it will be in. This is bm_flush+1.
 * When we are ready to do a write, if that batch hasn't been written yet,
 *   we plug the array and queue the stripe for later.
 * When an unplug happens, we increment bm_flush, thus closing the current
 *   batch.
 * When we notice that bm_flush > bm_write, we write out all pending updates
 * to the bitmap, and advance bm_write to where bm_flush was.
 * This may occasionally write a bit out twice, but is sure never to
 * miss any bits.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/bitops.h>
#include <linux/kthread.h>
#include <asm/atomic.h>
#include "raid6.h"

#include <linux/raid/bitmap.h>

/*
 * Stripe cache
 */

#define NR_STRIPES		256
#define STRIPE_SIZE		PAGE_SIZE
#define STRIPE_SHIFT		(PAGE_SHIFT - 9)
#define STRIPE_SECTORS		(STRIPE_SIZE>>9)
#define	IO_THRESHOLD		1
#define NR_HASH			(PAGE_SIZE / sizeof(struct hlist_head))
#define HASH_MASK		(NR_HASH - 1)

#define stripe_hash(conf, sect)	(&((conf)->stripe_hashtbl[((sect) >> STRIPE_SHIFT) & HASH_MASK]))

/* bio's attached to a stripe+device for I/O are linked together in bi_sector
 * order without overlap.  There may be several bio's per stripe+device, and
 * a bio could span several devices.
 * When walking this list for a particular stripe+device, we must never proceed
 * beyond a bio that extends past this device, as the next bio might no longer
 * be valid.
 * This macro is used to determine the 'next' bio in the list, given the sector
 * of the current stripe+device
 */
#define r5_next_bio(bio, sect) ( ( (bio)->bi_sector + ((bio)->bi_size>>9) < sect + STRIPE_SECTORS) ? (bio)->bi_next : NULL)
/*
 * The following can be used to debug the driver
 */
#define RAID5_DEBUG	0
#define RAID5_PARANOIA	1
#if RAID5_PARANOIA && defined(CONFIG_SMP)
# define CHECK_DEVLOCK() assert_spin_locked(&conf->device_lock)
#else
# define CHECK_DEVLOCK()
#endif

#define PRINTK(x...) ((void)(RAID5_DEBUG && printk(x)))
#if RAID5_DEBUG
#define inline
#define __inline__
#endif

#if !RAID6_USE_EMPTY_ZERO_PAGE
/* In .bss so it's zeroed */
const char raid6_empty_zero_page[PAGE_SIZE] __attribute__((aligned(256)));
#endif

static inline int raid6_next_disk(int disk, int raid_disks)
{
	disk++;
	return (disk < raid_disks) ? disk : 0;
}
static void print_raid5_conf (raid5_conf_t *conf);

static void __release_stripe(raid5_conf_t *conf, struct stripe_head *sh)
{
	if (atomic_dec_and_test(&sh->count)) {
		BUG_ON(!list_empty(&sh->lru));
		BUG_ON(atomic_read(&conf->active_stripes)==0);
		if (test_bit(STRIPE_HANDLE, &sh->state)) {
			if (test_bit(STRIPE_DELAYED, &sh->state)) {
				list_add_tail(&sh->lru, &conf->delayed_list);
				blk_plug_device(conf->mddev->queue);
			} else if (test_bit(STRIPE_BIT_DELAY, &sh->state) &&
				   sh->bm_seq - conf->seq_write > 0) {
				list_add_tail(&sh->lru, &conf->bitmap_list);
				blk_plug_device(conf->mddev->queue);
			} else {
				clear_bit(STRIPE_BIT_DELAY, &sh->state);
				list_add_tail(&sh->lru, &conf->handle_list);
			}
			md_wakeup_thread(conf->mddev->thread);
		} else {
			if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
				atomic_dec(&conf->preread_active_stripes);
				if (atomic_read(&conf->preread_active_stripes) < IO_THRESHOLD)
					md_wakeup_thread(conf->mddev->thread);
			}
			atomic_dec(&conf->active_stripes);
			if (!test_bit(STRIPE_EXPANDING, &sh->state)) {
				list_add_tail(&sh->lru, &conf->inactive_list);
				wake_up(&conf->wait_for_stripe);
				if (conf->retry_read_aligned)
					md_wakeup_thread(conf->mddev->thread);
			}
		}
	}
}
static void release_stripe(struct stripe_head *sh)
{
	raid5_conf_t *conf = sh->raid_conf;
	unsigned long flags;

	spin_lock_irqsave(&conf->device_lock, flags);
	__release_stripe(conf, sh);
	spin_unlock_irqrestore(&conf->device_lock, flags);
}

static inline void remove_hash(struct stripe_head *sh)
{
	PRINTK("remove_hash(), stripe %llu\n", (unsigned long long)sh->sector);

	hlist_del_init(&sh->hash);
}

static inline void insert_hash(raid5_conf_t *conf, struct stripe_head *sh)
{
	struct hlist_head *hp = stripe_hash(conf, sh->sector);

	PRINTK("insert_hash(), stripe %llu\n", (unsigned long long)sh->sector);

	CHECK_DEVLOCK();
	hlist_add_head(&sh->hash, hp);
}


/* find an idle stripe, make sure it is unhashed, and return it. */
static struct stripe_head *get_free_stripe(raid5_conf_t *conf)
{
	struct stripe_head *sh = NULL;
	struct list_head *first;

	CHECK_DEVLOCK();
	if (list_empty(&conf->inactive_list))
		goto out;
	first = conf->inactive_list.next;
	sh = list_entry(first, struct stripe_head, lru);
	list_del_init(first);
	remove_hash(sh);
	atomic_inc(&conf->active_stripes);
out:
	return sh;
}

static void shrink_buffers(struct stripe_head *sh, int num)
{
	struct page *p;
	int i;

	for (i=0; i<num ; i++) {
		p = sh->dev[i].page;
		if (!p)
			continue;
		sh->dev[i].page = NULL;
		put_page(p);
	}
}

static int grow_buffers(struct stripe_head *sh, int num)
{
	int i;

	for (i=0; i<num; i++) {
		struct page *page;

		if (!(page = alloc_page(GFP_KERNEL))) {
			return 1;
		}
		sh->dev[i].page = page;
	}
	return 0;
}

static void raid5_build_block (struct stripe_head *sh, int i);

static void init_stripe(struct stripe_head *sh, sector_t sector, int pd_idx, int disks)
{
	raid5_conf_t *conf = sh->raid_conf;
	int i;

	BUG_ON(atomic_read(&sh->count) != 0);
	BUG_ON(test_bit(STRIPE_HANDLE, &sh->state));
	
	CHECK_DEVLOCK();
	PRINTK("init_stripe called, stripe %llu\n", 
		(unsigned long long)sh->sector);

	remove_hash(sh);

	sh->sector = sector;
	sh->pd_idx = pd_idx;
	sh->state = 0;

	sh->disks = disks;

	for (i = sh->disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];

		if (dev->toread || dev->towrite || dev->written ||
		    test_bit(R5_LOCKED, &dev->flags)) {
			printk("sector=%llx i=%d %p %p %p %d\n",
			       (unsigned long long)sh->sector, i, dev->toread,
			       dev->towrite, dev->written,
			       test_bit(R5_LOCKED, &dev->flags));
			BUG();
		}
		dev->flags = 0;
		raid5_build_block(sh, i);
	}
	insert_hash(conf, sh);
}

static struct stripe_head *__find_stripe(raid5_conf_t *conf, sector_t sector, int disks)
{
	struct stripe_head *sh;
	struct hlist_node *hn;

	CHECK_DEVLOCK();
	PRINTK("__find_stripe, sector %llu\n", (unsigned long long)sector);
	hlist_for_each_entry(sh, hn, stripe_hash(conf, sector), hash)
		if (sh->sector == sector && sh->disks == disks)
			return sh;
	PRINTK("__stripe %llu not in cache\n", (unsigned long long)sector);
	return NULL;
}

static void unplug_slaves(mddev_t *mddev);
static void raid5_unplug_device(request_queue_t *q);

static struct stripe_head *get_active_stripe(raid5_conf_t *conf, sector_t sector, int disks,
					     int pd_idx, int noblock)
{
	struct stripe_head *sh;

	PRINTK("get_stripe, sector %llu\n", (unsigned long long)sector);

	spin_lock_irq(&conf->device_lock);

	do {
		wait_event_lock_irq(conf->wait_for_stripe,
				    conf->quiesce == 0,
				    conf->device_lock, /* nothing */);
		sh = __find_stripe(conf, sector, disks);
		if (!sh) {
			if (!conf->inactive_blocked)
				sh = get_free_stripe(conf);
			if (noblock && sh == NULL)
				break;
			if (!sh) {
				conf->inactive_blocked = 1;
				wait_event_lock_irq(conf->wait_for_stripe,
						    !list_empty(&conf->inactive_list) &&
						    (atomic_read(&conf->active_stripes)
						     < (conf->max_nr_stripes *3/4)
						     || !conf->inactive_blocked),
						    conf->device_lock,
						    raid5_unplug_device(conf->mddev->queue)
					);
				conf->inactive_blocked = 0;
			} else
				init_stripe(sh, sector, pd_idx, disks);
		} else {
			if (atomic_read(&sh->count)) {
			  BUG_ON(!list_empty(&sh->lru));
			} else {
				if (!test_bit(STRIPE_HANDLE, &sh->state))
					atomic_inc(&conf->active_stripes);
				if (list_empty(&sh->lru) &&
				    !test_bit(STRIPE_EXPANDING, &sh->state))
					BUG();
				list_del_init(&sh->lru);
			}
		}
	} while (sh == NULL);

	if (sh)
		atomic_inc(&sh->count);

	spin_unlock_irq(&conf->device_lock);
	return sh;
}

static int grow_one_stripe(raid5_conf_t *conf)
{
	struct stripe_head *sh;
	sh = kmem_cache_alloc(conf->slab_cache, GFP_KERNEL);
	if (!sh)
		return 0;
	memset(sh, 0, sizeof(*sh) + (conf->raid_disks-1)*sizeof(struct r5dev));
	sh->raid_conf = conf;
	spin_lock_init(&sh->lock);

	if (grow_buffers(sh, conf->raid_disks)) {
		shrink_buffers(sh, conf->raid_disks);
		kmem_cache_free(conf->slab_cache, sh);
		return 0;
	}
	sh->disks = conf->raid_disks;
	/* we just created an active stripe so... */
	atomic_set(&sh->count, 1);
	atomic_inc(&conf->active_stripes);
	INIT_LIST_HEAD(&sh->lru);
	release_stripe(sh);
	return 1;
}

static int grow_stripes(raid5_conf_t *conf, int num)
{
	struct kmem_cache *sc;
	int devs = conf->raid_disks;

	sprintf(conf->cache_name[0], "raid5-%s", mdname(conf->mddev));
	sprintf(conf->cache_name[1], "raid5-%s-alt", mdname(conf->mddev));
	conf->active_name = 0;
	sc = kmem_cache_create(conf->cache_name[conf->active_name],
			       sizeof(struct stripe_head)+(devs-1)*sizeof(struct r5dev),
			       0, 0, NULL, NULL);
	if (!sc)
		return 1;
	conf->slab_cache = sc;
	conf->pool_size = devs;
	while (num--)
		if (!grow_one_stripe(conf))
			return 1;
	return 0;
}

#ifdef CONFIG_MD_RAID5_RESHAPE
static int resize_stripes(raid5_conf_t *conf, int newsize)
{
	/* Make all the stripes able to hold 'newsize' devices.
	 * New slots in each stripe get 'page' set to a new page.
	 *
	 * This happens in stages:
	 * 1/ create a new kmem_cache and allocate the required number of
	 *    stripe_heads.
	 * 2/ gather all the old stripe_heads and tranfer the pages across
	 *    to the new stripe_heads.  This will have the side effect of
	 *    freezing the array as once all stripe_heads have been collected,
	 *    no IO will be possible.  Old stripe heads are freed once their
	 *    pages have been transferred over, and the old kmem_cache is
	 *    freed when all stripes are done.
	 * 3/ reallocate conf->disks to be suitable bigger.  If this fails,
	 *    we simple return a failre status - no need to clean anything up.
	 * 4/ allocate new pages for the new slots in the new stripe_heads.
	 *    If this fails, we don't bother trying the shrink the
	 *    stripe_heads down again, we just leave them as they are.
	 *    As each stripe_head is processed the new one is released into
	 *    active service.
	 *
	 * Once step2 is started, we cannot afford to wait for a write,
	 * so we use GFP_NOIO allocations.
	 */
	struct stripe_head *osh, *nsh;
	LIST_HEAD(newstripes);
	struct disk_info *ndisks;
	int err = 0;
	struct kmem_cache *sc;
	int i;

	if (newsize <= conf->pool_size)
		return 0; /* never bother to shrink */

	md_allow_write(conf->mddev);

	/* Step 1 */
	sc = kmem_cache_create(conf->cache_name[1-conf->active_name],
			       sizeof(struct stripe_head)+(newsize-1)*sizeof(struct r5dev),
			       0, 0, NULL, NULL);
	if (!sc)
		return -ENOMEM;

	for (i = conf->max_nr_stripes; i; i--) {
		nsh = kmem_cache_alloc(sc, GFP_KERNEL);
		if (!nsh)
			break;

		memset(nsh, 0, sizeof(*nsh) + (newsize-1)*sizeof(struct r5dev));

		nsh->raid_conf = conf;
		spin_lock_init(&nsh->lock);

		list_add(&nsh->lru, &newstripes);
	}
	if (i) {
		/* didn't get enough, give up */
		while (!list_empty(&newstripes)) {
			nsh = list_entry(newstripes.next, struct stripe_head, lru);
			list_del(&nsh->lru);
			kmem_cache_free(sc, nsh);
		}
		kmem_cache_destroy(sc);
		return -ENOMEM;
	}
	/* Step 2 - Must use GFP_NOIO now.
	 * OK, we have enough stripes, start collecting inactive
	 * stripes and copying them over
	 */
	list_for_each_entry(nsh, &newstripes, lru) {
		spin_lock_irq(&conf->device_lock);
		wait_event_lock_irq(conf->wait_for_stripe,
				    !list_empty(&conf->inactive_list),
				    conf->device_lock,
				    unplug_slaves(conf->mddev)
			);
		osh = get_free_stripe(conf);
		spin_unlock_irq(&conf->device_lock);
		atomic_set(&nsh->count, 1);
		for(i=0; i<conf->pool_size; i++)
			nsh->dev[i].page = osh->dev[i].page;
		for( ; i<newsize; i++)
			nsh->dev[i].page = NULL;
		kmem_cache_free(conf->slab_cache, osh);
	}
	kmem_cache_destroy(conf->slab_cache);

	/* Step 3.
	 * At this point, we are holding all the stripes so the array
	 * is completely stalled, so now is a good time to resize
	 * conf->disks.
	 */
	ndisks = kzalloc(newsize * sizeof(struct disk_info), GFP_NOIO);
	if (ndisks) {
		for (i=0; i<conf->raid_disks; i++)
			ndisks[i] = conf->disks[i];
		kfree(conf->disks);
		conf->disks = ndisks;
	} else
		err = -ENOMEM;

	/* Step 4, return new stripes to service */
	while(!list_empty(&newstripes)) {
		nsh = list_entry(newstripes.next, struct stripe_head, lru);
		list_del_init(&nsh->lru);
		for (i=conf->raid_disks; i < newsize; i++)
			if (nsh->dev[i].page == NULL) {
				struct page *p = alloc_page(GFP_NOIO);
				nsh->dev[i].page = p;
				if (!p)
					err = -ENOMEM;
			}
		release_stripe(nsh);
	}
	/* critical section pass, GFP_NOIO no longer needed */

	conf->slab_cache = sc;
	conf->active_name = 1-conf->active_name;
	conf->pool_size = newsize;
	return err;
}
#endif

static int drop_one_stripe(raid5_conf_t *conf)
{
	struct stripe_head *sh;

	spin_lock_irq(&conf->device_lock);
	sh = get_free_stripe(conf);
	spin_unlock_irq(&conf->device_lock);
	if (!sh)
		return 0;
	BUG_ON(atomic_read(&sh->count));
	shrink_buffers(sh, conf->pool_size);
	kmem_cache_free(conf->slab_cache, sh);
	atomic_dec(&conf->active_stripes);
	return 1;
}

static void shrink_stripes(raid5_conf_t *conf)
{
	while (drop_one_stripe(conf))
		;

	if (conf->slab_cache)
		kmem_cache_destroy(conf->slab_cache);
	conf->slab_cache = NULL;
}

static int raid5_end_read_request(struct bio * bi, unsigned int bytes_done,
				   int error)
{
 	struct stripe_head *sh = bi->bi_private;
	raid5_conf_t *conf = sh->raid_conf;
	int disks = sh->disks, i;
	int uptodate = test_bit(BIO_UPTODATE, &bi->bi_flags);
	char b[BDEVNAME_SIZE];
	mdk_rdev_t *rdev;

	if (bi->bi_size)
		return 1;

	for (i=0 ; i<disks; i++)
		if (bi == &sh->dev[i].req)
			break;

	PRINTK("end_read_request %llu/%d, count: %d, uptodate %d.\n", 
		(unsigned long long)sh->sector, i, atomic_read(&sh->count), 
		uptodate);
	if (i == disks) {
		BUG();
		return 0;
	}

	if (uptodate) {
		set_bit(R5_UPTODATE, &sh->dev[i].flags);
		if (test_bit(R5_ReadError, &sh->dev[i].flags)) {
			rdev = conf->disks[i].rdev;
			printk(KERN_INFO "raid5:%s: read error corrected (%lu sectors at %llu on %s)\n",
			       mdname(conf->mddev), STRIPE_SECTORS,
			       (unsigned long long)sh->sector + rdev->data_offset,
			       bdevname(rdev->bdev, b));
			clear_bit(R5_ReadError, &sh->dev[i].flags);
			clear_bit(R5_ReWrite, &sh->dev[i].flags);
		}
		if (atomic_read(&conf->disks[i].rdev->read_errors))
			atomic_set(&conf->disks[i].rdev->read_errors, 0);
	} else {
		const char *bdn = bdevname(conf->disks[i].rdev->bdev, b);
		int retry = 0;
		rdev = conf->disks[i].rdev;

		clear_bit(R5_UPTODATE, &sh->dev[i].flags);
		atomic_inc(&rdev->read_errors);
		if (conf->mddev->degraded)
			printk(KERN_WARNING "raid5:%s: read error not correctable (sector %llu on %s).\n",
			       mdname(conf->mddev),
			       (unsigned long long)sh->sector + rdev->data_offset,
			       bdn);
		else if (test_bit(R5_ReWrite, &sh->dev[i].flags))
			/* Oh, no!!! */
			printk(KERN_WARNING "raid5:%s: read error NOT corrected!! (sector %llu on %s).\n",
			       mdname(conf->mddev),
			       (unsigned long long)sh->sector + rdev->data_offset,
			       bdn);
		else if (atomic_read(&rdev->read_errors)
			 > conf->max_nr_stripes)
			printk(KERN_WARNING
			       "raid5:%s: Too many read errors, failing device %s.\n",
			       mdname(conf->mddev), bdn);
		else
			retry = 1;
		if (retry)
			set_bit(R5_ReadError, &sh->dev[i].flags);
		else {
			clear_bit(R5_ReadError, &sh->dev[i].flags);
			clear_bit(R5_ReWrite, &sh->dev[i].flags);
			md_error(conf->mddev, rdev);
		}
	}
	rdev_dec_pending(conf->disks[i].rdev, conf->mddev);
	clear_bit(R5_LOCKED, &sh->dev[i].flags);
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
	return 0;
}

static int raid5_end_write_request (struct bio *bi, unsigned int bytes_done,
				    int error)
{
 	struct stripe_head *sh = bi->bi_private;
	raid5_conf_t *conf = sh->raid_conf;
	int disks = sh->disks, i;
	int uptodate = test_bit(BIO_UPTODATE, &bi->bi_flags);

	if (bi->bi_size)
		return 1;

	for (i=0 ; i<disks; i++)
		if (bi == &sh->dev[i].req)
			break;

	PRINTK("end_write_request %llu/%d, count %d, uptodate: %d.\n", 
		(unsigned long long)sh->sector, i, atomic_read(&sh->count),
		uptodate);
	if (i == disks) {
		BUG();
		return 0;
	}

	if (!uptodate)
		md_error(conf->mddev, conf->disks[i].rdev);

	rdev_dec_pending(conf->disks[i].rdev, conf->mddev);
	
	clear_bit(R5_LOCKED, &sh->dev[i].flags);
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
	return 0;
}


static sector_t compute_blocknr(struct stripe_head *sh, int i);
	
static void raid5_build_block (struct stripe_head *sh, int i)
{
	struct r5dev *dev = &sh->dev[i];

	bio_init(&dev->req);
	dev->req.bi_io_vec = &dev->vec;
	dev->req.bi_vcnt++;
	dev->req.bi_max_vecs++;
	dev->vec.bv_page = dev->page;
	dev->vec.bv_len = STRIPE_SIZE;
	dev->vec.bv_offset = 0;

	dev->req.bi_sector = sh->sector;
	dev->req.bi_private = sh;

	dev->flags = 0;
	dev->sector = compute_blocknr(sh, i);
}

static void error(mddev_t *mddev, mdk_rdev_t *rdev)
{
	char b[BDEVNAME_SIZE];
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	PRINTK("raid5: error called\n");

	if (!test_bit(Faulty, &rdev->flags)) {
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		if (test_and_clear_bit(In_sync, &rdev->flags)) {
			unsigned long flags;
			spin_lock_irqsave(&conf->device_lock, flags);
			mddev->degraded++;
			spin_unlock_irqrestore(&conf->device_lock, flags);
			/*
			 * if recovery was running, make sure it aborts.
			 */
			set_bit(MD_RECOVERY_ERR, &mddev->recovery);
		}
		set_bit(Faulty, &rdev->flags);
		printk (KERN_ALERT
			"raid5: Disk failure on %s, disabling device."
			" Operation continuing on %d devices\n",
			bdevname(rdev->bdev,b), conf->raid_disks - mddev->degraded);
	}
}

/*
 * Input: a 'big' sector number,
 * Output: index of the data and parity disk, and the sector # in them.
 */
static sector_t raid5_compute_sector(sector_t r_sector, unsigned int raid_disks,
			unsigned int data_disks, unsigned int * dd_idx,
			unsigned int * pd_idx, raid5_conf_t *conf)
{
	long stripe;
	unsigned long chunk_number;
	unsigned int chunk_offset;
	sector_t new_sector;
	int sectors_per_chunk = conf->chunk_size >> 9;

	/* First compute the information on this sector */

	/*
	 * Compute the chunk number and the sector offset inside the chunk
	 */
	chunk_offset = sector_div(r_sector, sectors_per_chunk);
	chunk_number = r_sector;
	BUG_ON(r_sector != chunk_number);

	/*
	 * Compute the stripe number
	 */
	stripe = chunk_number / data_disks;

	/*
	 * Compute the data disk and parity disk indexes inside the stripe
	 */
	*dd_idx = chunk_number % data_disks;

	/*
	 * Select the parity disk based on the user selected algorithm.
	 */
	switch(conf->level) {
	case 4:
		*pd_idx = data_disks;
		break;
	case 5:
		switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
			*pd_idx = data_disks - stripe % raid_disks;
			if (*dd_idx >= *pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_RIGHT_ASYMMETRIC:
			*pd_idx = stripe % raid_disks;
			if (*dd_idx >= *pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
			*pd_idx = data_disks - stripe % raid_disks;
			*dd_idx = (*pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_RIGHT_SYMMETRIC:
			*pd_idx = stripe % raid_disks;
			*dd_idx = (*pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		default:
			printk(KERN_ERR "raid5: unsupported algorithm %d\n",
				conf->algorithm);
		}
		break;
	case 6:

		/**** FIX THIS ****/
		switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
			*pd_idx = raid_disks - 1 - (stripe % raid_disks);
			if (*pd_idx == raid_disks-1)
				(*dd_idx)++; 	/* Q D D D P */
			else if (*dd_idx >= *pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			break;
		case ALGORITHM_RIGHT_ASYMMETRIC:
			*pd_idx = stripe % raid_disks;
			if (*pd_idx == raid_disks-1)
				(*dd_idx)++; 	/* Q D D D P */
			else if (*dd_idx >= *pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
			*pd_idx = raid_disks - 1 - (stripe % raid_disks);
			*dd_idx = (*pd_idx + 2 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_RIGHT_SYMMETRIC:
			*pd_idx = stripe % raid_disks;
			*dd_idx = (*pd_idx + 2 + *dd_idx) % raid_disks;
			break;
		default:
			printk (KERN_CRIT "raid6: unsupported algorithm %d\n",
				conf->algorithm);
		}
		break;
	}

	/*
	 * Finally, compute the new sector number
	 */
	new_sector = (sector_t)stripe * sectors_per_chunk + chunk_offset;
	return new_sector;
}


static sector_t compute_blocknr(struct stripe_head *sh, int i)
{
	raid5_conf_t *conf = sh->raid_conf;
	int raid_disks = sh->disks;
	int data_disks = raid_disks - conf->max_degraded;
	sector_t new_sector = sh->sector, check;
	int sectors_per_chunk = conf->chunk_size >> 9;
	sector_t stripe;
	int chunk_offset;
	int chunk_number, dummy1, dummy2, dd_idx = i;
	sector_t r_sector;


	chunk_offset = sector_div(new_sector, sectors_per_chunk);
	stripe = new_sector;
	BUG_ON(new_sector != stripe);

	if (i == sh->pd_idx)
		return 0;
	switch(conf->level) {
	case 4: break;
	case 5:
		switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
		case ALGORITHM_RIGHT_ASYMMETRIC:
			if (i > sh->pd_idx)
				i--;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
		case ALGORITHM_RIGHT_SYMMETRIC:
			if (i < sh->pd_idx)
				i += raid_disks;
			i -= (sh->pd_idx + 1);
			break;
		default:
			printk(KERN_ERR "raid5: unsupported algorithm %d\n",
			       conf->algorithm);
		}
		break;
	case 6:
		if (i == raid6_next_disk(sh->pd_idx, raid_disks))
			return 0; /* It is the Q disk */
		switch (conf->algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
		case ALGORITHM_RIGHT_ASYMMETRIC:
		  	if (sh->pd_idx == raid_disks-1)
				i--; 	/* Q D D D P */
			else if (i > sh->pd_idx)
				i -= 2; /* D D P Q D */
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
		case ALGORITHM_RIGHT_SYMMETRIC:
			if (sh->pd_idx == raid_disks-1)
				i--; /* Q D D D P */
			else {
				/* D D P Q D */
				if (i < sh->pd_idx)
					i += raid_disks;
				i -= (sh->pd_idx + 2);
			}
			break;
		default:
			printk (KERN_CRIT "raid6: unsupported algorithm %d\n",
				conf->algorithm);
		}
		break;
	}

	chunk_number = stripe * data_disks + i;
	r_sector = (sector_t)chunk_number * sectors_per_chunk + chunk_offset;

	check = raid5_compute_sector (r_sector, raid_disks, data_disks, &dummy1, &dummy2, conf);
	if (check != sh->sector || dummy1 != dd_idx || dummy2 != sh->pd_idx) {
		printk(KERN_ERR "compute_blocknr: map not correct\n");
		return 0;
	}
	return r_sector;
}



/*
 * Copy data between a page in the stripe cache, and one or more bion
 * The page could align with the middle of the bio, or there could be
 * several bion, each with several bio_vecs, which cover part of the page
 * Multiple bion are linked together on bi_next.  There may be extras
 * at the end of this list.  We ignore them.
 */
static void copy_data(int frombio, struct bio *bio,
		     struct page *page,
		     sector_t sector)
{
	char *pa = page_address(page);
	struct bio_vec *bvl;
	int i;
	int page_offset;

	if (bio->bi_sector >= sector)
		page_offset = (signed)(bio->bi_sector - sector) * 512;
	else
		page_offset = (signed)(sector - bio->bi_sector) * -512;
	bio_for_each_segment(bvl, bio, i) {
		int len = bio_iovec_idx(bio,i)->bv_len;
		int clen;
		int b_offset = 0;

		if (page_offset < 0) {
			b_offset = -page_offset;
			page_offset += b_offset;
			len -= b_offset;
		}

		if (len > 0 && page_offset + len > STRIPE_SIZE)
			clen = STRIPE_SIZE - page_offset;
		else clen = len;

		if (clen > 0) {
			char *ba = __bio_kmap_atomic(bio, i, KM_USER0);
			if (frombio)
				memcpy(pa+page_offset, ba+b_offset, clen);
			else
				memcpy(ba+b_offset, pa+page_offset, clen);
			__bio_kunmap_atomic(ba, KM_USER0);
		}
		if (clen < len) /* hit end of page */
			break;
		page_offset +=  len;
	}
}

#define check_xor() 	do { 						\
			   if (count == MAX_XOR_BLOCKS) {		\
				xor_block(count, STRIPE_SIZE, ptr);	\
				count = 1;				\
			   }						\
			} while(0)


static void compute_block(struct stripe_head *sh, int dd_idx)
{
	int i, count, disks = sh->disks;
	void *ptr[MAX_XOR_BLOCKS], *p;

	PRINTK("compute_block, stripe %llu, idx %d\n", 
		(unsigned long long)sh->sector, dd_idx);

	ptr[0] = page_address(sh->dev[dd_idx].page);
	memset(ptr[0], 0, STRIPE_SIZE);
	count = 1;
	for (i = disks ; i--; ) {
		if (i == dd_idx)
			continue;
		p = page_address(sh->dev[i].page);
		if (test_bit(R5_UPTODATE, &sh->dev[i].flags))
			ptr[count++] = p;
		else
			printk(KERN_ERR "compute_block() %d, stripe %llu, %d"
				" not present\n", dd_idx,
				(unsigned long long)sh->sector, i);

		check_xor();
	}
	if (count != 1)
		xor_block(count, STRIPE_SIZE, ptr);
	set_bit(R5_UPTODATE, &sh->dev[dd_idx].flags);
}

static void compute_parity5(struct stripe_head *sh, int method)
{
	raid5_conf_t *conf = sh->raid_conf;
	int i, pd_idx = sh->pd_idx, disks = sh->disks, count;
	void *ptr[MAX_XOR_BLOCKS];
	struct bio *chosen;

	PRINTK("compute_parity5, stripe %llu, method %d\n",
		(unsigned long long)sh->sector, method);

	count = 1;
	ptr[0] = page_address(sh->dev[pd_idx].page);
	switch(method) {
	case READ_MODIFY_WRITE:
		BUG_ON(!test_bit(R5_UPTODATE, &sh->dev[pd_idx].flags));
		for (i=disks ; i-- ;) {
			if (i==pd_idx)
				continue;
			if (sh->dev[i].towrite &&
			    test_bit(R5_UPTODATE, &sh->dev[i].flags)) {
				ptr[count++] = page_address(sh->dev[i].page);
				chosen = sh->dev[i].towrite;
				sh->dev[i].towrite = NULL;

				if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
					wake_up(&conf->wait_for_overlap);

				BUG_ON(sh->dev[i].written);
				sh->dev[i].written = chosen;
				check_xor();
			}
		}
		break;
	case RECONSTRUCT_WRITE:
		memset(ptr[0], 0, STRIPE_SIZE);
		for (i= disks; i-- ;)
			if (i!=pd_idx && sh->dev[i].towrite) {
				chosen = sh->dev[i].towrite;
				sh->dev[i].towrite = NULL;

				if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
					wake_up(&conf->wait_for_overlap);

				BUG_ON(sh->dev[i].written);
				sh->dev[i].written = chosen;
			}
		break;
	case CHECK_PARITY:
		break;
	}
	if (count>1) {
		xor_block(count, STRIPE_SIZE, ptr);
		count = 1;
	}
	
	for (i = disks; i--;)
		if (sh->dev[i].written) {
			sector_t sector = sh->dev[i].sector;
			struct bio *wbi = sh->dev[i].written;
			while (wbi && wbi->bi_sector < sector + STRIPE_SECTORS) {
				copy_data(1, wbi, sh->dev[i].page, sector);
				wbi = r5_next_bio(wbi, sector);
			}

			set_bit(R5_LOCKED, &sh->dev[i].flags);
			set_bit(R5_UPTODATE, &sh->dev[i].flags);
		}

	switch(method) {
	case RECONSTRUCT_WRITE:
	case CHECK_PARITY:
		for (i=disks; i--;)
			if (i != pd_idx) {
				ptr[count++] = page_address(sh->dev[i].page);
				check_xor();
			}
		break;
	case READ_MODIFY_WRITE:
		for (i = disks; i--;)
			if (sh->dev[i].written) {
				ptr[count++] = page_address(sh->dev[i].page);
				check_xor();
			}
	}
	if (count != 1)
		xor_block(count, STRIPE_SIZE, ptr);
	
	if (method != CHECK_PARITY) {
		set_bit(R5_UPTODATE, &sh->dev[pd_idx].flags);
		set_bit(R5_LOCKED,   &sh->dev[pd_idx].flags);
	} else
		clear_bit(R5_UPTODATE, &sh->dev[pd_idx].flags);
}

static void compute_parity6(struct stripe_head *sh, int method)
{
	raid6_conf_t *conf = sh->raid_conf;
	int i, pd_idx = sh->pd_idx, qd_idx, d0_idx, disks = sh->disks, count;
	struct bio *chosen;
	/**** FIX THIS: This could be very bad if disks is close to 256 ****/
	void *ptrs[disks];

	qd_idx = raid6_next_disk(pd_idx, disks);
	d0_idx = raid6_next_disk(qd_idx, disks);

	PRINTK("compute_parity, stripe %llu, method %d\n",
		(unsigned long long)sh->sector, method);

	switch(method) {
	case READ_MODIFY_WRITE:
		BUG();		/* READ_MODIFY_WRITE N/A for RAID-6 */
	case RECONSTRUCT_WRITE:
		for (i= disks; i-- ;)
			if ( i != pd_idx && i != qd_idx && sh->dev[i].towrite ) {
				chosen = sh->dev[i].towrite;
				sh->dev[i].towrite = NULL;

				if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
					wake_up(&conf->wait_for_overlap);

				BUG_ON(sh->dev[i].written);
				sh->dev[i].written = chosen;
			}
		break;
	case CHECK_PARITY:
		BUG();		/* Not implemented yet */
	}

	for (i = disks; i--;)
		if (sh->dev[i].written) {
			sector_t sector = sh->dev[i].sector;
			struct bio *wbi = sh->dev[i].written;
			while (wbi && wbi->bi_sector < sector + STRIPE_SECTORS) {
				copy_data(1, wbi, sh->dev[i].page, sector);
				wbi = r5_next_bio(wbi, sector);
			}

			set_bit(R5_LOCKED, &sh->dev[i].flags);
			set_bit(R5_UPTODATE, &sh->dev[i].flags);
		}

//	switch(method) {
//	case RECONSTRUCT_WRITE:
//	case CHECK_PARITY:
//	case UPDATE_PARITY:
		/* Note that unlike RAID-5, the ordering of the disks matters greatly. */
		/* FIX: Is this ordering of drives even remotely optimal? */
		count = 0;
		i = d0_idx;
		do {
			ptrs[count++] = page_address(sh->dev[i].page);
			if (count <= disks-2 && !test_bit(R5_UPTODATE, &sh->dev[i].flags))
				printk("block %d/%d not uptodate on parity calc\n", i,count);
			i = raid6_next_disk(i, disks);
		} while ( i != d0_idx );
//		break;
//	}

	raid6_call.gen_syndrome(disks, STRIPE_SIZE, ptrs);

	switch(method) {
	case RECONSTRUCT_WRITE:
		set_bit(R5_UPTODATE, &sh->dev[pd_idx].flags);
		set_bit(R5_UPTODATE, &sh->dev[qd_idx].flags);
		set_bit(R5_LOCKED,   &sh->dev[pd_idx].flags);
		set_bit(R5_LOCKED,   &sh->dev[qd_idx].flags);
		break;
	case UPDATE_PARITY:
		set_bit(R5_UPTODATE, &sh->dev[pd_idx].flags);
		set_bit(R5_UPTODATE, &sh->dev[qd_idx].flags);
		break;
	}
}


/* Compute one missing block */
static void compute_block_1(struct stripe_head *sh, int dd_idx, int nozero)
{
	int i, count, disks = sh->disks;
	void *ptr[MAX_XOR_BLOCKS], *p;
	int pd_idx = sh->pd_idx;
	int qd_idx = raid6_next_disk(pd_idx, disks);

	PRINTK("compute_block_1, stripe %llu, idx %d\n",
		(unsigned long long)sh->sector, dd_idx);

	if ( dd_idx == qd_idx ) {
		/* We're actually computing the Q drive */
		compute_parity6(sh, UPDATE_PARITY);
	} else {
		ptr[0] = page_address(sh->dev[dd_idx].page);
		if (!nozero) memset(ptr[0], 0, STRIPE_SIZE);
		count = 1;
		for (i = disks ; i--; ) {
			if (i == dd_idx || i == qd_idx)
				continue;
			p = page_address(sh->dev[i].page);
			if (test_bit(R5_UPTODATE, &sh->dev[i].flags))
				ptr[count++] = p;
			else
				printk("compute_block() %d, stripe %llu, %d"
				       " not present\n", dd_idx,
				       (unsigned long long)sh->sector, i);

			check_xor();
		}
		if (count != 1)
			xor_block(count, STRIPE_SIZE, ptr);
		if (!nozero) set_bit(R5_UPTODATE, &sh->dev[dd_idx].flags);
		else clear_bit(R5_UPTODATE, &sh->dev[dd_idx].flags);
	}
}

/* Compute two missing blocks */
static void compute_block_2(struct stripe_head *sh, int dd_idx1, int dd_idx2)
{
	int i, count, disks = sh->disks;
	int pd_idx = sh->pd_idx;
	int qd_idx = raid6_next_disk(pd_idx, disks);
	int d0_idx = raid6_next_disk(qd_idx, disks);
	int faila, failb;

	/* faila and failb are disk numbers relative to d0_idx */
	/* pd_idx become disks-2 and qd_idx become disks-1 */
	faila = (dd_idx1 < d0_idx) ? dd_idx1+(disks-d0_idx) : dd_idx1-d0_idx;
	failb = (dd_idx2 < d0_idx) ? dd_idx2+(disks-d0_idx) : dd_idx2-d0_idx;

	BUG_ON(faila == failb);
	if ( failb < faila ) { int tmp = faila; faila = failb; failb = tmp; }

	PRINTK("compute_block_2, stripe %llu, idx %d,%d (%d,%d)\n",
	       (unsigned long long)sh->sector, dd_idx1, dd_idx2, faila, failb);

	if ( failb == disks-1 ) {
		/* Q disk is one of the missing disks */
		if ( faila == disks-2 ) {
			/* Missing P+Q, just recompute */
			compute_parity6(sh, UPDATE_PARITY);
			return;
		} else {
			/* We're missing D+Q; recompute D from P */
			compute_block_1(sh, (dd_idx1 == qd_idx) ? dd_idx2 : dd_idx1, 0);
			compute_parity6(sh, UPDATE_PARITY); /* Is this necessary? */
			return;
		}
	}

	/* We're missing D+P or D+D; build pointer table */
	{
		/**** FIX THIS: This could be very bad if disks is close to 256 ****/
		void *ptrs[disks];

		count = 0;
		i = d0_idx;
		do {
			ptrs[count++] = page_address(sh->dev[i].page);
			i = raid6_next_disk(i, disks);
			if (i != dd_idx1 && i != dd_idx2 &&
			    !test_bit(R5_UPTODATE, &sh->dev[i].flags))
				printk("compute_2 with missing block %d/%d\n", count, i);
		} while ( i != d0_idx );

		if ( failb == disks-2 ) {
			/* We're missing D+P. */
			raid6_datap_recov(disks, STRIPE_SIZE, faila, ptrs);
		} else {
			/* We're missing D+D. */
			raid6_2data_recov(disks, STRIPE_SIZE, faila, failb, ptrs);
		}

		/* Both the above update both missing blocks */
		set_bit(R5_UPTODATE, &sh->dev[dd_idx1].flags);
		set_bit(R5_UPTODATE, &sh->dev[dd_idx2].flags);
	}
}



/*
 * Each stripe/dev can have one or more bion attached.
 * toread/towrite point to the first in a chain.
 * The bi_next chain must be in order.
 */
static int add_stripe_bio(struct stripe_head *sh, struct bio *bi, int dd_idx, int forwrite)
{
	struct bio **bip;
	raid5_conf_t *conf = sh->raid_conf;
	int firstwrite=0;

	PRINTK("adding bh b#%llu to stripe s#%llu\n",
		(unsigned long long)bi->bi_sector,
		(unsigned long long)sh->sector);


	spin_lock(&sh->lock);
	spin_lock_irq(&conf->device_lock);
	if (forwrite) {
		bip = &sh->dev[dd_idx].towrite;
		if (*bip == NULL && sh->dev[dd_idx].written == NULL)
			firstwrite = 1;
	} else
		bip = &sh->dev[dd_idx].toread;
	while (*bip && (*bip)->bi_sector < bi->bi_sector) {
		if ((*bip)->bi_sector + ((*bip)->bi_size >> 9) > bi->bi_sector)
			goto overlap;
		bip = & (*bip)->bi_next;
	}
	if (*bip && (*bip)->bi_sector < bi->bi_sector + ((bi->bi_size)>>9))
		goto overlap;

	BUG_ON(*bip && bi->bi_next && (*bip) != bi->bi_next);
	if (*bip)
		bi->bi_next = *bip;
	*bip = bi;
	bi->bi_phys_segments ++;
	spin_unlock_irq(&conf->device_lock);
	spin_unlock(&sh->lock);

	PRINTK("added bi b#%llu to stripe s#%llu, disk %d.\n",
		(unsigned long long)bi->bi_sector,
		(unsigned long long)sh->sector, dd_idx);

	if (conf->mddev->bitmap && firstwrite) {
		bitmap_startwrite(conf->mddev->bitmap, sh->sector,
				  STRIPE_SECTORS, 0);
		sh->bm_seq = conf->seq_flush+1;
		set_bit(STRIPE_BIT_DELAY, &sh->state);
	}

	if (forwrite) {
		/* check if page is covered */
		sector_t sector = sh->dev[dd_idx].sector;
		for (bi=sh->dev[dd_idx].towrite;
		     sector < sh->dev[dd_idx].sector + STRIPE_SECTORS &&
			     bi && bi->bi_sector <= sector;
		     bi = r5_next_bio(bi, sh->dev[dd_idx].sector)) {
			if (bi->bi_sector + (bi->bi_size>>9) >= sector)
				sector = bi->bi_sector + (bi->bi_size>>9);
		}
		if (sector >= sh->dev[dd_idx].sector + STRIPE_SECTORS)
			set_bit(R5_OVERWRITE, &sh->dev[dd_idx].flags);
	}
	return 1;

 overlap:
	set_bit(R5_Overlap, &sh->dev[dd_idx].flags);
	spin_unlock_irq(&conf->device_lock);
	spin_unlock(&sh->lock);
	return 0;
}

static void end_reshape(raid5_conf_t *conf);

static int page_is_zero(struct page *p)
{
	char *a = page_address(p);
	return ((*(u32*)a) == 0 &&
		memcmp(a, a+4, STRIPE_SIZE-4)==0);
}

static int stripe_to_pdidx(sector_t stripe, raid5_conf_t *conf, int disks)
{
	int sectors_per_chunk = conf->chunk_size >> 9;
	int pd_idx, dd_idx;
	int chunk_offset = sector_div(stripe, sectors_per_chunk);

	raid5_compute_sector(stripe * (disks - conf->max_degraded)
			     *sectors_per_chunk + chunk_offset,
			     disks, disks - conf->max_degraded,
			     &dd_idx, &pd_idx, conf);
	return pd_idx;
}


/*
 * handle_stripe - do things to a stripe.
 *
 * We lock the stripe and then examine the state of various bits
 * to see what needs to be done.
 * Possible results:
 *    return some read request which now have data
 *    return some write requests which are safely on disc
 *    schedule a read on some buffers
 *    schedule a write of some buffers
 *    return confirmation of parity correctness
 *
 * Parity calculations are done inside the stripe lock
 * buffers are taken off read_list or write_list, and bh_cache buffers
 * get BH_Lock set before the stripe lock is released.
 *
 */
 
static void handle_stripe5(struct stripe_head *sh)
{
	raid5_conf_t *conf = sh->raid_conf;
	int disks = sh->disks;
	struct bio *return_bi= NULL;
	struct bio *bi;
	int i;
	int syncing, expanding, expanded;
	int locked=0, uptodate=0, to_read=0, to_write=0, failed=0, written=0;
	int non_overwrite = 0;
	int failed_num=0;
	struct r5dev *dev;

	PRINTK("handling stripe %llu, cnt=%d, pd_idx=%d\n",
		(unsigned long long)sh->sector, atomic_read(&sh->count),
		sh->pd_idx);

	spin_lock(&sh->lock);
	clear_bit(STRIPE_HANDLE, &sh->state);
	clear_bit(STRIPE_DELAYED, &sh->state);

	syncing = test_bit(STRIPE_SYNCING, &sh->state);
	expanding = test_bit(STRIPE_EXPAND_SOURCE, &sh->state);
	expanded = test_bit(STRIPE_EXPAND_READY, &sh->state);
	/* Now to look around and see what can be done */

	rcu_read_lock();
	for (i=disks; i--; ) {
		mdk_rdev_t *rdev;
		dev = &sh->dev[i];
		clear_bit(R5_Insync, &dev->flags);

		PRINTK("check %d: state 0x%lx read %p write %p written %p\n",
			i, dev->flags, dev->toread, dev->towrite, dev->written);
		/* maybe we can reply to a read */
		if (test_bit(R5_UPTODATE, &dev->flags) && dev->toread) {
			struct bio *rbi, *rbi2;
			PRINTK("Return read for disc %d\n", i);
			spin_lock_irq(&conf->device_lock);
			rbi = dev->toread;
			dev->toread = NULL;
			if (test_and_clear_bit(R5_Overlap, &dev->flags))
				wake_up(&conf->wait_for_overlap);
			spin_unlock_irq(&conf->device_lock);
			while (rbi && rbi->bi_sector < dev->sector + STRIPE_SECTORS) {
				copy_data(0, rbi, dev->page, dev->sector);
				rbi2 = r5_next_bio(rbi, dev->sector);
				spin_lock_irq(&conf->device_lock);
				if (--rbi->bi_phys_segments == 0) {
					rbi->bi_next = return_bi;
					return_bi = rbi;
				}
				spin_unlock_irq(&conf->device_lock);
				rbi = rbi2;
			}
		}

		/* now count some things */
		if (test_bit(R5_LOCKED, &dev->flags)) locked++;
		if (test_bit(R5_UPTODATE, &dev->flags)) uptodate++;

		
		if (dev->toread) to_read++;
		if (dev->towrite) {
			to_write++;
			if (!test_bit(R5_OVERWRITE, &dev->flags))
				non_overwrite++;
		}
		if (dev->written) written++;
		rdev = rcu_dereference(conf->disks[i].rdev);
		if (!rdev || !test_bit(In_sync, &rdev->flags)) {
			/* The ReadError flag will just be confusing now */
			clear_bit(R5_ReadError, &dev->flags);
			clear_bit(R5_ReWrite, &dev->flags);
		}
		if (!rdev || !test_bit(In_sync, &rdev->flags)
		    || test_bit(R5_ReadError, &dev->flags)) {
			failed++;
			failed_num = i;
		} else
			set_bit(R5_Insync, &dev->flags);
	}
	rcu_read_unlock();
	PRINTK("locked=%d uptodate=%d to_read=%d"
		" to_write=%d failed=%d failed_num=%d\n",
		locked, uptodate, to_read, to_write, failed, failed_num);
	/* check if the array has lost two devices and, if so, some requests might
	 * need to be failed
	 */
	if (failed > 1 && to_read+to_write+written) {
		for (i=disks; i--; ) {
			int bitmap_end = 0;

			if (test_bit(R5_ReadError, &sh->dev[i].flags)) {
				mdk_rdev_t *rdev;
				rcu_read_lock();
				rdev = rcu_dereference(conf->disks[i].rdev);
				if (rdev && test_bit(In_sync, &rdev->flags))
					/* multiple read failures in one stripe */
					md_error(conf->mddev, rdev);
				rcu_read_unlock();
			}

			spin_lock_irq(&conf->device_lock);
			/* fail all writes first */
			bi = sh->dev[i].towrite;
			sh->dev[i].towrite = NULL;
			if (bi) { to_write--; bitmap_end = 1; }

			if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
				wake_up(&conf->wait_for_overlap);

			while (bi && bi->bi_sector < sh->dev[i].sector + STRIPE_SECTORS){
				struct bio *nextbi = r5_next_bio(bi, sh->dev[i].sector);
				clear_bit(BIO_UPTODATE, &bi->bi_flags);
				if (--bi->bi_phys_segments == 0) {
					md_write_end(conf->mddev);
					bi->bi_next = return_bi;
					return_bi = bi;
				}
				bi = nextbi;
			}
			/* and fail all 'written' */
			bi = sh->dev[i].written;
			sh->dev[i].written = NULL;
			if (bi) bitmap_end = 1;
			while (bi && bi->bi_sector < sh->dev[i].sector + STRIPE_SECTORS) {
				struct bio *bi2 = r5_next_bio(bi, sh->dev[i].sector);
				clear_bit(BIO_UPTODATE, &bi->bi_flags);
				if (--bi->bi_phys_segments == 0) {
					md_write_end(conf->mddev);
					bi->bi_next = return_bi;
					return_bi = bi;
				}
				bi = bi2;
			}

			/* fail any reads if this device is non-operational */
			if (!test_bit(R5_Insync, &sh->dev[i].flags) ||
			    test_bit(R5_ReadError, &sh->dev[i].flags)) {
				bi = sh->dev[i].toread;
				sh->dev[i].toread = NULL;
				if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
					wake_up(&conf->wait_for_overlap);
				if (bi) to_read--;
				while (bi && bi->bi_sector < sh->dev[i].sector + STRIPE_SECTORS){
					struct bio *nextbi = r5_next_bio(bi, sh->dev[i].sector);
					clear_bit(BIO_UPTODATE, &bi->bi_flags);
					if (--bi->bi_phys_segments == 0) {
						bi->bi_next = return_bi;
						return_bi = bi;
					}
					bi = nextbi;
				}
			}
			spin_unlock_irq(&conf->device_lock);
			if (bitmap_end)
				bitmap_endwrite(conf->mddev->bitmap, sh->sector,
						STRIPE_SECTORS, 0, 0);
		}
	}
	if (failed > 1 && syncing) {
		md_done_sync(conf->mddev, STRIPE_SECTORS,0);
		clear_bit(STRIPE_SYNCING, &sh->state);
		syncing = 0;
	}

	/* might be able to return some write requests if the parity block
	 * is safe, or on a failed drive
	 */
	dev = &sh->dev[sh->pd_idx];
	if ( written &&
	     ( (test_bit(R5_Insync, &dev->flags) && !test_bit(R5_LOCKED, &dev->flags) &&
		test_bit(R5_UPTODATE, &dev->flags))
	       || (failed == 1 && failed_num == sh->pd_idx))
	    ) {
	    /* any written block on an uptodate or failed drive can be returned.
	     * Note that if we 'wrote' to a failed drive, it will be UPTODATE, but 
	     * never LOCKED, so we don't need to test 'failed' directly.
	     */
	    for (i=disks; i--; )
		if (sh->dev[i].written) {
		    dev = &sh->dev[i];
		    if (!test_bit(R5_LOCKED, &dev->flags) &&
			 test_bit(R5_UPTODATE, &dev->flags) ) {
			/* We can return any write requests */
			    struct bio *wbi, *wbi2;
			    int bitmap_end = 0;
			    PRINTK("Return write for disc %d\n", i);
			    spin_lock_irq(&conf->device_lock);
			    wbi = dev->written;
			    dev->written = NULL;
			    while (wbi && wbi->bi_sector < dev->sector + STRIPE_SECTORS) {
				    wbi2 = r5_next_bio(wbi, dev->sector);
				    if (--wbi->bi_phys_segments == 0) {
					    md_write_end(conf->mddev);
					    wbi->bi_next = return_bi;
					    return_bi = wbi;
				    }
				    wbi = wbi2;
			    }
			    if (dev->towrite == NULL)
				    bitmap_end = 1;
			    spin_unlock_irq(&conf->device_lock);
			    if (bitmap_end)
				    bitmap_endwrite(conf->mddev->bitmap, sh->sector,
						    STRIPE_SECTORS,
						    !test_bit(STRIPE_DEGRADED, &sh->state), 0);
		    }
		}
	}

	/* Now we might consider reading some blocks, either to check/generate
	 * parity, or to satisfy requests
	 * or to load a block that is being partially written.
	 */
	if (to_read || non_overwrite || (syncing && (uptodate < disks)) || expanding) {
		for (i=disks; i--;) {
			dev = &sh->dev[i];
			if (!test_bit(R5_LOCKED, &dev->flags) && !test_bit(R5_UPTODATE, &dev->flags) &&
			    (dev->toread ||
			     (dev->towrite && !test_bit(R5_OVERWRITE, &dev->flags)) ||
			     syncing ||
			     expanding ||
			     (failed && (sh->dev[failed_num].toread ||
					 (sh->dev[failed_num].towrite && !test_bit(R5_OVERWRITE, &sh->dev[failed_num].flags))))
				    )
				) {
				/* we would like to get this block, possibly
				 * by computing it, but we might not be able to
				 */
				if (uptodate == disks-1) {
					PRINTK("Computing block %d\n", i);
					compute_block(sh, i);
					uptodate++;
				} else if (test_bit(R5_Insync, &dev->flags)) {
					set_bit(R5_LOCKED, &dev->flags);
					set_bit(R5_Wantread, &dev->flags);
					locked++;
					PRINTK("Reading block %d (sync=%d)\n", 
						i, syncing);
				}
			}
		}
		set_bit(STRIPE_HANDLE, &sh->state);
	}

	/* now to consider writing and what else, if anything should be read */
	if (to_write) {
		int rmw=0, rcw=0;
		for (i=disks ; i--;) {
			/* would I have to read this buffer for read_modify_write */
			dev = &sh->dev[i];
			if ((dev->towrite || i == sh->pd_idx) &&
			    (!test_bit(R5_LOCKED, &dev->flags) 
				    ) &&
			    !test_bit(R5_UPTODATE, &dev->flags)) {
				if (test_bit(R5_Insync, &dev->flags)
/*				    && !(!mddev->insync && i == sh->pd_idx) */
					)
					rmw++;
				else rmw += 2*disks;  /* cannot read it */
			}
			/* Would I have to read this buffer for reconstruct_write */
			if (!test_bit(R5_OVERWRITE, &dev->flags) && i != sh->pd_idx &&
			    (!test_bit(R5_LOCKED, &dev->flags) 
				    ) &&
			    !test_bit(R5_UPTODATE, &dev->flags)) {
				if (test_bit(R5_Insync, &dev->flags)) rcw++;
				else rcw += 2*disks;
			}
		}
		PRINTK("for sector %llu, rmw=%d rcw=%d\n", 
			(unsigned long long)sh->sector, rmw, rcw);
		set_bit(STRIPE_HANDLE, &sh->state);
		if (rmw < rcw && rmw > 0)
			/* prefer read-modify-write, but need to get some data */
			for (i=disks; i--;) {
				dev = &sh->dev[i];
				if ((dev->towrite || i == sh->pd_idx) &&
				    !test_bit(R5_LOCKED, &dev->flags) && !test_bit(R5_UPTODATE, &dev->flags) &&
				    test_bit(R5_Insync, &dev->flags)) {
					if (test_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
					{
						PRINTK("Read_old block %d for r-m-w\n", i);
						set_bit(R5_LOCKED, &dev->flags);
						set_bit(R5_Wantread, &dev->flags);
						locked++;
					} else {
						set_bit(STRIPE_DELAYED, &sh->state);
						set_bit(STRIPE_HANDLE, &sh->state);
					}
				}
			}
		if (rcw <= rmw && rcw > 0)
			/* want reconstruct write, but need to get some data */
			for (i=disks; i--;) {
				dev = &sh->dev[i];
				if (!test_bit(R5_OVERWRITE, &dev->flags) && i != sh->pd_idx &&
				    !test_bit(R5_LOCKED, &dev->flags) && !test_bit(R5_UPTODATE, &dev->flags) &&
				    test_bit(R5_Insync, &dev->flags)) {
					if (test_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
					{
						PRINTK("Read_old block %d for Reconstruct\n", i);
						set_bit(R5_LOCKED, &dev->flags);
						set_bit(R5_Wantread, &dev->flags);
						locked++;
					} else {
						set_bit(STRIPE_DELAYED, &sh->state);
						set_bit(STRIPE_HANDLE, &sh->state);
					}
				}
			}
		/* now if nothing is locked, and if we have enough data, we can start a write request */
		if (locked == 0 && (rcw == 0 ||rmw == 0) &&
		    !test_bit(STRIPE_BIT_DELAY, &sh->state)) {
			PRINTK("Computing parity...\n");
			compute_parity5(sh, rcw==0 ? RECONSTRUCT_WRITE : READ_MODIFY_WRITE);
			/* now every locked buffer is ready to be written */
			for (i=disks; i--;)
				if (test_bit(R5_LOCKED, &sh->dev[i].flags)) {
					PRINTK("Writing block %d\n", i);
					locked++;
					set_bit(R5_Wantwrite, &sh->dev[i].flags);
					if (!test_bit(R5_Insync, &sh->dev[i].flags)
					    || (i==sh->pd_idx && failed == 0))
						set_bit(STRIPE_INSYNC, &sh->state);
				}
			if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
				atomic_dec(&conf->preread_active_stripes);
				if (atomic_read(&conf->preread_active_stripes) < IO_THRESHOLD)
					md_wakeup_thread(conf->mddev->thread);
			}
		}
	}

	/* maybe we need to check and possibly fix the parity for this stripe
	 * Any reads will already have been scheduled, so we just see if enough data
	 * is available
	 */
	if (syncing && locked == 0 &&
	    !test_bit(STRIPE_INSYNC, &sh->state)) {
		set_bit(STRIPE_HANDLE, &sh->state);
		if (failed == 0) {
			BUG_ON(uptodate != disks);
			compute_parity5(sh, CHECK_PARITY);
			uptodate--;
			if (page_is_zero(sh->dev[sh->pd_idx].page)) {
				/* parity is correct (on disc, not in buffer any more) */
				set_bit(STRIPE_INSYNC, &sh->state);
			} else {
				conf->mddev->resync_mismatches += STRIPE_SECTORS;
				if (test_bit(MD_RECOVERY_CHECK, &conf->mddev->recovery))
					/* don't try to repair!! */
					set_bit(STRIPE_INSYNC, &sh->state);
				else {
					compute_block(sh, sh->pd_idx);
					uptodate++;
				}
			}
		}
		if (!test_bit(STRIPE_INSYNC, &sh->state)) {
			/* either failed parity check, or recovery is happening */
			if (failed==0)
				failed_num = sh->pd_idx;
			dev = &sh->dev[failed_num];
			BUG_ON(!test_bit(R5_UPTODATE, &dev->flags));
			BUG_ON(uptodate != disks);

			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantwrite, &dev->flags);
			clear_bit(STRIPE_DEGRADED, &sh->state);
			locked++;
			set_bit(STRIPE_INSYNC, &sh->state);
		}
	}
	if (syncing && locked == 0 && test_bit(STRIPE_INSYNC, &sh->state)) {
		md_done_sync(conf->mddev, STRIPE_SECTORS,1);
		clear_bit(STRIPE_SYNCING, &sh->state);
	}

	/* If the failed drive is just a ReadError, then we might need to progress
	 * the repair/check process
	 */
	if (failed == 1 && ! conf->mddev->ro &&
	    test_bit(R5_ReadError, &sh->dev[failed_num].flags)
	    && !test_bit(R5_LOCKED, &sh->dev[failed_num].flags)
	    && test_bit(R5_UPTODATE, &sh->dev[failed_num].flags)
		) {
		dev = &sh->dev[failed_num];
		if (!test_bit(R5_ReWrite, &dev->flags)) {
			set_bit(R5_Wantwrite, &dev->flags);
			set_bit(R5_ReWrite, &dev->flags);
			set_bit(R5_LOCKED, &dev->flags);
			locked++;
		} else {
			/* let's read it back */
			set_bit(R5_Wantread, &dev->flags);
			set_bit(R5_LOCKED, &dev->flags);
			locked++;
		}
	}

	if (expanded && test_bit(STRIPE_EXPANDING, &sh->state)) {
		/* Need to write out all blocks after computing parity */
		sh->disks = conf->raid_disks;
		sh->pd_idx = stripe_to_pdidx(sh->sector, conf, conf->raid_disks);
		compute_parity5(sh, RECONSTRUCT_WRITE);
		for (i= conf->raid_disks; i--;) {
			set_bit(R5_LOCKED, &sh->dev[i].flags);
			locked++;
			set_bit(R5_Wantwrite, &sh->dev[i].flags);
		}
		clear_bit(STRIPE_EXPANDING, &sh->state);
	} else if (expanded) {
		clear_bit(STRIPE_EXPAND_READY, &sh->state);
		atomic_dec(&conf->reshape_stripes);
		wake_up(&conf->wait_for_overlap);
		md_done_sync(conf->mddev, STRIPE_SECTORS, 1);
	}

	if (expanding && locked == 0) {
		/* We have read all the blocks in this stripe and now we need to
		 * copy some of them into a target stripe for expand.
		 */
		clear_bit(STRIPE_EXPAND_SOURCE, &sh->state);
		for (i=0; i< sh->disks; i++)
			if (i != sh->pd_idx) {
				int dd_idx, pd_idx, j;
				struct stripe_head *sh2;

				sector_t bn = compute_blocknr(sh, i);
				sector_t s = raid5_compute_sector(bn, conf->raid_disks,
								  conf->raid_disks-1,
								  &dd_idx, &pd_idx, conf);
				sh2 = get_active_stripe(conf, s, conf->raid_disks, pd_idx, 1);
				if (sh2 == NULL)
					/* so far only the early blocks of this stripe
					 * have been requested.  When later blocks
					 * get requested, we will try again
					 */
					continue;
				if(!test_bit(STRIPE_EXPANDING, &sh2->state) ||
				   test_bit(R5_Expanded, &sh2->dev[dd_idx].flags)) {
					/* must have already done this block */
					release_stripe(sh2);
					continue;
				}
				memcpy(page_address(sh2->dev[dd_idx].page),
				       page_address(sh->dev[i].page),
				       STRIPE_SIZE);
				set_bit(R5_Expanded, &sh2->dev[dd_idx].flags);
				set_bit(R5_UPTODATE, &sh2->dev[dd_idx].flags);
				for (j=0; j<conf->raid_disks; j++)
					if (j != sh2->pd_idx &&
					    !test_bit(R5_Expanded, &sh2->dev[j].flags))
						break;
				if (j == conf->raid_disks) {
					set_bit(STRIPE_EXPAND_READY, &sh2->state);
					set_bit(STRIPE_HANDLE, &sh2->state);
				}
				release_stripe(sh2);
			}
	}

	spin_unlock(&sh->lock);

	while ((bi=return_bi)) {
		int bytes = bi->bi_size;

		return_bi = bi->bi_next;
		bi->bi_next = NULL;
		bi->bi_size = 0;
		bi->bi_end_io(bi, bytes,
			      test_bit(BIO_UPTODATE, &bi->bi_flags)
			        ? 0 : -EIO);
	}
	for (i=disks; i-- ;) {
		int rw;
		struct bio *bi;
		mdk_rdev_t *rdev;
		if (test_and_clear_bit(R5_Wantwrite, &sh->dev[i].flags))
			rw = WRITE;
		else if (test_and_clear_bit(R5_Wantread, &sh->dev[i].flags))
			rw = READ;
		else
			continue;
 
		bi = &sh->dev[i].req;
 
		bi->bi_rw = rw;
		if (rw == WRITE)
			bi->bi_end_io = raid5_end_write_request;
		else
			bi->bi_end_io = raid5_end_read_request;
 
		rcu_read_lock();
		rdev = rcu_dereference(conf->disks[i].rdev);
		if (rdev && test_bit(Faulty, &rdev->flags))
			rdev = NULL;
		if (rdev)
			atomic_inc(&rdev->nr_pending);
		rcu_read_unlock();
 
		if (rdev) {
			if (syncing || expanding || expanded)
				md_sync_acct(rdev->bdev, STRIPE_SECTORS);

			bi->bi_bdev = rdev->bdev;
			PRINTK("for %llu schedule op %ld on disc %d\n",
				(unsigned long long)sh->sector, bi->bi_rw, i);
			atomic_inc(&sh->count);
			bi->bi_sector = sh->sector + rdev->data_offset;
			bi->bi_flags = 1 << BIO_UPTODATE;
			bi->bi_vcnt = 1;	
			bi->bi_max_vecs = 1;
			bi->bi_idx = 0;
			bi->bi_io_vec = &sh->dev[i].vec;
			bi->bi_io_vec[0].bv_len = STRIPE_SIZE;
			bi->bi_io_vec[0].bv_offset = 0;
			bi->bi_size = STRIPE_SIZE;
			bi->bi_next = NULL;
			if (rw == WRITE &&
			    test_bit(R5_ReWrite, &sh->dev[i].flags))
				atomic_add(STRIPE_SECTORS, &rdev->corrected_errors);
			generic_make_request(bi);
		} else {
			if (rw == WRITE)
				set_bit(STRIPE_DEGRADED, &sh->state);
			PRINTK("skip op %ld on disc %d for sector %llu\n",
				bi->bi_rw, i, (unsigned long long)sh->sector);
			clear_bit(R5_LOCKED, &sh->dev[i].flags);
			set_bit(STRIPE_HANDLE, &sh->state);
		}
	}
}

static void handle_stripe6(struct stripe_head *sh, struct page *tmp_page)
{
	raid6_conf_t *conf = sh->raid_conf;
	int disks = sh->disks;
	struct bio *return_bi= NULL;
	struct bio *bi;
	int i;
	int syncing, expanding, expanded;
	int locked=0, uptodate=0, to_read=0, to_write=0, failed=0, written=0;
	int non_overwrite = 0;
	int failed_num[2] = {0, 0};
	struct r5dev *dev, *pdev, *qdev;
	int pd_idx = sh->pd_idx;
	int qd_idx = raid6_next_disk(pd_idx, disks);
	int p_failed, q_failed;

	PRINTK("handling stripe %llu, state=%#lx cnt=%d, pd_idx=%d, qd_idx=%d\n",
	       (unsigned long long)sh->sector, sh->state, atomic_read(&sh->count),
	       pd_idx, qd_idx);

	spin_lock(&sh->lock);
	clear_bit(STRIPE_HANDLE, &sh->state);
	clear_bit(STRIPE_DELAYED, &sh->state);

	syncing = test_bit(STRIPE_SYNCING, &sh->state);
	expanding = test_bit(STRIPE_EXPAND_SOURCE, &sh->state);
	expanded = test_bit(STRIPE_EXPAND_READY, &sh->state);
	/* Now to look around and see what can be done */

	rcu_read_lock();
	for (i=disks; i--; ) {
		mdk_rdev_t *rdev;
		dev = &sh->dev[i];
		clear_bit(R5_Insync, &dev->flags);

		PRINTK("check %d: state 0x%lx read %p write %p written %p\n",
			i, dev->flags, dev->toread, dev->towrite, dev->written);
		/* maybe we can reply to a read */
		if (test_bit(R5_UPTODATE, &dev->flags) && dev->toread) {
			struct bio *rbi, *rbi2;
			PRINTK("Return read for disc %d\n", i);
			spin_lock_irq(&conf->device_lock);
			rbi = dev->toread;
			dev->toread = NULL;
			if (test_and_clear_bit(R5_Overlap, &dev->flags))
				wake_up(&conf->wait_for_overlap);
			spin_unlock_irq(&conf->device_lock);
			while (rbi && rbi->bi_sector < dev->sector + STRIPE_SECTORS) {
				copy_data(0, rbi, dev->page, dev->sector);
				rbi2 = r5_next_bio(rbi, dev->sector);
				spin_lock_irq(&conf->device_lock);
				if (--rbi->bi_phys_segments == 0) {
					rbi->bi_next = return_bi;
					return_bi = rbi;
				}
				spin_unlock_irq(&conf->device_lock);
				rbi = rbi2;
			}
		}

		/* now count some things */
		if (test_bit(R5_LOCKED, &dev->flags)) locked++;
		if (test_bit(R5_UPTODATE, &dev->flags)) uptodate++;


		if (dev->toread) to_read++;
		if (dev->towrite) {
			to_write++;
			if (!test_bit(R5_OVERWRITE, &dev->flags))
				non_overwrite++;
		}
		if (dev->written) written++;
		rdev = rcu_dereference(conf->disks[i].rdev);
		if (!rdev || !test_bit(In_sync, &rdev->flags)) {
			/* The ReadError flag will just be confusing now */
			clear_bit(R5_ReadError, &dev->flags);
			clear_bit(R5_ReWrite, &dev->flags);
		}
		if (!rdev || !test_bit(In_sync, &rdev->flags)
		    || test_bit(R5_ReadError, &dev->flags)) {
			if ( failed < 2 )
				failed_num[failed] = i;
			failed++;
		} else
			set_bit(R5_Insync, &dev->flags);
	}
	rcu_read_unlock();
	PRINTK("locked=%d uptodate=%d to_read=%d"
	       " to_write=%d failed=%d failed_num=%d,%d\n",
	       locked, uptodate, to_read, to_write, failed,
	       failed_num[0], failed_num[1]);
	/* check if the array has lost >2 devices and, if so, some requests might
	 * need to be failed
	 */
	if (failed > 2 && to_read+to_write+written) {
		for (i=disks; i--; ) {
			int bitmap_end = 0;

			if (test_bit(R5_ReadError, &sh->dev[i].flags)) {
				mdk_rdev_t *rdev;
				rcu_read_lock();
				rdev = rcu_dereference(conf->disks[i].rdev);
				if (rdev && test_bit(In_sync, &rdev->flags))
					/* multiple read failures in one stripe */
					md_error(conf->mddev, rdev);
				rcu_read_unlock();
			}

			spin_lock_irq(&conf->device_lock);
			/* fail all writes first */
			bi = sh->dev[i].towrite;
			sh->dev[i].towrite = NULL;
			if (bi) { to_write--; bitmap_end = 1; }

			if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
				wake_up(&conf->wait_for_overlap);

			while (bi && bi->bi_sector < sh->dev[i].sector + STRIPE_SECTORS){
				struct bio *nextbi = r5_next_bio(bi, sh->dev[i].sector);
				clear_bit(BIO_UPTODATE, &bi->bi_flags);
				if (--bi->bi_phys_segments == 0) {
					md_write_end(conf->mddev);
					bi->bi_next = return_bi;
					return_bi = bi;
				}
				bi = nextbi;
			}
			/* and fail all 'written' */
			bi = sh->dev[i].written;
			sh->dev[i].written = NULL;
			if (bi) bitmap_end = 1;
			while (bi && bi->bi_sector < sh->dev[i].sector + STRIPE_SECTORS) {
				struct bio *bi2 = r5_next_bio(bi, sh->dev[i].sector);
				clear_bit(BIO_UPTODATE, &bi->bi_flags);
				if (--bi->bi_phys_segments == 0) {
					md_write_end(conf->mddev);
					bi->bi_next = return_bi;
					return_bi = bi;
				}
				bi = bi2;
			}

			/* fail any reads if this device is non-operational */
			if (!test_bit(R5_Insync, &sh->dev[i].flags) ||
			    test_bit(R5_ReadError, &sh->dev[i].flags)) {
				bi = sh->dev[i].toread;
				sh->dev[i].toread = NULL;
				if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
					wake_up(&conf->wait_for_overlap);
				if (bi) to_read--;
				while (bi && bi->bi_sector < sh->dev[i].sector + STRIPE_SECTORS){
					struct bio *nextbi = r5_next_bio(bi, sh->dev[i].sector);
					clear_bit(BIO_UPTODATE, &bi->bi_flags);
					if (--bi->bi_phys_segments == 0) {
						bi->bi_next = return_bi;
						return_bi = bi;
					}
					bi = nextbi;
				}
			}
			spin_unlock_irq(&conf->device_lock);
			if (bitmap_end)
				bitmap_endwrite(conf->mddev->bitmap, sh->sector,
						STRIPE_SECTORS, 0, 0);
		}
	}
	if (failed > 2 && syncing) {
		md_done_sync(conf->mddev, STRIPE_SECTORS,0);
		clear_bit(STRIPE_SYNCING, &sh->state);
		syncing = 0;
	}

	/*
	 * might be able to return some write requests if the parity blocks
	 * are safe, or on a failed drive
	 */
	pdev = &sh->dev[pd_idx];
	p_failed = (failed >= 1 && failed_num[0] == pd_idx)
		|| (failed >= 2 && failed_num[1] == pd_idx);
	qdev = &sh->dev[qd_idx];
	q_failed = (failed >= 1 && failed_num[0] == qd_idx)
		|| (failed >= 2 && failed_num[1] == qd_idx);

	if ( written &&
	     ( p_failed || ((test_bit(R5_Insync, &pdev->flags)
			     && !test_bit(R5_LOCKED, &pdev->flags)
			     && test_bit(R5_UPTODATE, &pdev->flags))) ) &&
	     ( q_failed || ((test_bit(R5_Insync, &qdev->flags)
			     && !test_bit(R5_LOCKED, &qdev->flags)
			     && test_bit(R5_UPTODATE, &qdev->flags))) ) ) {
		/* any written block on an uptodate or failed drive can be
		 * returned.  Note that if we 'wrote' to a failed drive,
		 * it will be UPTODATE, but never LOCKED, so we don't need
		 * to test 'failed' directly.
		 */
		for (i=disks; i--; )
			if (sh->dev[i].written) {
				dev = &sh->dev[i];
				if (!test_bit(R5_LOCKED, &dev->flags) &&
				    test_bit(R5_UPTODATE, &dev->flags) ) {
					/* We can return any write requests */
					int bitmap_end = 0;
					struct bio *wbi, *wbi2;
					PRINTK("Return write for stripe %llu disc %d\n",
					       (unsigned long long)sh->sector, i);
					spin_lock_irq(&conf->device_lock);
					wbi = dev->written;
					dev->written = NULL;
					while (wbi && wbi->bi_sector < dev->sector + STRIPE_SECTORS) {
						wbi2 = r5_next_bio(wbi, dev->sector);
						if (--wbi->bi_phys_segments == 0) {
							md_write_end(conf->mddev);
							wbi->bi_next = return_bi;
							return_bi = wbi;
						}
						wbi = wbi2;
					}
					if (dev->towrite == NULL)
						bitmap_end = 1;
					spin_unlock_irq(&conf->device_lock);
					if (bitmap_end)
						bitmap_endwrite(conf->mddev->bitmap, sh->sector,
								STRIPE_SECTORS,
								!test_bit(STRIPE_DEGRADED, &sh->state), 0);
				}
			}
	}

	/* Now we might consider reading some blocks, either to check/generate
	 * parity, or to satisfy requests
	 * or to load a block that is being partially written.
	 */
	if (to_read || non_overwrite || (to_write && failed) ||
	    (syncing && (uptodate < disks)) || expanding) {
		for (i=disks; i--;) {
			dev = &sh->dev[i];
			if (!test_bit(R5_LOCKED, &dev->flags) && !test_bit(R5_UPTODATE, &dev->flags) &&
			    (dev->toread ||
			     (dev->towrite && !test_bit(R5_OVERWRITE, &dev->flags)) ||
			     syncing ||
			     expanding ||
			     (failed >= 1 && (sh->dev[failed_num[0]].toread || to_write)) ||
			     (failed >= 2 && (sh->dev[failed_num[1]].toread || to_write))
				    )
				) {
				/* we would like to get this block, possibly
				 * by computing it, but we might not be able to
				 */
				if (uptodate == disks-1) {
					PRINTK("Computing stripe %llu block %d\n",
					       (unsigned long long)sh->sector, i);
					compute_block_1(sh, i, 0);
					uptodate++;
				} else if ( uptodate == disks-2 && failed >= 2 ) {
					/* Computing 2-failure is *very* expensive; only do it if failed >= 2 */
					int other;
					for (other=disks; other--;) {
						if ( other == i )
							continue;
						if ( !test_bit(R5_UPTODATE, &sh->dev[other].flags) )
							break;
					}
					BUG_ON(other < 0);
					PRINTK("Computing stripe %llu blocks %d,%d\n",
					       (unsigned long long)sh->sector, i, other);
					compute_block_2(sh, i, other);
					uptodate += 2;
				} else if (test_bit(R5_Insync, &dev->flags)) {
					set_bit(R5_LOCKED, &dev->flags);
					set_bit(R5_Wantread, &dev->flags);
					locked++;
					PRINTK("Reading block %d (sync=%d)\n",
						i, syncing);
				}
			}
		}
		set_bit(STRIPE_HANDLE, &sh->state);
	}

	/* now to consider writing and what else, if anything should be read */
	if (to_write) {
		int rcw=0, must_compute=0;
		for (i=disks ; i--;) {
			dev = &sh->dev[i];
			/* Would I have to read this buffer for reconstruct_write */
			if (!test_bit(R5_OVERWRITE, &dev->flags)
			    && i != pd_idx && i != qd_idx
			    && (!test_bit(R5_LOCKED, &dev->flags)
				    ) &&
			    !test_bit(R5_UPTODATE, &dev->flags)) {
				if (test_bit(R5_Insync, &dev->flags)) rcw++;
				else {
					PRINTK("raid6: must_compute: disk %d flags=%#lx\n", i, dev->flags);
					must_compute++;
				}
			}
		}
		PRINTK("for sector %llu, rcw=%d, must_compute=%d\n",
		       (unsigned long long)sh->sector, rcw, must_compute);
		set_bit(STRIPE_HANDLE, &sh->state);

		if (rcw > 0)
			/* want reconstruct write, but need to get some data */
			for (i=disks; i--;) {
				dev = &sh->dev[i];
				if (!test_bit(R5_OVERWRITE, &dev->flags)
				    && !(failed == 0 && (i == pd_idx || i == qd_idx))
				    && !test_bit(R5_LOCKED, &dev->flags) && !test_bit(R5_UPTODATE, &dev->flags) &&
				    test_bit(R5_Insync, &dev->flags)) {
					if (test_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
					{
						PRINTK("Read_old stripe %llu block %d for Reconstruct\n",
						       (unsigned long long)sh->sector, i);
						set_bit(R5_LOCKED, &dev->flags);
						set_bit(R5_Wantread, &dev->flags);
						locked++;
					} else {
						PRINTK("Request delayed stripe %llu block %d for Reconstruct\n",
						       (unsigned long long)sh->sector, i);
						set_bit(STRIPE_DELAYED, &sh->state);
						set_bit(STRIPE_HANDLE, &sh->state);
					}
				}
			}
		/* now if nothing is locked, and if we have enough data, we can start a write request */
		if (locked == 0 && rcw == 0 &&
		    !test_bit(STRIPE_BIT_DELAY, &sh->state)) {
			if ( must_compute > 0 ) {
				/* We have failed blocks and need to compute them */
				switch ( failed ) {
				case 0:	BUG();
				case 1: compute_block_1(sh, failed_num[0], 0); break;
				case 2: compute_block_2(sh, failed_num[0], failed_num[1]); break;
				default: BUG();	/* This request should have been failed? */
				}
			}

			PRINTK("Computing parity for stripe %llu\n", (unsigned long long)sh->sector);
			compute_parity6(sh, RECONSTRUCT_WRITE);
			/* now every locked buffer is ready to be written */
			for (i=disks; i--;)
				if (test_bit(R5_LOCKED, &sh->dev[i].flags)) {
					PRINTK("Writing stripe %llu block %d\n",
					       (unsigned long long)sh->sector, i);
					locked++;
					set_bit(R5_Wantwrite, &sh->dev[i].flags);
				}
			/* after a RECONSTRUCT_WRITE, the stripe MUST be in-sync */
			set_bit(STRIPE_INSYNC, &sh->state);

			if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
				atomic_dec(&conf->preread_active_stripes);
				if (atomic_read(&conf->preread_active_stripes) < IO_THRESHOLD)
					md_wakeup_thread(conf->mddev->thread);
			}
		}
	}

	/* maybe we need to check and possibly fix the parity for this stripe
	 * Any reads will already have been scheduled, so we just see if enough data
	 * is available
	 */
	if (syncing && locked == 0 && !test_bit(STRIPE_INSYNC, &sh->state)) {
		int update_p = 0, update_q = 0;
		struct r5dev *dev;

		set_bit(STRIPE_HANDLE, &sh->state);

		BUG_ON(failed>2);
		BUG_ON(uptodate < disks);
		/* Want to check and possibly repair P and Q.
		 * However there could be one 'failed' device, in which
		 * case we can only check one of them, possibly using the
		 * other to generate missing data
		 */

		/* If !tmp_page, we cannot do the calculations,
		 * but as we have set STRIPE_HANDLE, we will soon be called
		 * by stripe_handle with a tmp_page - just wait until then.
		 */
		if (tmp_page) {
			if (failed == q_failed) {
				/* The only possible failed device holds 'Q', so it makes
				 * sense to check P (If anything else were failed, we would
				 * have used P to recreate it).
				 */
				compute_block_1(sh, pd_idx, 1);
				if (!page_is_zero(sh->dev[pd_idx].page)) {
					compute_block_1(sh,pd_idx,0);
					update_p = 1;
				}
			}
			if (!q_failed && failed < 2) {
				/* q is not failed, and we didn't use it to generate
				 * anything, so it makes sense to check it
				 */
				memcpy(page_address(tmp_page),
				       page_address(sh->dev[qd_idx].page),
				       STRIPE_SIZE);
				compute_parity6(sh, UPDATE_PARITY);
				if (memcmp(page_address(tmp_page),
					   page_address(sh->dev[qd_idx].page),
					   STRIPE_SIZE)!= 0) {
					clear_bit(STRIPE_INSYNC, &sh->state);
					update_q = 1;
				}
			}
			if (update_p || update_q) {
				conf->mddev->resync_mismatches += STRIPE_SECTORS;
				if (test_bit(MD_RECOVERY_CHECK, &conf->mddev->recovery))
					/* don't try to repair!! */
					update_p = update_q = 0;
			}

			/* now write out any block on a failed drive,
			 * or P or Q if they need it
			 */

			if (failed == 2) {
				dev = &sh->dev[failed_num[1]];
				locked++;
				set_bit(R5_LOCKED, &dev->flags);
				set_bit(R5_Wantwrite, &dev->flags);
			}
			if (failed >= 1) {
				dev = &sh->dev[failed_num[0]];
				locked++;
				set_bit(R5_LOCKED, &dev->flags);
				set_bit(R5_Wantwrite, &dev->flags);
			}

			if (update_p) {
				dev = &sh->dev[pd_idx];
				locked ++;
				set_bit(R5_LOCKED, &dev->flags);
				set_bit(R5_Wantwrite, &dev->flags);
			}
			if (update_q) {
				dev = &sh->dev[qd_idx];
				locked++;
				set_bit(R5_LOCKED, &dev->flags);
				set_bit(R5_Wantwrite, &dev->flags);
			}
			clear_bit(STRIPE_DEGRADED, &sh->state);

			set_bit(STRIPE_INSYNC, &sh->state);
		}
	}

	if (syncing && locked == 0 && test_bit(STRIPE_INSYNC, &sh->state)) {
		md_done_sync(conf->mddev, STRIPE_SECTORS,1);
		clear_bit(STRIPE_SYNCING, &sh->state);
	}

	/* If the failed drives are just a ReadError, then we might need
	 * to progress the repair/check process
	 */
	if (failed <= 2 && ! conf->mddev->ro)
		for (i=0; i<failed;i++) {
			dev = &sh->dev[failed_num[i]];
			if (test_bit(R5_ReadError, &dev->flags)
			    && !test_bit(R5_LOCKED, &dev->flags)
			    && test_bit(R5_UPTODATE, &dev->flags)
				) {
				if (!test_bit(R5_ReWrite, &dev->flags)) {
					set_bit(R5_Wantwrite, &dev->flags);
					set_bit(R5_ReWrite, &dev->flags);
					set_bit(R5_LOCKED, &dev->flags);
				} else {
					/* let's read it back */
					set_bit(R5_Wantread, &dev->flags);
					set_bit(R5_LOCKED, &dev->flags);
				}
			}
		}

	if (expanded && test_bit(STRIPE_EXPANDING, &sh->state)) {
		/* Need to write out all blocks after computing P&Q */
		sh->disks = conf->raid_disks;
		sh->pd_idx = stripe_to_pdidx(sh->sector, conf,
					     conf->raid_disks);
		compute_parity6(sh, RECONSTRUCT_WRITE);
		for (i = conf->raid_disks ; i-- ;  ) {
			set_bit(R5_LOCKED, &sh->dev[i].flags);
			locked++;
			set_bit(R5_Wantwrite, &sh->dev[i].flags);
		}
		clear_bit(STRIPE_EXPANDING, &sh->state);
	} else if (expanded) {
		clear_bit(STRIPE_EXPAND_READY, &sh->state);
		atomic_dec(&conf->reshape_stripes);
		wake_up(&conf->wait_for_overlap);
		md_done_sync(conf->mddev, STRIPE_SECTORS, 1);
	}

	if (expanding && locked == 0) {
		/* We have read all the blocks in this stripe and now we need to
		 * copy some of them into a target stripe for expand.
		 */
		clear_bit(STRIPE_EXPAND_SOURCE, &sh->state);
		for (i = 0; i < sh->disks ; i++)
			if (i != pd_idx && i != qd_idx) {
				int dd_idx2, pd_idx2, j;
				struct stripe_head *sh2;

				sector_t bn = compute_blocknr(sh, i);
				sector_t s = raid5_compute_sector(
					bn, conf->raid_disks,
					conf->raid_disks - conf->max_degraded,
					&dd_idx2, &pd_idx2, conf);
				sh2 = get_active_stripe(conf, s,
							conf->raid_disks,
						       pd_idx2, 1);
				if (sh2 == NULL)
					/* so for only the early blocks of
					 * this stripe have been requests.
					 * When later blocks get requests, we
					 * will try again
					 */
					continue;
				if (!test_bit(STRIPE_EXPANDING, &sh2->state) ||
				    test_bit(R5_Expanded,
					     &sh2->dev[dd_idx2].flags)) {
					/* must have already done this block */
					release_stripe(sh2);
					continue;
				}
				memcpy(page_address(sh2->dev[dd_idx2].page),
				       page_address(sh->dev[i].page),
				       STRIPE_SIZE);
				set_bit(R5_Expanded, &sh2->dev[dd_idx2].flags);
				set_bit(R5_UPTODATE, &sh2->dev[dd_idx2].flags);
				for (j = 0 ; j < conf->raid_disks ; j++)
					if (j != sh2->pd_idx &&
					    j != raid6_next_disk(sh2->pd_idx,
							   sh2->disks) &&
					    !test_bit(R5_Expanded,
						      &sh2->dev[j].flags))
						break;
				if (j == conf->raid_disks) {
					set_bit(STRIPE_EXPAND_READY,
						&sh2->state);
					set_bit(STRIPE_HANDLE, &sh2->state);
				}
				release_stripe(sh2);
			}
	}

	spin_unlock(&sh->lock);

	while ((bi=return_bi)) {
		int bytes = bi->bi_size;

		return_bi = bi->bi_next;
		bi->bi_next = NULL;
		bi->bi_size = 0;
		bi->bi_end_io(bi, bytes,
			      test_bit(BIO_UPTODATE, &bi->bi_flags)
			        ? 0 : -EIO);
	}
	for (i=disks; i-- ;) {
		int rw;
		struct bio *bi;
		mdk_rdev_t *rdev;
		if (test_and_clear_bit(R5_Wantwrite, &sh->dev[i].flags))
			rw = WRITE;
		else if (test_and_clear_bit(R5_Wantread, &sh->dev[i].flags))
			rw = READ;
		else
			continue;

		bi = &sh->dev[i].req;

		bi->bi_rw = rw;
		if (rw == WRITE)
			bi->bi_end_io = raid5_end_write_request;
		else
			bi->bi_end_io = raid5_end_read_request;

		rcu_read_lock();
		rdev = rcu_dereference(conf->disks[i].rdev);
		if (rdev && test_bit(Faulty, &rdev->flags))
			rdev = NULL;
		if (rdev)
			atomic_inc(&rdev->nr_pending);
		rcu_read_unlock();

		if (rdev) {
			if (syncing || expanding || expanded)
				md_sync_acct(rdev->bdev, STRIPE_SECTORS);

			bi->bi_bdev = rdev->bdev;
			PRINTK("for %llu schedule op %ld on disc %d\n",
				(unsigned long long)sh->sector, bi->bi_rw, i);
			atomic_inc(&sh->count);
			bi->bi_sector = sh->sector + rdev->data_offset;
			bi->bi_flags = 1 << BIO_UPTODATE;
			bi->bi_vcnt = 1;
			bi->bi_max_vecs = 1;
			bi->bi_idx = 0;
			bi->bi_io_vec = &sh->dev[i].vec;
			bi->bi_io_vec[0].bv_len = STRIPE_SIZE;
			bi->bi_io_vec[0].bv_offset = 0;
			bi->bi_size = STRIPE_SIZE;
			bi->bi_next = NULL;
			if (rw == WRITE &&
			    test_bit(R5_ReWrite, &sh->dev[i].flags))
				atomic_add(STRIPE_SECTORS, &rdev->corrected_errors);
			generic_make_request(bi);
		} else {
			if (rw == WRITE)
				set_bit(STRIPE_DEGRADED, &sh->state);
			PRINTK("skip op %ld on disc %d for sector %llu\n",
				bi->bi_rw, i, (unsigned long long)sh->sector);
			clear_bit(R5_LOCKED, &sh->dev[i].flags);
			set_bit(STRIPE_HANDLE, &sh->state);
		}
	}
}

static void handle_stripe(struct stripe_head *sh, struct page *tmp_page)
{
	if (sh->raid_conf->level == 6)
		handle_stripe6(sh, tmp_page);
	else
		handle_stripe5(sh);
}



static void raid5_activate_delayed(raid5_conf_t *conf)
{
	if (atomic_read(&conf->preread_active_stripes) < IO_THRESHOLD) {
		while (!list_empty(&conf->delayed_list)) {
			struct list_head *l = conf->delayed_list.next;
			struct stripe_head *sh;
			sh = list_entry(l, struct stripe_head, lru);
			list_del_init(l);
			clear_bit(STRIPE_DELAYED, &sh->state);
			if (!test_and_set_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
				atomic_inc(&conf->preread_active_stripes);
			list_add_tail(&sh->lru, &conf->handle_list);
		}
	}
}

static void activate_bit_delay(raid5_conf_t *conf)
{
	/* device_lock is held */
	struct list_head head;
	list_add(&head, &conf->bitmap_list);
	list_del_init(&conf->bitmap_list);
	while (!list_empty(&head)) {
		struct stripe_head *sh = list_entry(head.next, struct stripe_head, lru);
		list_del_init(&sh->lru);
		atomic_inc(&sh->count);
		__release_stripe(conf, sh);
	}
}

static void unplug_slaves(mddev_t *mddev)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	int i;

	rcu_read_lock();
	for (i=0; i<mddev->raid_disks; i++) {
		mdk_rdev_t *rdev = rcu_dereference(conf->disks[i].rdev);
		if (rdev && !test_bit(Faulty, &rdev->flags) && atomic_read(&rdev->nr_pending)) {
			request_queue_t *r_queue = bdev_get_queue(rdev->bdev);

			atomic_inc(&rdev->nr_pending);
			rcu_read_unlock();

			if (r_queue->unplug_fn)
				r_queue->unplug_fn(r_queue);

			rdev_dec_pending(rdev, mddev);
			rcu_read_lock();
		}
	}
	rcu_read_unlock();
}

static void raid5_unplug_device(request_queue_t *q)
{
	mddev_t *mddev = q->queuedata;
	raid5_conf_t *conf = mddev_to_conf(mddev);
	unsigned long flags;

	spin_lock_irqsave(&conf->device_lock, flags);

	if (blk_remove_plug(q)) {
		conf->seq_flush++;
		raid5_activate_delayed(conf);
	}
	md_wakeup_thread(mddev->thread);

	spin_unlock_irqrestore(&conf->device_lock, flags);

	unplug_slaves(mddev);
}

static int raid5_issue_flush(request_queue_t *q, struct gendisk *disk,
			     sector_t *error_sector)
{
	mddev_t *mddev = q->queuedata;
	raid5_conf_t *conf = mddev_to_conf(mddev);
	int i, ret = 0;

	rcu_read_lock();
	for (i=0; i<mddev->raid_disks && ret == 0; i++) {
		mdk_rdev_t *rdev = rcu_dereference(conf->disks[i].rdev);
		if (rdev && !test_bit(Faulty, &rdev->flags)) {
			struct block_device *bdev = rdev->bdev;
			request_queue_t *r_queue = bdev_get_queue(bdev);

			if (!r_queue->issue_flush_fn)
				ret = -EOPNOTSUPP;
			else {
				atomic_inc(&rdev->nr_pending);
				rcu_read_unlock();
				ret = r_queue->issue_flush_fn(r_queue, bdev->bd_disk,
							      error_sector);
				rdev_dec_pending(rdev, mddev);
				rcu_read_lock();
			}
		}
	}
	rcu_read_unlock();
	return ret;
}

static int raid5_congested(void *data, int bits)
{
	mddev_t *mddev = data;
	raid5_conf_t *conf = mddev_to_conf(mddev);

	/* No difference between reads and writes.  Just check
	 * how busy the stripe_cache is
	 */
	if (conf->inactive_blocked)
		return 1;
	if (conf->quiesce)
		return 1;
	if (list_empty_careful(&conf->inactive_list))
		return 1;

	return 0;
}

/* We want read requests to align with chunks where possible,
 * but write requests don't need to.
 */
static int raid5_mergeable_bvec(request_queue_t *q, struct bio *bio, struct bio_vec *biovec)
{
	mddev_t *mddev = q->queuedata;
	sector_t sector = bio->bi_sector + get_start_sect(bio->bi_bdev);
	int max;
	unsigned int chunk_sectors = mddev->chunk_size >> 9;
	unsigned int bio_sectors = bio->bi_size >> 9;

	if (bio_data_dir(bio) == WRITE)
		return biovec->bv_len; /* always allow writes to be mergeable */

	max =  (chunk_sectors - ((sector & (chunk_sectors - 1)) + bio_sectors)) << 9;
	if (max < 0) max = 0;
	if (max <= biovec->bv_len && bio_sectors == 0)
		return biovec->bv_len;
	else
		return max;
}


static int in_chunk_boundary(mddev_t *mddev, struct bio *bio)
{
	sector_t sector = bio->bi_sector + get_start_sect(bio->bi_bdev);
	unsigned int chunk_sectors = mddev->chunk_size >> 9;
	unsigned int bio_sectors = bio->bi_size >> 9;

	return  chunk_sectors >=
		((sector & (chunk_sectors - 1)) + bio_sectors);
}

/*
 *  add bio to the retry LIFO  ( in O(1) ... we are in interrupt )
 *  later sampled by raid5d.
 */
static void add_bio_to_retry(struct bio *bi,raid5_conf_t *conf)
{
	unsigned long flags;

	spin_lock_irqsave(&conf->device_lock, flags);

	bi->bi_next = conf->retry_read_aligned_list;
	conf->retry_read_aligned_list = bi;

	spin_unlock_irqrestore(&conf->device_lock, flags);
	md_wakeup_thread(conf->mddev->thread);
}


static struct bio *remove_bio_from_retry(raid5_conf_t *conf)
{
	struct bio *bi;

	bi = conf->retry_read_aligned;
	if (bi) {
		conf->retry_read_aligned = NULL;
		return bi;
	}
	bi = conf->retry_read_aligned_list;
	if(bi) {
		conf->retry_read_aligned_list = bi->bi_next;
		bi->bi_next = NULL;
		bi->bi_phys_segments = 1; /* biased count of active stripes */
		bi->bi_hw_segments = 0; /* count of processed stripes */
	}

	return bi;
}


/*
 *  The "raid5_align_endio" should check if the read succeeded and if it
 *  did, call bio_endio on the original bio (having bio_put the new bio
 *  first).
 *  If the read failed..
 */
static int raid5_align_endio(struct bio *bi, unsigned int bytes, int error)
{
	struct bio* raid_bi  = bi->bi_private;
	mddev_t *mddev;
	raid5_conf_t *conf;
	int uptodate = test_bit(BIO_UPTODATE, &bi->bi_flags);
	mdk_rdev_t *rdev;

	if (bi->bi_size)
		return 1;
	bio_put(bi);

	mddev = raid_bi->bi_bdev->bd_disk->queue->queuedata;
	conf = mddev_to_conf(mddev);
	rdev = (void*)raid_bi->bi_next;
	raid_bi->bi_next = NULL;

	rdev_dec_pending(rdev, conf->mddev);

	if (!error && uptodate) {
		bio_endio(raid_bi, bytes, 0);
		if (atomic_dec_and_test(&conf->active_aligned_reads))
			wake_up(&conf->wait_for_stripe);
		return 0;
	}


	PRINTK("raid5_align_endio : io error...handing IO for a retry\n");

	add_bio_to_retry(raid_bi, conf);
	return 0;
}

static int bio_fits_rdev(struct bio *bi)
{
	request_queue_t *q = bdev_get_queue(bi->bi_bdev);

	if ((bi->bi_size>>9) > q->max_sectors)
		return 0;
	blk_recount_segments(q, bi);
	if (bi->bi_phys_segments > q->max_phys_segments ||
	    bi->bi_hw_segments > q->max_hw_segments)
		return 0;

	if (q->merge_bvec_fn)
		/* it's too hard to apply the merge_bvec_fn at this stage,
		 * just just give up
		 */
		return 0;

	return 1;
}


static int chunk_aligned_read(request_queue_t *q, struct bio * raid_bio)
{
	mddev_t *mddev = q->queuedata;
	raid5_conf_t *conf = mddev_to_conf(mddev);
	const unsigned int raid_disks = conf->raid_disks;
	const unsigned int data_disks = raid_disks - conf->max_degraded;
	unsigned int dd_idx, pd_idx;
	struct bio* align_bi;
	mdk_rdev_t *rdev;

	if (!in_chunk_boundary(mddev, raid_bio)) {
		PRINTK("chunk_aligned_read : non aligned\n");
		return 0;
	}
	/*
 	 * use bio_clone to make a copy of the bio
	 */
	align_bi = bio_clone(raid_bio, GFP_NOIO);
	if (!align_bi)
		return 0;
	/*
	 *   set bi_end_io to a new function, and set bi_private to the
	 *     original bio.
	 */
	align_bi->bi_end_io  = raid5_align_endio;
	align_bi->bi_private = raid_bio;
	/*
	 *	compute position
	 */
	align_bi->bi_sector =  raid5_compute_sector(raid_bio->bi_sector,
					raid_disks,
					data_disks,
					&dd_idx,
					&pd_idx,
					conf);

	rcu_read_lock();
	rdev = rcu_dereference(conf->disks[dd_idx].rdev);
	if (rdev && test_bit(In_sync, &rdev->flags)) {
		atomic_inc(&rdev->nr_pending);
		rcu_read_unlock();
		raid_bio->bi_next = (void*)rdev;
		align_bi->bi_bdev =  rdev->bdev;
		align_bi->bi_flags &= ~(1 << BIO_SEG_VALID);
		align_bi->bi_sector += rdev->data_offset;

		if (!bio_fits_rdev(align_bi)) {
			/* too big in some way */
			bio_put(align_bi);
			rdev_dec_pending(rdev, mddev);
			return 0;
		}

		spin_lock_irq(&conf->device_lock);
		wait_event_lock_irq(conf->wait_for_stripe,
				    conf->quiesce == 0,
				    conf->device_lock, /* nothing */);
		atomic_inc(&conf->active_aligned_reads);
		spin_unlock_irq(&conf->device_lock);

		generic_make_request(align_bi);
		return 1;
	} else {
		rcu_read_unlock();
		bio_put(align_bi);
		return 0;
	}
}


static int make_request(request_queue_t *q, struct bio * bi)
{
	mddev_t *mddev = q->queuedata;
	raid5_conf_t *conf = mddev_to_conf(mddev);
	unsigned int dd_idx, pd_idx;
	sector_t new_sector;
	sector_t logical_sector, last_sector;
	struct stripe_head *sh;
	const int rw = bio_data_dir(bi);
	int remaining;

	if (unlikely(bio_barrier(bi))) {
		bio_endio(bi, bi->bi_size, -EOPNOTSUPP);
		return 0;
	}

	md_write_start(mddev, bi);

	disk_stat_inc(mddev->gendisk, ios[rw]);
	disk_stat_add(mddev->gendisk, sectors[rw], bio_sectors(bi));

	if (rw == READ &&
	     mddev->reshape_position == MaxSector &&
	     chunk_aligned_read(q,bi))
            	return 0;

	logical_sector = bi->bi_sector & ~((sector_t)STRIPE_SECTORS-1);
	last_sector = bi->bi_sector + (bi->bi_size>>9);
	bi->bi_next = NULL;
	bi->bi_phys_segments = 1;	/* over-loaded to count active stripes */

	for (;logical_sector < last_sector; logical_sector += STRIPE_SECTORS) {
		DEFINE_WAIT(w);
		int disks, data_disks;

	retry:
		prepare_to_wait(&conf->wait_for_overlap, &w, TASK_UNINTERRUPTIBLE);
		if (likely(conf->expand_progress == MaxSector))
			disks = conf->raid_disks;
		else {
			/* spinlock is needed as expand_progress may be
			 * 64bit on a 32bit platform, and so it might be
			 * possible to see a half-updated value
			 * Ofcourse expand_progress could change after
			 * the lock is dropped, so once we get a reference
			 * to the stripe that we think it is, we will have
			 * to check again.
			 */
			spin_lock_irq(&conf->device_lock);
			disks = conf->raid_disks;
			if (logical_sector >= conf->expand_progress)
				disks = conf->previous_raid_disks;
			else {
				if (logical_sector >= conf->expand_lo) {
					spin_unlock_irq(&conf->device_lock);
					schedule();
					goto retry;
				}
			}
			spin_unlock_irq(&conf->device_lock);
		}
		data_disks = disks - conf->max_degraded;

 		new_sector = raid5_compute_sector(logical_sector, disks, data_disks,
						  &dd_idx, &pd_idx, conf);
		PRINTK("raid5: make_request, sector %llu logical %llu\n",
			(unsigned long long)new_sector, 
			(unsigned long long)logical_sector);

		sh = get_active_stripe(conf, new_sector, disks, pd_idx, (bi->bi_rw&RWA_MASK));
		if (sh) {
			if (unlikely(conf->expand_progress != MaxSector)) {
				/* expansion might have moved on while waiting for a
				 * stripe, so we must do the range check again.
				 * Expansion could still move past after this
				 * test, but as we are holding a reference to
				 * 'sh', we know that if that happens,
				 *  STRIPE_EXPANDING will get set and the expansion
				 * won't proceed until we finish with the stripe.
				 */
				int must_retry = 0;
				spin_lock_irq(&conf->device_lock);
				if (logical_sector <  conf->expand_progress &&
				    disks == conf->previous_raid_disks)
					/* mismatch, need to try again */
					must_retry = 1;
				spin_unlock_irq(&conf->device_lock);
				if (must_retry) {
					release_stripe(sh);
					goto retry;
				}
			}
			/* FIXME what if we get a false positive because these
			 * are being updated.
			 */
			if (logical_sector >= mddev->suspend_lo &&
			    logical_sector < mddev->suspend_hi) {
				release_stripe(sh);
				schedule();
				goto retry;
			}

			if (test_bit(STRIPE_EXPANDING, &sh->state) ||
			    !add_stripe_bio(sh, bi, dd_idx, (bi->bi_rw&RW_MASK))) {
				/* Stripe is busy expanding or
				 * add failed due to overlap.  Flush everything
				 * and wait a while
				 */
				raid5_unplug_device(mddev->queue);
				release_stripe(sh);
				schedule();
				goto retry;
			}
			finish_wait(&conf->wait_for_overlap, &w);
			handle_stripe(sh, NULL);
			release_stripe(sh);
		} else {
			/* cannot get stripe for read-ahead, just give-up */
			clear_bit(BIO_UPTODATE, &bi->bi_flags);
			finish_wait(&conf->wait_for_overlap, &w);
			break;
		}
			
	}
	spin_lock_irq(&conf->device_lock);
	remaining = --bi->bi_phys_segments;
	spin_unlock_irq(&conf->device_lock);
	if (remaining == 0) {
		int bytes = bi->bi_size;

		if ( rw == WRITE )
			md_write_end(mddev);
		bi->bi_size = 0;
		bi->bi_end_io(bi, bytes,
			      test_bit(BIO_UPTODATE, &bi->bi_flags)
			        ? 0 : -EIO);
	}
	return 0;
}

static sector_t reshape_request(mddev_t *mddev, sector_t sector_nr, int *skipped)
{
	/* reshaping is quite different to recovery/resync so it is
	 * handled quite separately ... here.
	 *
	 * On each call to sync_request, we gather one chunk worth of
	 * destination stripes and flag them as expanding.
	 * Then we find all the source stripes and request reads.
	 * As the reads complete, handle_stripe will copy the data
	 * into the destination stripe and release that stripe.
	 */
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	struct stripe_head *sh;
	int pd_idx;
	sector_t first_sector, last_sector;
	int raid_disks = conf->previous_raid_disks;
	int data_disks = raid_disks - conf->max_degraded;
	int new_data_disks = conf->raid_disks - conf->max_degraded;
	int i;
	int dd_idx;
	sector_t writepos, safepos, gap;

	if (sector_nr == 0 &&
	    conf->expand_progress != 0) {
		/* restarting in the middle, skip the initial sectors */
		sector_nr = conf->expand_progress;
		sector_div(sector_nr, new_data_disks);
		*skipped = 1;
		return sector_nr;
	}

	/* we update the metadata when there is more than 3Meg
	 * in the block range (that is rather arbitrary, should
	 * probably be time based) or when the data about to be
	 * copied would over-write the source of the data at
	 * the front of the range.
	 * i.e. one new_stripe forward from expand_progress new_maps
	 * to after where expand_lo old_maps to
	 */
	writepos = conf->expand_progress +
		conf->chunk_size/512*(new_data_disks);
	sector_div(writepos, new_data_disks);
	safepos = conf->expand_lo;
	sector_div(safepos, data_disks);
	gap = conf->expand_progress - conf->expand_lo;

	if (writepos >= safepos ||
	    gap > (new_data_disks)*3000*2 /*3Meg*/) {
		/* Cannot proceed until we've updated the superblock... */
		wait_event(conf->wait_for_overlap,
			   atomic_read(&conf->reshape_stripes)==0);
		mddev->reshape_position = conf->expand_progress;
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		md_wakeup_thread(mddev->thread);
		wait_event(mddev->sb_wait, mddev->flags == 0 ||
			   kthread_should_stop());
		spin_lock_irq(&conf->device_lock);
		conf->expand_lo = mddev->reshape_position;
		spin_unlock_irq(&conf->device_lock);
		wake_up(&conf->wait_for_overlap);
	}

	for (i=0; i < conf->chunk_size/512; i+= STRIPE_SECTORS) {
		int j;
		int skipped = 0;
		pd_idx = stripe_to_pdidx(sector_nr+i, conf, conf->raid_disks);
		sh = get_active_stripe(conf, sector_nr+i,
				       conf->raid_disks, pd_idx, 0);
		set_bit(STRIPE_EXPANDING, &sh->state);
		atomic_inc(&conf->reshape_stripes);
		/* If any of this stripe is beyond the end of the old
		 * array, then we need to zero those blocks
		 */
		for (j=sh->disks; j--;) {
			sector_t s;
			if (j == sh->pd_idx)
				continue;
			if (conf->level == 6 &&
			    j == raid6_next_disk(sh->pd_idx, sh->disks))
				continue;
			s = compute_blocknr(sh, j);
			if (s < (mddev->array_size<<1)) {
				skipped = 1;
				continue;
			}
			memset(page_address(sh->dev[j].page), 0, STRIPE_SIZE);
			set_bit(R5_Expanded, &sh->dev[j].flags);
			set_bit(R5_UPTODATE, &sh->dev[j].flags);
		}
		if (!skipped) {
			set_bit(STRIPE_EXPAND_READY, &sh->state);
			set_bit(STRIPE_HANDLE, &sh->state);
		}
		release_stripe(sh);
	}
	spin_lock_irq(&conf->device_lock);
	conf->expand_progress = (sector_nr + i) * new_data_disks;
	spin_unlock_irq(&conf->device_lock);
	/* Ok, those stripe are ready. We can start scheduling
	 * reads on the source stripes.
	 * The source stripes are determined by mapping the first and last
	 * block on the destination stripes.
	 */
	first_sector =
		raid5_compute_sector(sector_nr*(new_data_disks),
				     raid_disks, data_disks,
				     &dd_idx, &pd_idx, conf);
	last_sector =
		raid5_compute_sector((sector_nr+conf->chunk_size/512)
				     *(new_data_disks) -1,
				     raid_disks, data_disks,
				     &dd_idx, &pd_idx, conf);
	if (last_sector >= (mddev->size<<1))
		last_sector = (mddev->size<<1)-1;
	while (first_sector <= last_sector) {
		pd_idx = stripe_to_pdidx(first_sector, conf,
					 conf->previous_raid_disks);
		sh = get_active_stripe(conf, first_sector,
				       conf->previous_raid_disks, pd_idx, 0);
		set_bit(STRIPE_EXPAND_SOURCE, &sh->state);
		set_bit(STRIPE_HANDLE, &sh->state);
		release_stripe(sh);
		first_sector += STRIPE_SECTORS;
	}
	return conf->chunk_size>>9;
}

/* FIXME go_faster isn't used */
static inline sector_t sync_request(mddev_t *mddev, sector_t sector_nr, int *skipped, int go_faster)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	struct stripe_head *sh;
	int pd_idx;
	int raid_disks = conf->raid_disks;
	sector_t max_sector = mddev->size << 1;
	int sync_blocks;
	int still_degraded = 0;
	int i;

	if (sector_nr >= max_sector) {
		/* just being told to finish up .. nothing much to do */
		unplug_slaves(mddev);
		if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery)) {
			end_reshape(conf);
			return 0;
		}

		if (mddev->curr_resync < max_sector) /* aborted */
			bitmap_end_sync(mddev->bitmap, mddev->curr_resync,
					&sync_blocks, 1);
		else /* completed sync */
			conf->fullsync = 0;
		bitmap_close_sync(mddev->bitmap);

		return 0;
	}

	if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
		return reshape_request(mddev, sector_nr, skipped);

	/* if there is too many failed drives and we are trying
	 * to resync, then assert that we are finished, because there is
	 * nothing we can do.
	 */
	if (mddev->degraded >= conf->max_degraded &&
	    test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
		sector_t rv = (mddev->size << 1) - sector_nr;
		*skipped = 1;
		return rv;
	}
	if (!bitmap_start_sync(mddev->bitmap, sector_nr, &sync_blocks, 1) &&
	    !test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery) &&
	    !conf->fullsync && sync_blocks >= STRIPE_SECTORS) {
		/* we can skip this block, and probably more */
		sync_blocks /= STRIPE_SECTORS;
		*skipped = 1;
		return sync_blocks * STRIPE_SECTORS; /* keep things rounded to whole stripes */
	}

	pd_idx = stripe_to_pdidx(sector_nr, conf, raid_disks);
	sh = get_active_stripe(conf, sector_nr, raid_disks, pd_idx, 1);
	if (sh == NULL) {
		sh = get_active_stripe(conf, sector_nr, raid_disks, pd_idx, 0);
		/* make sure we don't swamp the stripe cache if someone else
		 * is trying to get access
		 */
		schedule_timeout_uninterruptible(1);
	}
	/* Need to check if array will still be degraded after recovery/resync
	 * We don't need to check the 'failed' flag as when that gets set,
	 * recovery aborts.
	 */
	for (i=0; i<mddev->raid_disks; i++)
		if (conf->disks[i].rdev == NULL)
			still_degraded = 1;

	bitmap_start_sync(mddev->bitmap, sector_nr, &sync_blocks, still_degraded);

	spin_lock(&sh->lock);
	set_bit(STRIPE_SYNCING, &sh->state);
	clear_bit(STRIPE_INSYNC, &sh->state);
	spin_unlock(&sh->lock);

	handle_stripe(sh, NULL);
	release_stripe(sh);

	return STRIPE_SECTORS;
}

static int  retry_aligned_read(raid5_conf_t *conf, struct bio *raid_bio)
{
	/* We may not be able to submit a whole bio at once as there
	 * may not be enough stripe_heads available.
	 * We cannot pre-allocate enough stripe_heads as we may need
	 * more than exist in the cache (if we allow ever large chunks).
	 * So we do one stripe head at a time and record in
	 * ->bi_hw_segments how many have been done.
	 *
	 * We *know* that this entire raid_bio is in one chunk, so
	 * it will be only one 'dd_idx' and only need one call to raid5_compute_sector.
	 */
	struct stripe_head *sh;
	int dd_idx, pd_idx;
	sector_t sector, logical_sector, last_sector;
	int scnt = 0;
	int remaining;
	int handled = 0;

	logical_sector = raid_bio->bi_sector & ~((sector_t)STRIPE_SECTORS-1);
	sector = raid5_compute_sector(	logical_sector,
					conf->raid_disks,
					conf->raid_disks - conf->max_degraded,
					&dd_idx,
					&pd_idx,
					conf);
	last_sector = raid_bio->bi_sector + (raid_bio->bi_size>>9);

	for (; logical_sector < last_sector;
	     logical_sector += STRIPE_SECTORS,
		     sector += STRIPE_SECTORS,
		     scnt++) {

		if (scnt < raid_bio->bi_hw_segments)
			/* already done this stripe */
			continue;

		sh = get_active_stripe(conf, sector, conf->raid_disks, pd_idx, 1);

		if (!sh) {
			/* failed to get a stripe - must wait */
			raid_bio->bi_hw_segments = scnt;
			conf->retry_read_aligned = raid_bio;
			return handled;
		}

		set_bit(R5_ReadError, &sh->dev[dd_idx].flags);
		if (!add_stripe_bio(sh, raid_bio, dd_idx, 0)) {
			release_stripe(sh);
			raid_bio->bi_hw_segments = scnt;
			conf->retry_read_aligned = raid_bio;
			return handled;
		}

		handle_stripe(sh, NULL);
		release_stripe(sh);
		handled++;
	}
	spin_lock_irq(&conf->device_lock);
	remaining = --raid_bio->bi_phys_segments;
	spin_unlock_irq(&conf->device_lock);
	if (remaining == 0) {
		int bytes = raid_bio->bi_size;

		raid_bio->bi_size = 0;
		raid_bio->bi_end_io(raid_bio, bytes,
			      test_bit(BIO_UPTODATE, &raid_bio->bi_flags)
			        ? 0 : -EIO);
	}
	if (atomic_dec_and_test(&conf->active_aligned_reads))
		wake_up(&conf->wait_for_stripe);
	return handled;
}



/*
 * This is our raid5 kernel thread.
 *
 * We scan the hash table for stripes which can be handled now.
 * During the scan, completed stripes are saved for us by the interrupt
 * handler, so that they will not have to wait for our next wakeup.
 */
static void raid5d (mddev_t *mddev)
{
	struct stripe_head *sh;
	raid5_conf_t *conf = mddev_to_conf(mddev);
	int handled;

	PRINTK("+++ raid5d active\n");

	md_check_recovery(mddev);

	handled = 0;
	spin_lock_irq(&conf->device_lock);
	while (1) {
		struct list_head *first;
		struct bio *bio;

		if (conf->seq_flush != conf->seq_write) {
			int seq = conf->seq_flush;
			spin_unlock_irq(&conf->device_lock);
			bitmap_unplug(mddev->bitmap);
			spin_lock_irq(&conf->device_lock);
			conf->seq_write = seq;
			activate_bit_delay(conf);
		}

		if (list_empty(&conf->handle_list) &&
		    atomic_read(&conf->preread_active_stripes) < IO_THRESHOLD &&
		    !blk_queue_plugged(mddev->queue) &&
		    !list_empty(&conf->delayed_list))
			raid5_activate_delayed(conf);

		while ((bio = remove_bio_from_retry(conf))) {
			int ok;
			spin_unlock_irq(&conf->device_lock);
			ok = retry_aligned_read(conf, bio);
			spin_lock_irq(&conf->device_lock);
			if (!ok)
				break;
			handled++;
		}

		if (list_empty(&conf->handle_list))
			break;

		first = conf->handle_list.next;
		sh = list_entry(first, struct stripe_head, lru);

		list_del_init(first);
		atomic_inc(&sh->count);
		BUG_ON(atomic_read(&sh->count)!= 1);
		spin_unlock_irq(&conf->device_lock);
		
		handled++;
		handle_stripe(sh, conf->spare_page);
		release_stripe(sh);

		spin_lock_irq(&conf->device_lock);
	}
	PRINTK("%d stripes handled\n", handled);

	spin_unlock_irq(&conf->device_lock);

	unplug_slaves(mddev);

	PRINTK("--- raid5d inactive\n");
}

static ssize_t
raid5_show_stripe_cache_size(mddev_t *mddev, char *page)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	if (conf)
		return sprintf(page, "%d\n", conf->max_nr_stripes);
	else
		return 0;
}

static ssize_t
raid5_store_stripe_cache_size(mddev_t *mddev, const char *page, size_t len)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	char *end;
	int new;
	if (len >= PAGE_SIZE)
		return -EINVAL;
	if (!conf)
		return -ENODEV;

	new = simple_strtoul(page, &end, 10);
	if (!*page || (*end && *end != '\n') )
		return -EINVAL;
	if (new <= 16 || new > 32768)
		return -EINVAL;
	while (new < conf->max_nr_stripes) {
		if (drop_one_stripe(conf))
			conf->max_nr_stripes--;
		else
			break;
	}
	md_allow_write(mddev);
	while (new > conf->max_nr_stripes) {
		if (grow_one_stripe(conf))
			conf->max_nr_stripes++;
		else break;
	}
	return len;
}

static struct md_sysfs_entry
raid5_stripecache_size = __ATTR(stripe_cache_size, S_IRUGO | S_IWUSR,
				raid5_show_stripe_cache_size,
				raid5_store_stripe_cache_size);

static ssize_t
stripe_cache_active_show(mddev_t *mddev, char *page)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	if (conf)
		return sprintf(page, "%d\n", atomic_read(&conf->active_stripes));
	else
		return 0;
}

static struct md_sysfs_entry
raid5_stripecache_active = __ATTR_RO(stripe_cache_active);

static struct attribute *raid5_attrs[] =  {
	&raid5_stripecache_size.attr,
	&raid5_stripecache_active.attr,
	NULL,
};
static struct attribute_group raid5_attrs_group = {
	.name = NULL,
	.attrs = raid5_attrs,
};

static int run(mddev_t *mddev)
{
	raid5_conf_t *conf;
	int raid_disk, memory;
	mdk_rdev_t *rdev;
	struct disk_info *disk;
	struct list_head *tmp;
	int working_disks = 0;

	if (mddev->level != 5 && mddev->level != 4 && mddev->level != 6) {
		printk(KERN_ERR "raid5: %s: raid level not set to 4/5/6 (%d)\n",
		       mdname(mddev), mddev->level);
		return -EIO;
	}

	if (mddev->reshape_position != MaxSector) {
		/* Check that we can continue the reshape.
		 * Currently only disks can change, it must
		 * increase, and we must be past the point where
		 * a stripe over-writes itself
		 */
		sector_t here_new, here_old;
		int old_disks;
		int max_degraded = (mddev->level == 5 ? 1 : 2);

		if (mddev->new_level != mddev->level ||
		    mddev->new_layout != mddev->layout ||
		    mddev->new_chunk != mddev->chunk_size) {
			printk(KERN_ERR "raid5: %s: unsupported reshape "
			       "required - aborting.\n",
			       mdname(mddev));
			return -EINVAL;
		}
		if (mddev->delta_disks <= 0) {
			printk(KERN_ERR "raid5: %s: unsupported reshape "
			       "(reduce disks) required - aborting.\n",
			       mdname(mddev));
			return -EINVAL;
		}
		old_disks = mddev->raid_disks - mddev->delta_disks;
		/* reshape_position must be on a new-stripe boundary, and one
		 * further up in new geometry must map after here in old
		 * geometry.
		 */
		here_new = mddev->reshape_position;
		if (sector_div(here_new, (mddev->chunk_size>>9)*
			       (mddev->raid_disks - max_degraded))) {
			printk(KERN_ERR "raid5: reshape_position not "
			       "on a stripe boundary\n");
			return -EINVAL;
		}
		/* here_new is the stripe we will write to */
		here_old = mddev->reshape_position;
		sector_div(here_old, (mddev->chunk_size>>9)*
			   (old_disks-max_degraded));
		/* here_old is the first stripe that we might need to read
		 * from */
		if (here_new >= here_old) {
			/* Reading from the same stripe as writing to - bad */
			printk(KERN_ERR "raid5: reshape_position too early for "
			       "auto-recovery - aborting.\n");
			return -EINVAL;
		}
		printk(KERN_INFO "raid5: reshape will continue\n");
		/* OK, we should be able to continue; */
	}


	mddev->private = kzalloc(sizeof (raid5_conf_t), GFP_KERNEL);
	if ((conf = mddev->private) == NULL)
		goto abort;
	if (mddev->reshape_position == MaxSector) {
		conf->previous_raid_disks = conf->raid_disks = mddev->raid_disks;
	} else {
		conf->raid_disks = mddev->raid_disks;
		conf->previous_raid_disks = mddev->raid_disks - mddev->delta_disks;
	}

	conf->disks = kzalloc(conf->raid_disks * sizeof(struct disk_info),
			      GFP_KERNEL);
	if (!conf->disks)
		goto abort;

	conf->mddev = mddev;

	if ((conf->stripe_hashtbl = kzalloc(PAGE_SIZE, GFP_KERNEL)) == NULL)
		goto abort;

	if (mddev->level == 6) {
		conf->spare_page = alloc_page(GFP_KERNEL);
		if (!conf->spare_page)
			goto abort;
	}
	spin_lock_init(&conf->device_lock);
	init_waitqueue_head(&conf->wait_for_stripe);
	init_waitqueue_head(&conf->wait_for_overlap);
	INIT_LIST_HEAD(&conf->handle_list);
	INIT_LIST_HEAD(&conf->delayed_list);
	INIT_LIST_HEAD(&conf->bitmap_list);
	INIT_LIST_HEAD(&conf->inactive_list);
	atomic_set(&conf->active_stripes, 0);
	atomic_set(&conf->preread_active_stripes, 0);
	atomic_set(&conf->active_aligned_reads, 0);

	PRINTK("raid5: run(%s) called.\n", mdname(mddev));

	ITERATE_RDEV(mddev,rdev,tmp) {
		raid_disk = rdev->raid_disk;
		if (raid_disk >= conf->raid_disks
		    || raid_disk < 0)
			continue;
		disk = conf->disks + raid_disk;

		disk->rdev = rdev;

		if (test_bit(In_sync, &rdev->flags)) {
			char b[BDEVNAME_SIZE];
			printk(KERN_INFO "raid5: device %s operational as raid"
				" disk %d\n", bdevname(rdev->bdev,b),
				raid_disk);
			working_disks++;
		}
	}

	/*
	 * 0 for a fully functional array, 1 or 2 for a degraded array.
	 */
	mddev->degraded = conf->raid_disks - working_disks;
	conf->mddev = mddev;
	conf->chunk_size = mddev->chunk_size;
	conf->level = mddev->level;
	if (conf->level == 6)
		conf->max_degraded = 2;
	else
		conf->max_degraded = 1;
	conf->algorithm = mddev->layout;
	conf->max_nr_stripes = NR_STRIPES;
	conf->expand_progress = mddev->reshape_position;

	/* device size must be a multiple of chunk size */
	mddev->size &= ~(mddev->chunk_size/1024 -1);
	mddev->resync_max_sectors = mddev->size << 1;

	if (conf->level == 6 && conf->raid_disks < 4) {
		printk(KERN_ERR "raid6: not enough configured devices for %s (%d, minimum 4)\n",
		       mdname(mddev), conf->raid_disks);
		goto abort;
	}
	if (!conf->chunk_size || conf->chunk_size % 4) {
		printk(KERN_ERR "raid5: invalid chunk size %d for %s\n",
			conf->chunk_size, mdname(mddev));
		goto abort;
	}
	if (conf->algorithm > ALGORITHM_RIGHT_SYMMETRIC) {
		printk(KERN_ERR 
			"raid5: unsupported parity algorithm %d for %s\n",
			conf->algorithm, mdname(mddev));
		goto abort;
	}
	if (mddev->degraded > conf->max_degraded) {
		printk(KERN_ERR "raid5: not enough operational devices for %s"
			" (%d/%d failed)\n",
			mdname(mddev), mddev->degraded, conf->raid_disks);
		goto abort;
	}

	if (mddev->degraded > 0 &&
	    mddev->recovery_cp != MaxSector) {
		if (mddev->ok_start_degraded)
			printk(KERN_WARNING
			       "raid5: starting dirty degraded array: %s"
			       "- data corruption possible.\n",
			       mdname(mddev));
		else {
			printk(KERN_ERR
			       "raid5: cannot start dirty degraded array for %s\n",
			       mdname(mddev));
			goto abort;
		}
	}

	{
		mddev->thread = md_register_thread(raid5d, mddev, "%s_raid5");
		if (!mddev->thread) {
			printk(KERN_ERR 
				"raid5: couldn't allocate thread for %s\n",
				mdname(mddev));
			goto abort;
		}
	}
	memory = conf->max_nr_stripes * (sizeof(struct stripe_head) +
		 conf->raid_disks * ((sizeof(struct bio) + PAGE_SIZE))) / 1024;
	if (grow_stripes(conf, conf->max_nr_stripes)) {
		printk(KERN_ERR 
			"raid5: couldn't allocate %dkB for buffers\n", memory);
		shrink_stripes(conf);
		md_unregister_thread(mddev->thread);
		goto abort;
	} else
		printk(KERN_INFO "raid5: allocated %dkB for %s\n",
			memory, mdname(mddev));

	if (mddev->degraded == 0)
		printk("raid5: raid level %d set %s active with %d out of %d"
			" devices, algorithm %d\n", conf->level, mdname(mddev), 
			mddev->raid_disks-mddev->degraded, mddev->raid_disks,
			conf->algorithm);
	else
		printk(KERN_ALERT "raid5: raid level %d set %s active with %d"
			" out of %d devices, algorithm %d\n", conf->level,
			mdname(mddev), mddev->raid_disks - mddev->degraded,
			mddev->raid_disks, conf->algorithm);

	print_raid5_conf(conf);

	if (conf->expand_progress != MaxSector) {
		printk("...ok start reshape thread\n");
		conf->expand_lo = conf->expand_progress;
		atomic_set(&conf->reshape_stripes, 0);
		clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
		clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
		set_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
		set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
		mddev->sync_thread = md_register_thread(md_do_sync, mddev,
							"%s_reshape");
	}

	/* read-ahead size must cover two whole stripes, which is
	 * 2 * (datadisks) * chunksize where 'n' is the number of raid devices
	 */
	{
		int data_disks = conf->previous_raid_disks - conf->max_degraded;
		int stripe = data_disks *
			(mddev->chunk_size / PAGE_SIZE);
		if (mddev->queue->backing_dev_info.ra_pages < 2 * stripe)
			mddev->queue->backing_dev_info.ra_pages = 2 * stripe;
	}

	/* Ok, everything is just fine now */
	if (sysfs_create_group(&mddev->kobj, &raid5_attrs_group))
		printk(KERN_WARNING
		       "raid5: failed to create sysfs attributes for %s\n",
		       mdname(mddev));

	mddev->queue->unplug_fn = raid5_unplug_device;
	mddev->queue->issue_flush_fn = raid5_issue_flush;
	mddev->queue->backing_dev_info.congested_data = mddev;
	mddev->queue->backing_dev_info.congested_fn = raid5_congested;

	mddev->array_size =  mddev->size * (conf->previous_raid_disks -
					    conf->max_degraded);

	blk_queue_merge_bvec(mddev->queue, raid5_mergeable_bvec);

	return 0;
abort:
	if (conf) {
		print_raid5_conf(conf);
		safe_put_page(conf->spare_page);
		kfree(conf->disks);
		kfree(conf->stripe_hashtbl);
		kfree(conf);
	}
	mddev->private = NULL;
	printk(KERN_ALERT "raid5: failed to run raid set %s\n", mdname(mddev));
	return -EIO;
}



static int stop(mddev_t *mddev)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;

	md_unregister_thread(mddev->thread);
	mddev->thread = NULL;
	shrink_stripes(conf);
	kfree(conf->stripe_hashtbl);
	mddev->queue->backing_dev_info.congested_fn = NULL;
	blk_sync_queue(mddev->queue); /* the unplug fn references 'conf'*/
	sysfs_remove_group(&mddev->kobj, &raid5_attrs_group);
	kfree(conf->disks);
	kfree(conf);
	mddev->private = NULL;
	return 0;
}

#if RAID5_DEBUG
static void print_sh (struct seq_file *seq, struct stripe_head *sh)
{
	int i;

	seq_printf(seq, "sh %llu, pd_idx %d, state %ld.\n",
		   (unsigned long long)sh->sector, sh->pd_idx, sh->state);
	seq_printf(seq, "sh %llu,  count %d.\n",
		   (unsigned long long)sh->sector, atomic_read(&sh->count));
	seq_printf(seq, "sh %llu, ", (unsigned long long)sh->sector);
	for (i = 0; i < sh->disks; i++) {
		seq_printf(seq, "(cache%d: %p %ld) ",
			   i, sh->dev[i].page, sh->dev[i].flags);
	}
	seq_printf(seq, "\n");
}

static void printall (struct seq_file *seq, raid5_conf_t *conf)
{
	struct stripe_head *sh;
	struct hlist_node *hn;
	int i;

	spin_lock_irq(&conf->device_lock);
	for (i = 0; i < NR_HASH; i++) {
		hlist_for_each_entry(sh, hn, &conf->stripe_hashtbl[i], hash) {
			if (sh->raid_conf != conf)
				continue;
			print_sh(seq, sh);
		}
	}
	spin_unlock_irq(&conf->device_lock);
}
#endif

static void status (struct seq_file *seq, mddev_t *mddev)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	int i;

	seq_printf (seq, " level %d, %dk chunk, algorithm %d", mddev->level, mddev->chunk_size >> 10, mddev->layout);
	seq_printf (seq, " [%d/%d] [", conf->raid_disks, conf->raid_disks - mddev->degraded);
	for (i = 0; i < conf->raid_disks; i++)
		seq_printf (seq, "%s",
			       conf->disks[i].rdev &&
			       test_bit(In_sync, &conf->disks[i].rdev->flags) ? "U" : "_");
	seq_printf (seq, "]");
#if RAID5_DEBUG
	seq_printf (seq, "\n");
	printall(seq, conf);
#endif
}

static void print_raid5_conf (raid5_conf_t *conf)
{
	int i;
	struct disk_info *tmp;

	printk("RAID5 conf printout:\n");
	if (!conf) {
		printk("(conf==NULL)\n");
		return;
	}
	printk(" --- rd:%d wd:%d\n", conf->raid_disks,
		 conf->raid_disks - conf->mddev->degraded);

	for (i = 0; i < conf->raid_disks; i++) {
		char b[BDEVNAME_SIZE];
		tmp = conf->disks + i;
		if (tmp->rdev)
		printk(" disk %d, o:%d, dev:%s\n",
			i, !test_bit(Faulty, &tmp->rdev->flags),
			bdevname(tmp->rdev->bdev,b));
	}
}

static int raid5_spare_active(mddev_t *mddev)
{
	int i;
	raid5_conf_t *conf = mddev->private;
	struct disk_info *tmp;

	for (i = 0; i < conf->raid_disks; i++) {
		tmp = conf->disks + i;
		if (tmp->rdev
		    && !test_bit(Faulty, &tmp->rdev->flags)
		    && !test_and_set_bit(In_sync, &tmp->rdev->flags)) {
			unsigned long flags;
			spin_lock_irqsave(&conf->device_lock, flags);
			mddev->degraded--;
			spin_unlock_irqrestore(&conf->device_lock, flags);
		}
	}
	print_raid5_conf(conf);
	return 0;
}

static int raid5_remove_disk(mddev_t *mddev, int number)
{
	raid5_conf_t *conf = mddev->private;
	int err = 0;
	mdk_rdev_t *rdev;
	struct disk_info *p = conf->disks + number;

	print_raid5_conf(conf);
	rdev = p->rdev;
	if (rdev) {
		if (test_bit(In_sync, &rdev->flags) ||
		    atomic_read(&rdev->nr_pending)) {
			err = -EBUSY;
			goto abort;
		}
		p->rdev = NULL;
		synchronize_rcu();
		if (atomic_read(&rdev->nr_pending)) {
			/* lost the race, try later */
			err = -EBUSY;
			p->rdev = rdev;
		}
	}
abort:

	print_raid5_conf(conf);
	return err;
}

static int raid5_add_disk(mddev_t *mddev, mdk_rdev_t *rdev)
{
	raid5_conf_t *conf = mddev->private;
	int found = 0;
	int disk;
	struct disk_info *p;

	if (mddev->degraded > conf->max_degraded)
		/* no point adding a device */
		return 0;

	/*
	 * find the disk ... but prefer rdev->saved_raid_disk
	 * if possible.
	 */
	if (rdev->saved_raid_disk >= 0 &&
	    conf->disks[rdev->saved_raid_disk].rdev == NULL)
		disk = rdev->saved_raid_disk;
	else
		disk = 0;
	for ( ; disk < conf->raid_disks; disk++)
		if ((p=conf->disks + disk)->rdev == NULL) {
			clear_bit(In_sync, &rdev->flags);
			rdev->raid_disk = disk;
			found = 1;
			if (rdev->saved_raid_disk != disk)
				conf->fullsync = 1;
			rcu_assign_pointer(p->rdev, rdev);
			break;
		}
	print_raid5_conf(conf);
	return found;
}

static int raid5_resize(mddev_t *mddev, sector_t sectors)
{
	/* no resync is happening, and there is enough space
	 * on all devices, so we can resize.
	 * We need to make sure resync covers any new space.
	 * If the array is shrinking we should possibly wait until
	 * any io in the removed space completes, but it hardly seems
	 * worth it.
	 */
	raid5_conf_t *conf = mddev_to_conf(mddev);

	sectors &= ~((sector_t)mddev->chunk_size/512 - 1);
	mddev->array_size = (sectors * (mddev->raid_disks-conf->max_degraded))>>1;
	set_capacity(mddev->gendisk, mddev->array_size << 1);
	mddev->changed = 1;
	if (sectors/2  > mddev->size && mddev->recovery_cp == MaxSector) {
		mddev->recovery_cp = mddev->size << 1;
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	}
	mddev->size = sectors /2;
	mddev->resync_max_sectors = sectors;
	return 0;
}

#ifdef CONFIG_MD_RAID5_RESHAPE
static int raid5_check_reshape(mddev_t *mddev)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	int err;

	if (mddev->delta_disks < 0 ||
	    mddev->new_level != mddev->level)
		return -EINVAL; /* Cannot shrink array or change level yet */
	if (mddev->delta_disks == 0)
		return 0; /* nothing to do */

	/* Can only proceed if there are plenty of stripe_heads.
	 * We need a minimum of one full stripe,, and for sensible progress
	 * it is best to have about 4 times that.
	 * If we require 4 times, then the default 256 4K stripe_heads will
	 * allow for chunk sizes up to 256K, which is probably OK.
	 * If the chunk size is greater, user-space should request more
	 * stripe_heads first.
	 */
	if ((mddev->chunk_size / STRIPE_SIZE) * 4 > conf->max_nr_stripes ||
	    (mddev->new_chunk / STRIPE_SIZE) * 4 > conf->max_nr_stripes) {
		printk(KERN_WARNING "raid5: reshape: not enough stripes.  Needed %lu\n",
		       (mddev->chunk_size / STRIPE_SIZE)*4);
		return -ENOSPC;
	}

	err = resize_stripes(conf, conf->raid_disks + mddev->delta_disks);
	if (err)
		return err;

	if (mddev->degraded > conf->max_degraded)
		return -EINVAL;
	/* looks like we might be able to manage this */
	return 0;
}

static int raid5_start_reshape(mddev_t *mddev)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);
	mdk_rdev_t *rdev;
	struct list_head *rtmp;
	int spares = 0;
	int added_devices = 0;
	unsigned long flags;

	if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery))
		return -EBUSY;

	ITERATE_RDEV(mddev, rdev, rtmp)
		if (rdev->raid_disk < 0 &&
		    !test_bit(Faulty, &rdev->flags))
			spares++;

	if (spares - mddev->degraded < mddev->delta_disks - conf->max_degraded)
		/* Not enough devices even to make a degraded array
		 * of that size
		 */
		return -EINVAL;

	atomic_set(&conf->reshape_stripes, 0);
	spin_lock_irq(&conf->device_lock);
	conf->previous_raid_disks = conf->raid_disks;
	conf->raid_disks += mddev->delta_disks;
	conf->expand_progress = 0;
	conf->expand_lo = 0;
	spin_unlock_irq(&conf->device_lock);

	/* Add some new drives, as many as will fit.
	 * We know there are enough to make the newly sized array work.
	 */
	ITERATE_RDEV(mddev, rdev, rtmp)
		if (rdev->raid_disk < 0 &&
		    !test_bit(Faulty, &rdev->flags)) {
			if (raid5_add_disk(mddev, rdev)) {
				char nm[20];
				set_bit(In_sync, &rdev->flags);
				added_devices++;
				rdev->recovery_offset = 0;
				sprintf(nm, "rd%d", rdev->raid_disk);
				if (sysfs_create_link(&mddev->kobj,
						      &rdev->kobj, nm))
					printk(KERN_WARNING
					       "raid5: failed to create "
					       " link %s for %s\n",
					       nm, mdname(mddev));
			} else
				break;
		}

	spin_lock_irqsave(&conf->device_lock, flags);
	mddev->degraded = (conf->raid_disks - conf->previous_raid_disks) - added_devices;
	spin_unlock_irqrestore(&conf->device_lock, flags);
	mddev->raid_disks = conf->raid_disks;
	mddev->reshape_position = 0;
	set_bit(MD_CHANGE_DEVS, &mddev->flags);

	clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
	clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
	set_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
	set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
	mddev->sync_thread = md_register_thread(md_do_sync, mddev,
						"%s_reshape");
	if (!mddev->sync_thread) {
		mddev->recovery = 0;
		spin_lock_irq(&conf->device_lock);
		mddev->raid_disks = conf->raid_disks = conf->previous_raid_disks;
		conf->expand_progress = MaxSector;
		spin_unlock_irq(&conf->device_lock);
		return -EAGAIN;
	}
	md_wakeup_thread(mddev->sync_thread);
	md_new_event(mddev);
	return 0;
}
#endif

static void end_reshape(raid5_conf_t *conf)
{
	struct block_device *bdev;

	if (!test_bit(MD_RECOVERY_INTR, &conf->mddev->recovery)) {
		conf->mddev->array_size = conf->mddev->size *
			(conf->raid_disks - conf->max_degraded);
		set_capacity(conf->mddev->gendisk, conf->mddev->array_size << 1);
		conf->mddev->changed = 1;

		bdev = bdget_disk(conf->mddev->gendisk, 0);
		if (bdev) {
			mutex_lock(&bdev->bd_inode->i_mutex);
			i_size_write(bdev->bd_inode, (loff_t)conf->mddev->array_size << 10);
			mutex_unlock(&bdev->bd_inode->i_mutex);
			bdput(bdev);
		}
		spin_lock_irq(&conf->device_lock);
		conf->expand_progress = MaxSector;
		spin_unlock_irq(&conf->device_lock);
		conf->mddev->reshape_position = MaxSector;

		/* read-ahead size must cover two whole stripes, which is
		 * 2 * (datadisks) * chunksize where 'n' is the number of raid devices
		 */
		{
			int data_disks = conf->previous_raid_disks - conf->max_degraded;
			int stripe = data_disks *
				(conf->mddev->chunk_size / PAGE_SIZE);
			if (conf->mddev->queue->backing_dev_info.ra_pages < 2 * stripe)
				conf->mddev->queue->backing_dev_info.ra_pages = 2 * stripe;
		}
	}
}

static void raid5_quiesce(mddev_t *mddev, int state)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);

	switch(state) {
	case 2: /* resume for a suspend */
		wake_up(&conf->wait_for_overlap);
		break;

	case 1: /* stop all writes */
		spin_lock_irq(&conf->device_lock);
		conf->quiesce = 1;
		wait_event_lock_irq(conf->wait_for_stripe,
				    atomic_read(&conf->active_stripes) == 0 &&
				    atomic_read(&conf->active_aligned_reads) == 0,
				    conf->device_lock, /* nothing */);
		spin_unlock_irq(&conf->device_lock);
		break;

	case 0: /* re-enable writes */
		spin_lock_irq(&conf->device_lock);
		conf->quiesce = 0;
		wake_up(&conf->wait_for_stripe);
		wake_up(&conf->wait_for_overlap);
		spin_unlock_irq(&conf->device_lock);
		break;
	}
}

static struct mdk_personality raid6_personality =
{
	.name		= "raid6",
	.level		= 6,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid5_add_disk,
	.hot_remove_disk= raid5_remove_disk,
	.spare_active	= raid5_spare_active,
	.sync_request	= sync_request,
	.resize		= raid5_resize,
#ifdef CONFIG_MD_RAID5_RESHAPE
	.check_reshape	= raid5_check_reshape,
	.start_reshape  = raid5_start_reshape,
#endif
	.quiesce	= raid5_quiesce,
};
static struct mdk_personality raid5_personality =
{
	.name		= "raid5",
	.level		= 5,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid5_add_disk,
	.hot_remove_disk= raid5_remove_disk,
	.spare_active	= raid5_spare_active,
	.sync_request	= sync_request,
	.resize		= raid5_resize,
#ifdef CONFIG_MD_RAID5_RESHAPE
	.check_reshape	= raid5_check_reshape,
	.start_reshape  = raid5_start_reshape,
#endif
	.quiesce	= raid5_quiesce,
};

static struct mdk_personality raid4_personality =
{
	.name		= "raid4",
	.level		= 4,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid5_add_disk,
	.hot_remove_disk= raid5_remove_disk,
	.spare_active	= raid5_spare_active,
	.sync_request	= sync_request,
	.resize		= raid5_resize,
#ifdef CONFIG_MD_RAID5_RESHAPE
	.check_reshape	= raid5_check_reshape,
	.start_reshape  = raid5_start_reshape,
#endif
	.quiesce	= raid5_quiesce,
};

static int __init raid5_init(void)
{
	int e;

	e = raid6_select_algo();
	if ( e )
		return e;
	register_md_personality(&raid6_personality);
	register_md_personality(&raid5_personality);
	register_md_personality(&raid4_personality);
	return 0;
}

static void raid5_exit(void)
{
	unregister_md_personality(&raid6_personality);
	unregister_md_personality(&raid5_personality);
	unregister_md_personality(&raid4_personality);
}

module_init(raid5_init);
module_exit(raid5_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS("md-personality-4"); /* RAID5 */
MODULE_ALIAS("md-raid5");
MODULE_ALIAS("md-raid4");
MODULE_ALIAS("md-level-5");
MODULE_ALIAS("md-level-4");
MODULE_ALIAS("md-personality-8"); /* RAID6 */
MODULE_ALIAS("md-raid6");
MODULE_ALIAS("md-level-6");

/* This used to be two separate modules, they were: */
MODULE_ALIAS("raid5");
MODULE_ALIAS("raid6");
