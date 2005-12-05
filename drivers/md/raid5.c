/*
 * raid5.c : Multiple Devices driver for Linux
 *	   Copyright (C) 1996, 1997 Ingo Molnar, Miguel de Icaza, Gadi Oxman
 *	   Copyright (C) 1999, 2000 Ingo Molnar
 *
 * RAID-5 management functions.
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


#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/raid/raid5.h>
#include <linux/highmem.h>
#include <linux/bitops.h>
#include <asm/atomic.h>

#include <linux/raid/bitmap.h>

/*
 * Stripe cache
 */

#define NR_STRIPES		256
#define STRIPE_SIZE		PAGE_SIZE
#define STRIPE_SHIFT		(PAGE_SHIFT - 9)
#define STRIPE_SECTORS		(STRIPE_SIZE>>9)
#define	IO_THRESHOLD		1
#define HASH_PAGES		1
#define HASH_PAGES_ORDER	0
#define NR_HASH			(HASH_PAGES * PAGE_SIZE / sizeof(struct stripe_head *))
#define HASH_MASK		(NR_HASH - 1)

#define stripe_hash(conf, sect)	((conf)->stripe_hashtbl[((sect) >> STRIPE_SHIFT) & HASH_MASK])

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

static void print_raid5_conf (raid5_conf_t *conf);

static inline void __release_stripe(raid5_conf_t *conf, struct stripe_head *sh)
{
	if (atomic_dec_and_test(&sh->count)) {
		if (!list_empty(&sh->lru))
			BUG();
		if (atomic_read(&conf->active_stripes)==0)
			BUG();
		if (test_bit(STRIPE_HANDLE, &sh->state)) {
			if (test_bit(STRIPE_DELAYED, &sh->state))
				list_add_tail(&sh->lru, &conf->delayed_list);
			else if (test_bit(STRIPE_BIT_DELAY, &sh->state) &&
				 conf->seq_write == sh->bm_seq)
				list_add_tail(&sh->lru, &conf->bitmap_list);
			else {
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
			list_add_tail(&sh->lru, &conf->inactive_list);
			atomic_dec(&conf->active_stripes);
			if (!conf->inactive_blocked ||
			    atomic_read(&conf->active_stripes) < (NR_STRIPES*3/4))
				wake_up(&conf->wait_for_stripe);
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

static void remove_hash(struct stripe_head *sh)
{
	PRINTK("remove_hash(), stripe %llu\n", (unsigned long long)sh->sector);

	if (sh->hash_pprev) {
		if (sh->hash_next)
			sh->hash_next->hash_pprev = sh->hash_pprev;
		*sh->hash_pprev = sh->hash_next;
		sh->hash_pprev = NULL;
	}
}

static __inline__ void insert_hash(raid5_conf_t *conf, struct stripe_head *sh)
{
	struct stripe_head **shp = &stripe_hash(conf, sh->sector);

	PRINTK("insert_hash(), stripe %llu\n", (unsigned long long)sh->sector);

	CHECK_DEVLOCK();
	if ((sh->hash_next = *shp) != NULL)
		(*shp)->hash_pprev = &sh->hash_next;
	*shp = sh;
	sh->hash_pprev = shp;
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
		page_cache_release(p);
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

static inline void init_stripe(struct stripe_head *sh, sector_t sector, int pd_idx)
{
	raid5_conf_t *conf = sh->raid_conf;
	int disks = conf->raid_disks, i;

	if (atomic_read(&sh->count) != 0)
		BUG();
	if (test_bit(STRIPE_HANDLE, &sh->state))
		BUG();
	
	CHECK_DEVLOCK();
	PRINTK("init_stripe called, stripe %llu\n", 
		(unsigned long long)sh->sector);

	remove_hash(sh);
	
	sh->sector = sector;
	sh->pd_idx = pd_idx;
	sh->state = 0;

	for (i=disks; i--; ) {
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

static struct stripe_head *__find_stripe(raid5_conf_t *conf, sector_t sector)
{
	struct stripe_head *sh;

	CHECK_DEVLOCK();
	PRINTK("__find_stripe, sector %llu\n", (unsigned long long)sector);
	for (sh = stripe_hash(conf, sector); sh; sh = sh->hash_next)
		if (sh->sector == sector)
			return sh;
	PRINTK("__stripe %llu not in cache\n", (unsigned long long)sector);
	return NULL;
}

static void unplug_slaves(mddev_t *mddev);
static void raid5_unplug_device(request_queue_t *q);

static struct stripe_head *get_active_stripe(raid5_conf_t *conf, sector_t sector,
					     int pd_idx, int noblock) 
{
	struct stripe_head *sh;

	PRINTK("get_stripe, sector %llu\n", (unsigned long long)sector);

	spin_lock_irq(&conf->device_lock);

	do {
		wait_event_lock_irq(conf->wait_for_stripe,
				    conf->quiesce == 0,
				    conf->device_lock, /* nothing */);
		sh = __find_stripe(conf, sector);
		if (!sh) {
			if (!conf->inactive_blocked)
				sh = get_free_stripe(conf);
			if (noblock && sh == NULL)
				break;
			if (!sh) {
				conf->inactive_blocked = 1;
				wait_event_lock_irq(conf->wait_for_stripe,
						    !list_empty(&conf->inactive_list) &&
						    (atomic_read(&conf->active_stripes) < (NR_STRIPES *3/4)
						     || !conf->inactive_blocked),
						    conf->device_lock,
						    unplug_slaves(conf->mddev);
					);
				conf->inactive_blocked = 0;
			} else
				init_stripe(sh, sector, pd_idx);
		} else {
			if (atomic_read(&sh->count)) {
				if (!list_empty(&sh->lru))
					BUG();
			} else {
				if (!test_bit(STRIPE_HANDLE, &sh->state))
					atomic_inc(&conf->active_stripes);
				if (list_empty(&sh->lru))
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
	/* we just created an active stripe so... */
	atomic_set(&sh->count, 1);
	atomic_inc(&conf->active_stripes);
	INIT_LIST_HEAD(&sh->lru);
	release_stripe(sh);
	return 1;
}

static int grow_stripes(raid5_conf_t *conf, int num)
{
	kmem_cache_t *sc;
	int devs = conf->raid_disks;

	sprintf(conf->cache_name, "raid5/%s", mdname(conf->mddev));

	sc = kmem_cache_create(conf->cache_name, 
			       sizeof(struct stripe_head)+(devs-1)*sizeof(struct r5dev),
			       0, 0, NULL, NULL);
	if (!sc)
		return 1;
	conf->slab_cache = sc;
	while (num--) {
		if (!grow_one_stripe(conf))
			return 1;
	}
	return 0;
}

static int drop_one_stripe(raid5_conf_t *conf)
{
	struct stripe_head *sh;

	spin_lock_irq(&conf->device_lock);
	sh = get_free_stripe(conf);
	spin_unlock_irq(&conf->device_lock);
	if (!sh)
		return 0;
	if (atomic_read(&sh->count))
		BUG();
	shrink_buffers(sh, conf->raid_disks);
	kmem_cache_free(conf->slab_cache, sh);
	atomic_dec(&conf->active_stripes);
	return 1;
}

static void shrink_stripes(raid5_conf_t *conf)
{
	while (drop_one_stripe(conf))
		;

	kmem_cache_destroy(conf->slab_cache);
	conf->slab_cache = NULL;
}

static int raid5_end_read_request(struct bio * bi, unsigned int bytes_done,
				   int error)
{
 	struct stripe_head *sh = bi->bi_private;
	raid5_conf_t *conf = sh->raid_conf;
	int disks = conf->raid_disks, i;
	int uptodate = test_bit(BIO_UPTODATE, &bi->bi_flags);

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
#if 0
		struct bio *bio;
		unsigned long flags;
		spin_lock_irqsave(&conf->device_lock, flags);
		/* we can return a buffer if we bypassed the cache or
		 * if the top buffer is not in highmem.  If there are
		 * multiple buffers, leave the extra work to
		 * handle_stripe
		 */
		buffer = sh->bh_read[i];
		if (buffer &&
		    (!PageHighMem(buffer->b_page)
		     || buffer->b_page == bh->b_page )
			) {
			sh->bh_read[i] = buffer->b_reqnext;
			buffer->b_reqnext = NULL;
		} else
			buffer = NULL;
		spin_unlock_irqrestore(&conf->device_lock, flags);
		if (sh->bh_page[i]==bh->b_page)
			set_buffer_uptodate(bh);
		if (buffer) {
			if (buffer->b_page != bh->b_page)
				memcpy(buffer->b_data, bh->b_data, bh->b_size);
			buffer->b_end_io(buffer, 1);
		}
#else
		set_bit(R5_UPTODATE, &sh->dev[i].flags);
#endif
		if (test_bit(R5_ReadError, &sh->dev[i].flags)) {
			printk("R5: read error corrected!!\n");
			clear_bit(R5_ReadError, &sh->dev[i].flags);
			clear_bit(R5_ReWrite, &sh->dev[i].flags);
		}
		if (atomic_read(&conf->disks[i].rdev->read_errors))
			atomic_set(&conf->disks[i].rdev->read_errors, 0);
	} else {
		int retry = 0;
		clear_bit(R5_UPTODATE, &sh->dev[i].flags);
		atomic_inc(&conf->disks[i].rdev->read_errors);
		if (conf->mddev->degraded)
			printk("R5: read error not correctable.\n");
		else if (test_bit(R5_ReWrite, &sh->dev[i].flags))
			/* Oh, no!!! */
			printk("R5: read error NOT corrected!!\n");
		else if (atomic_read(&conf->disks[i].rdev->read_errors)
			 > conf->max_nr_stripes)
			printk("raid5: Too many read errors, failing device.\n");
		else
			retry = 1;
		if (retry)
			set_bit(R5_ReadError, &sh->dev[i].flags);
		else {
			clear_bit(R5_ReadError, &sh->dev[i].flags);
			clear_bit(R5_ReWrite, &sh->dev[i].flags);
			md_error(conf->mddev, conf->disks[i].rdev);
		}
	}
	rdev_dec_pending(conf->disks[i].rdev, conf->mddev);
#if 0
	/* must restore b_page before unlocking buffer... */
	if (sh->bh_page[i] != bh->b_page) {
		bh->b_page = sh->bh_page[i];
		bh->b_data = page_address(bh->b_page);
		clear_buffer_uptodate(bh);
	}
#endif
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
	int disks = conf->raid_disks, i;
	unsigned long flags;
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

	spin_lock_irqsave(&conf->device_lock, flags);
	if (!uptodate)
		md_error(conf->mddev, conf->disks[i].rdev);

	rdev_dec_pending(conf->disks[i].rdev, conf->mddev);
	
	clear_bit(R5_LOCKED, &sh->dev[i].flags);
	set_bit(STRIPE_HANDLE, &sh->state);
	__release_stripe(conf, sh);
	spin_unlock_irqrestore(&conf->device_lock, flags);
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
	if (i != sh->pd_idx)
		dev->sector = compute_blocknr(sh, i);
}

static void error(mddev_t *mddev, mdk_rdev_t *rdev)
{
	char b[BDEVNAME_SIZE];
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	PRINTK("raid5: error called\n");

	if (!test_bit(Faulty, &rdev->flags)) {
		mddev->sb_dirty = 1;
		if (test_bit(In_sync, &rdev->flags)) {
			conf->working_disks--;
			mddev->degraded++;
			conf->failed_disks++;
			clear_bit(In_sync, &rdev->flags);
			/*
			 * if recovery was running, make sure it aborts.
			 */
			set_bit(MD_RECOVERY_ERR, &mddev->recovery);
		}
		set_bit(Faulty, &rdev->flags);
		printk (KERN_ALERT
			"raid5: Disk failure on %s, disabling device."
			" Operation continuing on %d devices\n",
			bdevname(rdev->bdev,b), conf->working_disks);
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
	if (conf->level == 4)
		*pd_idx = data_disks;
	else switch (conf->algorithm) {
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
			printk("raid5: unsupported algorithm %d\n",
				conf->algorithm);
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
	int raid_disks = conf->raid_disks, data_disks = raid_disks - 1;
	sector_t new_sector = sh->sector, check;
	int sectors_per_chunk = conf->chunk_size >> 9;
	sector_t stripe;
	int chunk_offset;
	int chunk_number, dummy1, dummy2, dd_idx = i;
	sector_t r_sector;

	chunk_offset = sector_div(new_sector, sectors_per_chunk);
	stripe = new_sector;
	BUG_ON(new_sector != stripe);

	
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
			printk("raid5: unsupported algorithm %d\n",
				conf->algorithm);
	}

	chunk_number = stripe * data_disks + i;
	r_sector = (sector_t)chunk_number * sectors_per_chunk + chunk_offset;

	check = raid5_compute_sector (r_sector, raid_disks, data_disks, &dummy1, &dummy2, conf);
	if (check != sh->sector || dummy1 != dd_idx || dummy2 != sh->pd_idx) {
		printk("compute_blocknr: map not correct\n");
		return 0;
	}
	return r_sector;
}



/*
 * Copy data between a page in the stripe cache, and a bio.
 * There are no alignment or size guarantees between the page or the
 * bio except that there is some overlap.
 * All iovecs in the bio must be considered.
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
	raid5_conf_t *conf = sh->raid_conf;
	int i, count, disks = conf->raid_disks;
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
			printk("compute_block() %d, stripe %llu, %d"
				" not present\n", dd_idx,
				(unsigned long long)sh->sector, i);

		check_xor();
	}
	if (count != 1)
		xor_block(count, STRIPE_SIZE, ptr);
	set_bit(R5_UPTODATE, &sh->dev[dd_idx].flags);
}

static void compute_parity(struct stripe_head *sh, int method)
{
	raid5_conf_t *conf = sh->raid_conf;
	int i, pd_idx = sh->pd_idx, disks = conf->raid_disks, count;
	void *ptr[MAX_XOR_BLOCKS];
	struct bio *chosen;

	PRINTK("compute_parity, stripe %llu, method %d\n",
		(unsigned long long)sh->sector, method);

	count = 1;
	ptr[0] = page_address(sh->dev[pd_idx].page);
	switch(method) {
	case READ_MODIFY_WRITE:
		if (!test_bit(R5_UPTODATE, &sh->dev[pd_idx].flags))
			BUG();
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

				if (sh->dev[i].written) BUG();
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

				if (sh->dev[i].written) BUG();
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

	if (*bip && bi->bi_next && (*bip) != bi->bi_next)
		BUG();
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
		sh->bm_seq = conf->seq_write;
		bitmap_startwrite(conf->mddev->bitmap, sh->sector,
				  STRIPE_SECTORS, 0);
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
 
static void handle_stripe(struct stripe_head *sh)
{
	raid5_conf_t *conf = sh->raid_conf;
	int disks = conf->raid_disks;
	struct bio *return_bi= NULL;
	struct bio *bi;
	int i;
	int syncing;
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
	/* Now to look around and see what can be done */

	for (i=disks; i--; ) {
		mdk_rdev_t *rdev;
		dev = &sh->dev[i];
		clear_bit(R5_Insync, &dev->flags);
		clear_bit(R5_Syncio, &dev->flags);

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
		rdev = conf->disks[i].rdev; /* FIXME, should I be looking rdev */
		if (!rdev || !test_bit(In_sync, &rdev->flags)) {
			/* The ReadError flag wil just be confusing now */
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
				mdk_rdev_t *rdev = conf->disks[i].rdev;
				if (rdev && test_bit(In_sync, &rdev->flags))
					/* multiple read failures in one stripe */
					md_error(conf->mddev, rdev);
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
	if (to_read || non_overwrite || (syncing && (uptodate < disks))) {
		for (i=disks; i--;) {
			dev = &sh->dev[i];
			if (!test_bit(R5_LOCKED, &dev->flags) && !test_bit(R5_UPTODATE, &dev->flags) &&
			    (dev->toread ||
			     (dev->towrite && !test_bit(R5_OVERWRITE, &dev->flags)) ||
			     syncing ||
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
#if 0
					/* if I am just reading this block and we don't have
					   a failed drive, or any pending writes then sidestep the cache */
					if (sh->bh_read[i] && !sh->bh_read[i]->b_reqnext &&
					    ! syncing && !failed && !to_write) {
						sh->bh_cache[i]->b_page =  sh->bh_read[i]->b_page;
						sh->bh_cache[i]->b_data =  sh->bh_read[i]->b_data;
					}
#endif
					locked++;
					PRINTK("Reading block %d (sync=%d)\n", 
						i, syncing);
					if (syncing)
						md_sync_acct(conf->disks[i].rdev->bdev,
							     STRIPE_SECTORS);
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
#if 0
|| sh->bh_page[i]!=bh->b_page
#endif
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
#if 0
|| sh->bh_page[i] != bh->b_page
#endif
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
			compute_parity(sh, rcw==0 ? RECONSTRUCT_WRITE : READ_MODIFY_WRITE);
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
	    !test_bit(STRIPE_INSYNC, &sh->state) && failed <= 1) {
		set_bit(STRIPE_HANDLE, &sh->state);
		if (failed == 0) {
			char *pagea;
			if (uptodate != disks)
				BUG();
			compute_parity(sh, CHECK_PARITY);
			uptodate--;
			pagea = page_address(sh->dev[sh->pd_idx].page);
			if ((*(u32*)pagea) == 0 &&
			    !memcmp(pagea, pagea+4, STRIPE_SIZE-4)) {
				/* parity is correct (on disc, not in buffer any more) */
				set_bit(STRIPE_INSYNC, &sh->state);
			} else {
				conf->mddev->resync_mismatches += STRIPE_SECTORS;
				if (test_bit(MD_RECOVERY_CHECK, &conf->mddev->recovery))
					/* don't try to repair!! */
					set_bit(STRIPE_INSYNC, &sh->state);
			}
		}
		if (!test_bit(STRIPE_INSYNC, &sh->state)) {
			if (failed==0)
				failed_num = sh->pd_idx;
			/* should be able to compute the missing block and write it to spare */
			if (!test_bit(R5_UPTODATE, &sh->dev[failed_num].flags)) {
				if (uptodate+1 != disks)
					BUG();
				compute_block(sh, failed_num);
				uptodate++;
			}
			if (uptodate != disks)
				BUG();
			dev = &sh->dev[failed_num];
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantwrite, &dev->flags);
			clear_bit(STRIPE_DEGRADED, &sh->state);
			locked++;
			set_bit(STRIPE_INSYNC, &sh->state);
			set_bit(R5_Syncio, &dev->flags);
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
		} else {
			/* let's read it back */
			set_bit(R5_Wantread, &dev->flags);
			set_bit(R5_LOCKED, &dev->flags);
		}
	}

	spin_unlock(&sh->lock);

	while ((bi=return_bi)) {
		int bytes = bi->bi_size;

		return_bi = bi->bi_next;
		bi->bi_next = NULL;
		bi->bi_size = 0;
		bi->bi_end_io(bi, bytes, 0);
	}
	for (i=disks; i-- ;) {
		int rw;
		struct bio *bi;
		mdk_rdev_t *rdev;
		if (test_and_clear_bit(R5_Wantwrite, &sh->dev[i].flags))
			rw = 1;
		else if (test_and_clear_bit(R5_Wantread, &sh->dev[i].flags))
			rw = 0;
		else
			continue;
 
		bi = &sh->dev[i].req;
 
		bi->bi_rw = rw;
		if (rw)
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
			if (test_bit(R5_Syncio, &sh->dev[i].flags))
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
			generic_make_request(bi);
		} else {
			if (rw == 1)
				set_bit(STRIPE_DEGRADED, &sh->state);
			PRINTK("skip op %ld on disc %d for sector %llu\n",
				bi->bi_rw, i, (unsigned long long)sh->sector);
			clear_bit(R5_LOCKED, &sh->dev[i].flags);
			set_bit(STRIPE_HANDLE, &sh->state);
		}
	}
}

static inline void raid5_activate_delayed(raid5_conf_t *conf)
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

static inline void activate_bit_delay(raid5_conf_t *conf)
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

static inline void raid5_plug_device(raid5_conf_t *conf)
{
	spin_lock_irq(&conf->device_lock);
	blk_plug_device(conf->mddev->queue);
	spin_unlock_irq(&conf->device_lock);
}

static int make_request (request_queue_t *q, struct bio * bi)
{
	mddev_t *mddev = q->queuedata;
	raid5_conf_t *conf = mddev_to_conf(mddev);
	const unsigned int raid_disks = conf->raid_disks;
	const unsigned int data_disks = raid_disks - 1;
	unsigned int dd_idx, pd_idx;
	sector_t new_sector;
	sector_t logical_sector, last_sector;
	struct stripe_head *sh;
	const int rw = bio_data_dir(bi);

	if (unlikely(bio_barrier(bi))) {
		bio_endio(bi, bi->bi_size, -EOPNOTSUPP);
		return 0;
	}

	md_write_start(mddev, bi);

	disk_stat_inc(mddev->gendisk, ios[rw]);
	disk_stat_add(mddev->gendisk, sectors[rw], bio_sectors(bi));

	logical_sector = bi->bi_sector & ~((sector_t)STRIPE_SECTORS-1);
	last_sector = bi->bi_sector + (bi->bi_size>>9);
	bi->bi_next = NULL;
	bi->bi_phys_segments = 1;	/* over-loaded to count active stripes */

	for (;logical_sector < last_sector; logical_sector += STRIPE_SECTORS) {
		DEFINE_WAIT(w);
		
		new_sector = raid5_compute_sector(logical_sector,
						  raid_disks, data_disks, &dd_idx, &pd_idx, conf);

		PRINTK("raid5: make_request, sector %llu logical %llu\n",
			(unsigned long long)new_sector, 
			(unsigned long long)logical_sector);

	retry:
		prepare_to_wait(&conf->wait_for_overlap, &w, TASK_UNINTERRUPTIBLE);
		sh = get_active_stripe(conf, new_sector, pd_idx, (bi->bi_rw&RWA_MASK));
		if (sh) {
			if (!add_stripe_bio(sh, bi, dd_idx, (bi->bi_rw&RW_MASK))) {
				/* Add failed due to overlap.  Flush everything
				 * and wait a while
				 */
				raid5_unplug_device(mddev->queue);
				release_stripe(sh);
				schedule();
				goto retry;
			}
			finish_wait(&conf->wait_for_overlap, &w);
			raid5_plug_device(conf);
			handle_stripe(sh);
			release_stripe(sh);

		} else {
			/* cannot get stripe for read-ahead, just give-up */
			clear_bit(BIO_UPTODATE, &bi->bi_flags);
			finish_wait(&conf->wait_for_overlap, &w);
			break;
		}
			
	}
	spin_lock_irq(&conf->device_lock);
	if (--bi->bi_phys_segments == 0) {
		int bytes = bi->bi_size;

		if ( bio_data_dir(bi) == WRITE )
			md_write_end(mddev);
		bi->bi_size = 0;
		bi->bi_end_io(bi, bytes, 0);
	}
	spin_unlock_irq(&conf->device_lock);
	return 0;
}

/* FIXME go_faster isn't used */
static sector_t sync_request(mddev_t *mddev, sector_t sector_nr, int *skipped, int go_faster)
{
	raid5_conf_t *conf = (raid5_conf_t *) mddev->private;
	struct stripe_head *sh;
	int sectors_per_chunk = conf->chunk_size >> 9;
	sector_t x;
	unsigned long stripe;
	int chunk_offset;
	int dd_idx, pd_idx;
	sector_t first_sector;
	int raid_disks = conf->raid_disks;
	int data_disks = raid_disks-1;
	sector_t max_sector = mddev->size << 1;
	int sync_blocks;

	if (sector_nr >= max_sector) {
		/* just being told to finish up .. nothing much to do */
		unplug_slaves(mddev);

		if (mddev->curr_resync < max_sector) /* aborted */
			bitmap_end_sync(mddev->bitmap, mddev->curr_resync,
					&sync_blocks, 1);
		else /* compelted sync */
			conf->fullsync = 0;
		bitmap_close_sync(mddev->bitmap);

		return 0;
	}
	/* if there is 1 or more failed drives and we are trying
	 * to resync, then assert that we are finished, because there is
	 * nothing we can do.
	 */
	if (mddev->degraded >= 1 && test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
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

	x = sector_nr;
	chunk_offset = sector_div(x, sectors_per_chunk);
	stripe = x;
	BUG_ON(x != stripe);

	first_sector = raid5_compute_sector((sector_t)stripe*data_disks*sectors_per_chunk
		+ chunk_offset, raid_disks, data_disks, &dd_idx, &pd_idx, conf);
	sh = get_active_stripe(conf, sector_nr, pd_idx, 1);
	if (sh == NULL) {
		sh = get_active_stripe(conf, sector_nr, pd_idx, 0);
		/* make sure we don't swamp the stripe cache if someone else
		 * is trying to get access 
		 */
		schedule_timeout_uninterruptible(1);
	}
	bitmap_start_sync(mddev->bitmap, sector_nr, &sync_blocks, 0);
	spin_lock(&sh->lock);	
	set_bit(STRIPE_SYNCING, &sh->state);
	clear_bit(STRIPE_INSYNC, &sh->state);
	spin_unlock(&sh->lock);

	handle_stripe(sh);
	release_stripe(sh);

	return STRIPE_SECTORS;
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

		if (conf->seq_flush - conf->seq_write > 0) {
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

		if (list_empty(&conf->handle_list))
			break;

		first = conf->handle_list.next;
		sh = list_entry(first, struct stripe_head, lru);

		list_del_init(first);
		atomic_inc(&sh->count);
		if (atomic_read(&sh->count)!= 1)
			BUG();
		spin_unlock_irq(&conf->device_lock);
		
		handled++;
		handle_stripe(sh);
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

	if (mddev->level != 5 && mddev->level != 4) {
		printk("raid5: %s: raid level not set to 4/5 (%d)\n", mdname(mddev), mddev->level);
		return -EIO;
	}

	mddev->private = kmalloc (sizeof (raid5_conf_t)
				  + mddev->raid_disks * sizeof(struct disk_info),
				  GFP_KERNEL);
	if ((conf = mddev->private) == NULL)
		goto abort;
	memset (conf, 0, sizeof (*conf) + mddev->raid_disks * sizeof(struct disk_info) );
	conf->mddev = mddev;

	if ((conf->stripe_hashtbl = (struct stripe_head **) __get_free_pages(GFP_ATOMIC, HASH_PAGES_ORDER)) == NULL)
		goto abort;
	memset(conf->stripe_hashtbl, 0, HASH_PAGES * PAGE_SIZE);

	spin_lock_init(&conf->device_lock);
	init_waitqueue_head(&conf->wait_for_stripe);
	init_waitqueue_head(&conf->wait_for_overlap);
	INIT_LIST_HEAD(&conf->handle_list);
	INIT_LIST_HEAD(&conf->delayed_list);
	INIT_LIST_HEAD(&conf->bitmap_list);
	INIT_LIST_HEAD(&conf->inactive_list);
	atomic_set(&conf->active_stripes, 0);
	atomic_set(&conf->preread_active_stripes, 0);

	PRINTK("raid5: run(%s) called.\n", mdname(mddev));

	ITERATE_RDEV(mddev,rdev,tmp) {
		raid_disk = rdev->raid_disk;
		if (raid_disk >= mddev->raid_disks
		    || raid_disk < 0)
			continue;
		disk = conf->disks + raid_disk;

		disk->rdev = rdev;

		if (test_bit(In_sync, &rdev->flags)) {
			char b[BDEVNAME_SIZE];
			printk(KERN_INFO "raid5: device %s operational as raid"
				" disk %d\n", bdevname(rdev->bdev,b),
				raid_disk);
			conf->working_disks++;
		}
	}

	conf->raid_disks = mddev->raid_disks;
	/*
	 * 0 for a fully functional array, 1 for a degraded array.
	 */
	mddev->degraded = conf->failed_disks = conf->raid_disks - conf->working_disks;
	conf->mddev = mddev;
	conf->chunk_size = mddev->chunk_size;
	conf->level = mddev->level;
	conf->algorithm = mddev->layout;
	conf->max_nr_stripes = NR_STRIPES;

	/* device size must be a multiple of chunk size */
	mddev->size &= ~(mddev->chunk_size/1024 -1);
	mddev->resync_max_sectors = mddev->size << 1;

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
	if (mddev->degraded > 1) {
		printk(KERN_ERR "raid5: not enough operational devices for %s"
			" (%d/%d failed)\n",
			mdname(mddev), conf->failed_disks, conf->raid_disks);
		goto abort;
	}

	if (mddev->degraded == 1 &&
	    mddev->recovery_cp != MaxSector) {
		printk(KERN_ERR 
			"raid5: cannot start dirty degraded array for %s\n",
			mdname(mddev));
		goto abort;
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

	/* read-ahead size must cover two whole stripes, which is
	 * 2 * (n-1) * chunksize where 'n' is the number of raid devices
	 */
	{
		int stripe = (mddev->raid_disks-1) * mddev->chunk_size
			/ PAGE_CACHE_SIZE;
		if (mddev->queue->backing_dev_info.ra_pages < 2 * stripe)
			mddev->queue->backing_dev_info.ra_pages = 2 * stripe;
	}

	/* Ok, everything is just fine now */
	sysfs_create_group(&mddev->kobj, &raid5_attrs_group);

	if (mddev->bitmap)
		mddev->thread->timeout = mddev->bitmap->daemon_sleep * HZ;

	mddev->queue->unplug_fn = raid5_unplug_device;
	mddev->queue->issue_flush_fn = raid5_issue_flush;

	mddev->array_size =  mddev->size * (mddev->raid_disks - 1);
	return 0;
abort:
	if (conf) {
		print_raid5_conf(conf);
		if (conf->stripe_hashtbl)
			free_pages((unsigned long) conf->stripe_hashtbl,
							HASH_PAGES_ORDER);
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
	free_pages((unsigned long) conf->stripe_hashtbl, HASH_PAGES_ORDER);
	blk_sync_queue(mddev->queue); /* the unplug fn references 'conf'*/
	sysfs_remove_group(&mddev->kobj, &raid5_attrs_group);
	kfree(conf);
	mddev->private = NULL;
	return 0;
}

#if RAID5_DEBUG
static void print_sh (struct stripe_head *sh)
{
	int i;

	printk("sh %llu, pd_idx %d, state %ld.\n",
		(unsigned long long)sh->sector, sh->pd_idx, sh->state);
	printk("sh %llu,  count %d.\n",
		(unsigned long long)sh->sector, atomic_read(&sh->count));
	printk("sh %llu, ", (unsigned long long)sh->sector);
	for (i = 0; i < sh->raid_conf->raid_disks; i++) {
		printk("(cache%d: %p %ld) ", 
			i, sh->dev[i].page, sh->dev[i].flags);
	}
	printk("\n");
}

static void printall (raid5_conf_t *conf)
{
	struct stripe_head *sh;
	int i;

	spin_lock_irq(&conf->device_lock);
	for (i = 0; i < NR_HASH; i++) {
		sh = conf->stripe_hashtbl[i];
		for (; sh; sh = sh->hash_next) {
			if (sh->raid_conf != conf)
				continue;
			print_sh(sh);
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
	seq_printf (seq, " [%d/%d] [", conf->raid_disks, conf->working_disks);
	for (i = 0; i < conf->raid_disks; i++)
		seq_printf (seq, "%s",
			       conf->disks[i].rdev &&
			       test_bit(In_sync, &conf->disks[i].rdev->flags) ? "U" : "_");
	seq_printf (seq, "]");
#if RAID5_DEBUG
#define D(x) \
	seq_printf (seq, "<"#x":%d>", atomic_read(&conf->x))
	printall(conf);
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
	printk(" --- rd:%d wd:%d fd:%d\n", conf->raid_disks,
		 conf->working_disks, conf->failed_disks);

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
		    && !test_bit(In_sync, &tmp->rdev->flags)) {
			mddev->degraded--;
			conf->failed_disks--;
			conf->working_disks++;
			set_bit(In_sync, &tmp->rdev->flags);
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

	if (mddev->degraded > 1)
		/* no point adding a device */
		return 0;

	/*
	 * find the disk ...
	 */
	for (disk=0; disk < mddev->raid_disks; disk++)
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
	sectors &= ~((sector_t)mddev->chunk_size/512 - 1);
	mddev->array_size = (sectors * (mddev->raid_disks-1))>>1;
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

static void raid5_quiesce(mddev_t *mddev, int state)
{
	raid5_conf_t *conf = mddev_to_conf(mddev);

	switch(state) {
	case 1: /* stop all writes */
		spin_lock_irq(&conf->device_lock);
		conf->quiesce = 1;
		wait_event_lock_irq(conf->wait_for_stripe,
				    atomic_read(&conf->active_stripes) == 0,
				    conf->device_lock, /* nothing */);
		spin_unlock_irq(&conf->device_lock);
		break;

	case 0: /* re-enable writes */
		spin_lock_irq(&conf->device_lock);
		conf->quiesce = 0;
		wake_up(&conf->wait_for_stripe);
		spin_unlock_irq(&conf->device_lock);
		break;
	}
	if (mddev->thread) {
		if (mddev->bitmap)
			mddev->thread->timeout = mddev->bitmap->daemon_sleep * HZ;
		else
			mddev->thread->timeout = MAX_SCHEDULE_TIMEOUT;
		md_wakeup_thread(mddev->thread);
	}
}
static mdk_personality_t raid5_personality=
{
	.name		= "raid5",
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
	.quiesce	= raid5_quiesce,
};

static int __init raid5_init (void)
{
	return register_md_personality (RAID5, &raid5_personality);
}

static void raid5_exit (void)
{
	unregister_md_personality (RAID5);
}

module_init(raid5_init);
module_exit(raid5_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS("md-personality-4"); /* RAID5 */
