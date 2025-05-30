// SPDX-License-Identifier: GPL-2.0
/* Maximum size of each resync request */
#define RESYNC_BLOCK_SIZE (64*1024)
#define RESYNC_PAGES ((RESYNC_BLOCK_SIZE + PAGE_SIZE-1) / PAGE_SIZE)

/*
 * Number of guaranteed raid bios in case of extreme VM load:
 */
#define	NR_RAID_BIOS 256

/* when we get a read error on a read-only array, we redirect to another
 * device without failing the first device, or trying to over-write to
 * correct the read error.  To keep track of bad blocks on a per-bio
 * level, we store IO_BLOCKED in the appropriate 'bios' pointer
 */
#define IO_BLOCKED ((struct bio *)1)
/* When we successfully write to a known bad-block, we need to remove the
 * bad-block marking which must be done from process context.  So we record
 * the success by setting devs[n].bio to IO_MADE_GOOD
 */
#define IO_MADE_GOOD ((struct bio *)2)

#define BIO_SPECIAL(bio) ((unsigned long)bio <= 2)
#define MAX_PLUG_BIO 32

/* for managing resync I/O pages */
struct resync_pages {
	void		*raid_bio;
	struct page	*pages[RESYNC_PAGES];
};

struct raid1_plug_cb {
	struct blk_plug_cb	cb;
	struct bio_list		pending;
	unsigned int		count;
};

static void rbio_pool_free(void *rbio, void *data)
{
	kfree(rbio);
}

static inline int resync_alloc_pages(struct resync_pages *rp,
				     gfp_t gfp_flags)
{
	int i;

	for (i = 0; i < RESYNC_PAGES; i++) {
		rp->pages[i] = alloc_page(gfp_flags);
		if (!rp->pages[i])
			goto out_free;
	}

	return 0;

out_free:
	while (--i >= 0)
		put_page(rp->pages[i]);
	return -ENOMEM;
}

static inline void resync_free_pages(struct resync_pages *rp)
{
	int i;

	for (i = 0; i < RESYNC_PAGES; i++)
		put_page(rp->pages[i]);
}

static inline void resync_get_all_pages(struct resync_pages *rp)
{
	int i;

	for (i = 0; i < RESYNC_PAGES; i++)
		get_page(rp->pages[i]);
}

static inline struct page *resync_fetch_page(struct resync_pages *rp,
					     unsigned idx)
{
	if (WARN_ON_ONCE(idx >= RESYNC_PAGES))
		return NULL;
	return rp->pages[idx];
}

/*
 * 'strct resync_pages' stores actual pages used for doing the resync
 *  IO, and it is per-bio, so make .bi_private points to it.
 */
static inline struct resync_pages *get_resync_pages(struct bio *bio)
{
	return bio->bi_private;
}

/* generally called after bio_reset() for reseting bvec */
static void md_bio_reset_resync_pages(struct bio *bio, struct resync_pages *rp,
			       int size)
{
	int idx = 0;

	/* initialize bvec table again */
	do {
		struct page *page = resync_fetch_page(rp, idx);
		int len = min_t(int, size, PAGE_SIZE);

		if (WARN_ON(!bio_add_page(bio, page, len, 0))) {
			bio->bi_status = BLK_STS_RESOURCE;
			bio_endio(bio);
			return;
		}

		size -= len;
	} while (idx++ < RESYNC_PAGES && size > 0);
}


static inline void raid1_submit_write(struct bio *bio)
{
	struct md_rdev *rdev = (void *)bio->bi_bdev;

	bio->bi_next = NULL;
	bio_set_dev(bio, rdev->bdev);
	if (test_bit(Faulty, &rdev->flags))
		bio_io_error(bio);
	else if (unlikely(bio_op(bio) ==  REQ_OP_DISCARD &&
			  !bdev_max_discard_sectors(bio->bi_bdev)))
		/* Just ignore it */
		bio_endio(bio);
	else
		submit_bio_noacct(bio);
}

static inline bool raid1_add_bio_to_plug(struct mddev *mddev, struct bio *bio,
				      blk_plug_cb_fn unplug, int copies)
{
	struct raid1_plug_cb *plug = NULL;
	struct blk_plug_cb *cb;

	/*
	 * If bitmap is not enabled, it's safe to submit the io directly, and
	 * this can get optimal performance.
	 */
	if (!mddev->bitmap_ops->enabled(mddev)) {
		raid1_submit_write(bio);
		return true;
	}

	cb = blk_check_plugged(unplug, mddev, sizeof(*plug));
	if (!cb)
		return false;

	plug = container_of(cb, struct raid1_plug_cb, cb);
	bio_list_add(&plug->pending, bio);
	if (++plug->count / MAX_PLUG_BIO >= copies) {
		list_del(&cb->list);
		cb->callback(cb, false);
	}


	return true;
}

/*
 * current->bio_list will be set under submit_bio() context, in this case bitmap
 * io will be added to the list and wait for current io submission to finish,
 * while current io submission must wait for bitmap io to be done. In order to
 * avoid such deadlock, submit bitmap io asynchronously.
 */
static inline void raid1_prepare_flush_writes(struct mddev *mddev)
{
	mddev->bitmap_ops->unplug(mddev, current->bio_list == NULL);
}

/*
 * Used by fix_read_error() to decay the per rdev read_errors.
 * We halve the read error count for every hour that has elapsed
 * since the last recorded read error.
 */
static inline void check_decay_read_errors(struct mddev *mddev, struct md_rdev *rdev)
{
	long cur_time_mon;
	unsigned long hours_since_last;
	unsigned int read_errors = atomic_read(&rdev->read_errors);

	cur_time_mon = ktime_get_seconds();

	if (rdev->last_read_error == 0) {
		/* first time we've seen a read error */
		rdev->last_read_error = cur_time_mon;
		return;
	}

	hours_since_last = (long)(cur_time_mon -
			    rdev->last_read_error) / 3600;

	rdev->last_read_error = cur_time_mon;

	/*
	 * if hours_since_last is > the number of bits in read_errors
	 * just set read errors to 0. We do this to avoid
	 * overflowing the shift of read_errors by hours_since_last.
	 */
	if (hours_since_last >= 8 * sizeof(read_errors))
		atomic_set(&rdev->read_errors, 0);
	else
		atomic_set(&rdev->read_errors, read_errors >> hours_since_last);
}

static inline bool exceed_read_errors(struct mddev *mddev, struct md_rdev *rdev)
{
	int max_read_errors = atomic_read(&mddev->max_corr_read_errors);
	int read_errors;

	check_decay_read_errors(mddev, rdev);
	read_errors =  atomic_inc_return(&rdev->read_errors);
	if (read_errors > max_read_errors) {
		pr_notice("md/"RAID_1_10_NAME":%s: %pg: Raid device exceeded read_error threshold [cur %d:max %d]\n",
			  mdname(mddev), rdev->bdev, read_errors, max_read_errors);
		pr_notice("md/"RAID_1_10_NAME":%s: %pg: Failing raid device\n",
			  mdname(mddev), rdev->bdev);
		md_error(mddev, rdev);
		return true;
	}

	return false;
}

/**
 * raid1_check_read_range() - check a given read range for bad blocks,
 * available read length is returned;
 * @rdev: the rdev to read;
 * @this_sector: read position;
 * @len: read length;
 *
 * helper function for read_balance()
 *
 * 1) If there are no bad blocks in the range, @len is returned;
 * 2) If the range are all bad blocks, 0 is returned;
 * 3) If there are partial bad blocks:
 *  - If the bad block range starts after @this_sector, the length of first
 *  good region is returned;
 *  - If the bad block range starts before @this_sector, 0 is returned and
 *  the @len is updated to the offset into the region before we get to the
 *  good blocks;
 */
static inline int raid1_check_read_range(struct md_rdev *rdev,
					 sector_t this_sector, int *len)
{
	sector_t first_bad;
	sector_t bad_sectors;

	/* no bad block overlap */
	if (!is_badblock(rdev, this_sector, *len, &first_bad, &bad_sectors))
		return *len;

	/*
	 * bad block range starts offset into our range so we can return the
	 * number of sectors before the bad blocks start.
	 */
	if (first_bad > this_sector)
		return first_bad - this_sector;

	/* read range is fully consumed by bad blocks. */
	if (this_sector + *len <= first_bad + bad_sectors)
		return 0;

	/*
	 * final case, bad block range starts before or at the start of our
	 * range but does not cover our entire range so we still return 0 but
	 * update the length with the number of sectors before we get to the
	 * good ones.
	 */
	*len = first_bad + bad_sectors - this_sector;
	return 0;
}

/*
 * Check if read should choose the first rdev.
 *
 * Balance on the whole device if no resync is going on (recovery is ok) or
 * below the resync window. Otherwise, take the first readable disk.
 */
static inline bool raid1_should_read_first(struct mddev *mddev,
					   sector_t this_sector, int len)
{
	if ((mddev->recovery_cp < this_sector + len))
		return true;

	if (mddev_is_clustered(mddev) &&
	    mddev->cluster_ops->area_resyncing(mddev, READ, this_sector,
					       this_sector + len))
		return true;

	return false;
}

/*
 * bio with REQ_RAHEAD or REQ_NOWAIT can fail at anytime, before such IO is
 * submitted to the underlying disks, hence don't record badblocks or retry
 * in this case.
 */
static inline bool raid1_should_handle_error(struct bio *bio)
{
	return !(bio->bi_opf & (REQ_RAHEAD | REQ_NOWAIT));
}
