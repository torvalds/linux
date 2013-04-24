/*
 * bcache setup/teardown code, and some metadata io - read a superblock and
 * figure out what to do with it.
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcache.h"
#include "btree.h"
#include "debug.h"
#include "request.h"

#include <linux/buffer_head.h>
#include <linux/debugfs.h>
#include <linux/genhd.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/reboot.h>
#include <linux/sysfs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kent Overstreet <kent.overstreet@gmail.com>");

static const char bcache_magic[] = {
	0xc6, 0x85, 0x73, 0xf6, 0x4e, 0x1a, 0x45, 0xca,
	0x82, 0x65, 0xf5, 0x7f, 0x48, 0xba, 0x6d, 0x81
};

static const char invalid_uuid[] = {
	0xa0, 0x3e, 0xf8, 0xed, 0x3e, 0xe1, 0xb8, 0x78,
	0xc8, 0x50, 0xfc, 0x5e, 0xcb, 0x16, 0xcd, 0x99
};

/* Default is -1; we skip past it for struct cached_dev's cache mode */
const char * const bch_cache_modes[] = {
	"default",
	"writethrough",
	"writeback",
	"writearound",
	"none",
	NULL
};

struct uuid_entry_v0 {
	uint8_t		uuid[16];
	uint8_t		label[32];
	uint32_t	first_reg;
	uint32_t	last_reg;
	uint32_t	invalidated;
	uint32_t	pad;
};

static struct kobject *bcache_kobj;
struct mutex bch_register_lock;
LIST_HEAD(bch_cache_sets);
static LIST_HEAD(uncached_devices);

static int bcache_major, bcache_minor;
static wait_queue_head_t unregister_wait;
struct workqueue_struct *bcache_wq;

#define BTREE_MAX_PAGES		(256 * 1024 / PAGE_SIZE)

static void bio_split_pool_free(struct bio_split_pool *p)
{
	if (p->bio_split_hook)
		mempool_destroy(p->bio_split_hook);

	if (p->bio_split)
		bioset_free(p->bio_split);
}

static int bio_split_pool_init(struct bio_split_pool *p)
{
	p->bio_split = bioset_create(4, 0);
	if (!p->bio_split)
		return -ENOMEM;

	p->bio_split_hook = mempool_create_kmalloc_pool(4,
				sizeof(struct bio_split_hook));
	if (!p->bio_split_hook)
		return -ENOMEM;

	return 0;
}

/* Superblock */

static const char *read_super(struct cache_sb *sb, struct block_device *bdev,
			      struct page **res)
{
	const char *err;
	struct cache_sb *s;
	struct buffer_head *bh = __bread(bdev, 1, SB_SIZE);
	unsigned i;

	if (!bh)
		return "IO error";

	s = (struct cache_sb *) bh->b_data;

	sb->offset		= le64_to_cpu(s->offset);
	sb->version		= le64_to_cpu(s->version);

	memcpy(sb->magic,	s->magic, 16);
	memcpy(sb->uuid,	s->uuid, 16);
	memcpy(sb->set_uuid,	s->set_uuid, 16);
	memcpy(sb->label,	s->label, SB_LABEL_SIZE);

	sb->flags		= le64_to_cpu(s->flags);
	sb->seq			= le64_to_cpu(s->seq);
	sb->last_mount		= le32_to_cpu(s->last_mount);
	sb->first_bucket	= le16_to_cpu(s->first_bucket);
	sb->keys		= le16_to_cpu(s->keys);

	for (i = 0; i < SB_JOURNAL_BUCKETS; i++)
		sb->d[i] = le64_to_cpu(s->d[i]);

	pr_debug("read sb version %llu, flags %llu, seq %llu, journal size %u",
		 sb->version, sb->flags, sb->seq, sb->keys);

	err = "Not a bcache superblock";
	if (sb->offset != SB_SECTOR)
		goto err;

	if (memcmp(sb->magic, bcache_magic, 16))
		goto err;

	err = "Too many journal buckets";
	if (sb->keys > SB_JOURNAL_BUCKETS)
		goto err;

	err = "Bad checksum";
	if (s->csum != csum_set(s))
		goto err;

	err = "Bad UUID";
	if (bch_is_zero(sb->uuid, 16))
		goto err;

	sb->block_size	= le16_to_cpu(s->block_size);

	err = "Superblock block size smaller than device block size";
	if (sb->block_size << 9 < bdev_logical_block_size(bdev))
		goto err;

	switch (sb->version) {
	case BCACHE_SB_VERSION_BDEV:
		sb->data_offset	= BDEV_DATA_START_DEFAULT;
		break;
	case BCACHE_SB_VERSION_BDEV_WITH_OFFSET:
		sb->data_offset	= le64_to_cpu(s->data_offset);

		err = "Bad data offset";
		if (sb->data_offset < BDEV_DATA_START_DEFAULT)
			goto err;

		break;
	case BCACHE_SB_VERSION_CDEV:
	case BCACHE_SB_VERSION_CDEV_WITH_UUID:
		sb->nbuckets	= le64_to_cpu(s->nbuckets);
		sb->block_size	= le16_to_cpu(s->block_size);
		sb->bucket_size	= le16_to_cpu(s->bucket_size);

		sb->nr_in_set	= le16_to_cpu(s->nr_in_set);
		sb->nr_this_dev	= le16_to_cpu(s->nr_this_dev);

		err = "Too many buckets";
		if (sb->nbuckets > LONG_MAX)
			goto err;

		err = "Not enough buckets";
		if (sb->nbuckets < 1 << 7)
			goto err;

		err = "Bad block/bucket size";
		if (!is_power_of_2(sb->block_size) ||
		    sb->block_size > PAGE_SECTORS ||
		    !is_power_of_2(sb->bucket_size) ||
		    sb->bucket_size < PAGE_SECTORS)
			goto err;

		err = "Invalid superblock: device too small";
		if (get_capacity(bdev->bd_disk) < sb->bucket_size * sb->nbuckets)
			goto err;

		err = "Bad UUID";
		if (bch_is_zero(sb->set_uuid, 16))
			goto err;

		err = "Bad cache device number in set";
		if (!sb->nr_in_set ||
		    sb->nr_in_set <= sb->nr_this_dev ||
		    sb->nr_in_set > MAX_CACHES_PER_SET)
			goto err;

		err = "Journal buckets not sequential";
		for (i = 0; i < sb->keys; i++)
			if (sb->d[i] != sb->first_bucket + i)
				goto err;

		err = "Too many journal buckets";
		if (sb->first_bucket + sb->keys > sb->nbuckets)
			goto err;

		err = "Invalid superblock: first bucket comes before end of super";
		if (sb->first_bucket * sb->bucket_size < 16)
			goto err;

		break;
	default:
		err = "Unsupported superblock version";
		goto err;
	}

	sb->last_mount = get_seconds();
	err = NULL;

	get_page(bh->b_page);
	*res = bh->b_page;
err:
	put_bh(bh);
	return err;
}

static void write_bdev_super_endio(struct bio *bio, int error)
{
	struct cached_dev *dc = bio->bi_private;
	/* XXX: error checking */

	closure_put(&dc->sb_write.cl);
}

static void __write_super(struct cache_sb *sb, struct bio *bio)
{
	struct cache_sb *out = page_address(bio->bi_io_vec[0].bv_page);
	unsigned i;

	bio->bi_sector	= SB_SECTOR;
	bio->bi_rw	= REQ_SYNC|REQ_META;
	bio->bi_size	= SB_SIZE;
	bch_bio_map(bio, NULL);

	out->offset		= cpu_to_le64(sb->offset);
	out->version		= cpu_to_le64(sb->version);

	memcpy(out->uuid,	sb->uuid, 16);
	memcpy(out->set_uuid,	sb->set_uuid, 16);
	memcpy(out->label,	sb->label, SB_LABEL_SIZE);

	out->flags		= cpu_to_le64(sb->flags);
	out->seq		= cpu_to_le64(sb->seq);

	out->last_mount		= cpu_to_le32(sb->last_mount);
	out->first_bucket	= cpu_to_le16(sb->first_bucket);
	out->keys		= cpu_to_le16(sb->keys);

	for (i = 0; i < sb->keys; i++)
		out->d[i] = cpu_to_le64(sb->d[i]);

	out->csum = csum_set(out);

	pr_debug("ver %llu, flags %llu, seq %llu",
		 sb->version, sb->flags, sb->seq);

	submit_bio(REQ_WRITE, bio);
}

void bch_write_bdev_super(struct cached_dev *dc, struct closure *parent)
{
	struct closure *cl = &dc->sb_write.cl;
	struct bio *bio = &dc->sb_bio;

	closure_lock(&dc->sb_write, parent);

	bio_reset(bio);
	bio->bi_bdev	= dc->bdev;
	bio->bi_end_io	= write_bdev_super_endio;
	bio->bi_private = dc;

	closure_get(cl);
	__write_super(&dc->sb, bio);

	closure_return(cl);
}

static void write_super_endio(struct bio *bio, int error)
{
	struct cache *ca = bio->bi_private;

	bch_count_io_errors(ca, error, "writing superblock");
	closure_put(&ca->set->sb_write.cl);
}

void bcache_write_super(struct cache_set *c)
{
	struct closure *cl = &c->sb_write.cl;
	struct cache *ca;
	unsigned i;

	closure_lock(&c->sb_write, &c->cl);

	c->sb.seq++;

	for_each_cache(ca, c, i) {
		struct bio *bio = &ca->sb_bio;

		ca->sb.version		= BCACHE_SB_VERSION_CDEV_WITH_UUID;
		ca->sb.seq		= c->sb.seq;
		ca->sb.last_mount	= c->sb.last_mount;

		SET_CACHE_SYNC(&ca->sb, CACHE_SYNC(&c->sb));

		bio_reset(bio);
		bio->bi_bdev	= ca->bdev;
		bio->bi_end_io	= write_super_endio;
		bio->bi_private = ca;

		closure_get(cl);
		__write_super(&ca->sb, bio);
	}

	closure_return(cl);
}

/* UUID io */

static void uuid_endio(struct bio *bio, int error)
{
	struct closure *cl = bio->bi_private;
	struct cache_set *c = container_of(cl, struct cache_set, uuid_write.cl);

	cache_set_err_on(error, c, "accessing uuids");
	bch_bbio_free(bio, c);
	closure_put(cl);
}

static void uuid_io(struct cache_set *c, unsigned long rw,
		    struct bkey *k, struct closure *parent)
{
	struct closure *cl = &c->uuid_write.cl;
	struct uuid_entry *u;
	unsigned i;

	BUG_ON(!parent);
	closure_lock(&c->uuid_write, parent);

	for (i = 0; i < KEY_PTRS(k); i++) {
		struct bio *bio = bch_bbio_alloc(c);

		bio->bi_rw	= REQ_SYNC|REQ_META|rw;
		bio->bi_size	= KEY_SIZE(k) << 9;

		bio->bi_end_io	= uuid_endio;
		bio->bi_private = cl;
		bch_bio_map(bio, c->uuids);

		bch_submit_bbio(bio, c, k, i);

		if (!(rw & WRITE))
			break;
	}

	pr_debug("%s UUIDs at %s", rw & REQ_WRITE ? "wrote" : "read",
		 pkey(&c->uuid_bucket));

	for (u = c->uuids; u < c->uuids + c->nr_uuids; u++)
		if (!bch_is_zero(u->uuid, 16))
			pr_debug("Slot %zi: %pU: %s: 1st: %u last: %u inv: %u",
				 u - c->uuids, u->uuid, u->label,
				 u->first_reg, u->last_reg, u->invalidated);

	closure_return(cl);
}

static char *uuid_read(struct cache_set *c, struct jset *j, struct closure *cl)
{
	struct bkey *k = &j->uuid_bucket;

	if (__bch_ptr_invalid(c, 1, k))
		return "bad uuid pointer";

	bkey_copy(&c->uuid_bucket, k);
	uuid_io(c, READ_SYNC, k, cl);

	if (j->version < BCACHE_JSET_VERSION_UUIDv1) {
		struct uuid_entry_v0	*u0 = (void *) c->uuids;
		struct uuid_entry	*u1 = (void *) c->uuids;
		int i;

		closure_sync(cl);

		/*
		 * Since the new uuid entry is bigger than the old, we have to
		 * convert starting at the highest memory address and work down
		 * in order to do it in place
		 */

		for (i = c->nr_uuids - 1;
		     i >= 0;
		     --i) {
			memcpy(u1[i].uuid,	u0[i].uuid, 16);
			memcpy(u1[i].label,	u0[i].label, 32);

			u1[i].first_reg		= u0[i].first_reg;
			u1[i].last_reg		= u0[i].last_reg;
			u1[i].invalidated	= u0[i].invalidated;

			u1[i].flags	= 0;
			u1[i].sectors	= 0;
		}
	}

	return NULL;
}

static int __uuid_write(struct cache_set *c)
{
	BKEY_PADDED(key) k;
	struct closure cl;
	closure_init_stack(&cl);

	lockdep_assert_held(&bch_register_lock);

	if (bch_bucket_alloc_set(c, WATERMARK_METADATA, &k.key, 1, &cl))
		return 1;

	SET_KEY_SIZE(&k.key, c->sb.bucket_size);
	uuid_io(c, REQ_WRITE, &k.key, &cl);
	closure_sync(&cl);

	bkey_copy(&c->uuid_bucket, &k.key);
	__bkey_put(c, &k.key);
	return 0;
}

int bch_uuid_write(struct cache_set *c)
{
	int ret = __uuid_write(c);

	if (!ret)
		bch_journal_meta(c, NULL);

	return ret;
}

static struct uuid_entry *uuid_find(struct cache_set *c, const char *uuid)
{
	struct uuid_entry *u;

	for (u = c->uuids;
	     u < c->uuids + c->nr_uuids; u++)
		if (!memcmp(u->uuid, uuid, 16))
			return u;

	return NULL;
}

static struct uuid_entry *uuid_find_empty(struct cache_set *c)
{
	static const char zero_uuid[16] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
	return uuid_find(c, zero_uuid);
}

/*
 * Bucket priorities/gens:
 *
 * For each bucket, we store on disk its
   * 8 bit gen
   * 16 bit priority
 *
 * See alloc.c for an explanation of the gen. The priority is used to implement
 * lru (and in the future other) cache replacement policies; for most purposes
 * it's just an opaque integer.
 *
 * The gens and the priorities don't have a whole lot to do with each other, and
 * it's actually the gens that must be written out at specific times - it's no
 * big deal if the priorities don't get written, if we lose them we just reuse
 * buckets in suboptimal order.
 *
 * On disk they're stored in a packed array, and in as many buckets are required
 * to fit them all. The buckets we use to store them form a list; the journal
 * header points to the first bucket, the first bucket points to the second
 * bucket, et cetera.
 *
 * This code is used by the allocation code; periodically (whenever it runs out
 * of buckets to allocate from) the allocation code will invalidate some
 * buckets, but it can't use those buckets until their new gens are safely on
 * disk.
 */

static void prio_endio(struct bio *bio, int error)
{
	struct cache *ca = bio->bi_private;

	cache_set_err_on(error, ca->set, "accessing priorities");
	bch_bbio_free(bio, ca->set);
	closure_put(&ca->prio);
}

static void prio_io(struct cache *ca, uint64_t bucket, unsigned long rw)
{
	struct closure *cl = &ca->prio;
	struct bio *bio = bch_bbio_alloc(ca->set);

	closure_init_stack(cl);

	bio->bi_sector	= bucket * ca->sb.bucket_size;
	bio->bi_bdev	= ca->bdev;
	bio->bi_rw	= REQ_SYNC|REQ_META|rw;
	bio->bi_size	= bucket_bytes(ca);

	bio->bi_end_io	= prio_endio;
	bio->bi_private = ca;
	bch_bio_map(bio, ca->disk_buckets);

	closure_bio_submit(bio, &ca->prio, ca);
	closure_sync(cl);
}

#define buckets_free(c)	"free %zu, free_inc %zu, unused %zu",		\
	fifo_used(&c->free), fifo_used(&c->free_inc), fifo_used(&c->unused)

void bch_prio_write(struct cache *ca)
{
	int i;
	struct bucket *b;
	struct closure cl;

	closure_init_stack(&cl);

	lockdep_assert_held(&ca->set->bucket_lock);

	for (b = ca->buckets;
	     b < ca->buckets + ca->sb.nbuckets; b++)
		b->disk_gen = b->gen;

	ca->disk_buckets->seq++;

	atomic_long_add(ca->sb.bucket_size * prio_buckets(ca),
			&ca->meta_sectors_written);

	pr_debug("free %zu, free_inc %zu, unused %zu", fifo_used(&ca->free),
		 fifo_used(&ca->free_inc), fifo_used(&ca->unused));
	blktrace_msg(ca, "Starting priorities: " buckets_free(ca));

	for (i = prio_buckets(ca) - 1; i >= 0; --i) {
		long bucket;
		struct prio_set *p = ca->disk_buckets;
		struct bucket_disk *d = p->data;
		struct bucket_disk *end = d + prios_per_bucket(ca);

		for (b = ca->buckets + i * prios_per_bucket(ca);
		     b < ca->buckets + ca->sb.nbuckets && d < end;
		     b++, d++) {
			d->prio = cpu_to_le16(b->prio);
			d->gen = b->gen;
		}

		p->next_bucket	= ca->prio_buckets[i + 1];
		p->magic	= pset_magic(ca);
		p->csum		= bch_crc64(&p->magic, bucket_bytes(ca) - 8);

		bucket = bch_bucket_alloc(ca, WATERMARK_PRIO, &cl);
		BUG_ON(bucket == -1);

		mutex_unlock(&ca->set->bucket_lock);
		prio_io(ca, bucket, REQ_WRITE);
		mutex_lock(&ca->set->bucket_lock);

		ca->prio_buckets[i] = bucket;
		atomic_dec_bug(&ca->buckets[bucket].pin);
	}

	mutex_unlock(&ca->set->bucket_lock);

	bch_journal_meta(ca->set, &cl);
	closure_sync(&cl);

	mutex_lock(&ca->set->bucket_lock);

	ca->need_save_prio = 0;

	/*
	 * Don't want the old priorities to get garbage collected until after we
	 * finish writing the new ones, and they're journalled
	 */
	for (i = 0; i < prio_buckets(ca); i++)
		ca->prio_last_buckets[i] = ca->prio_buckets[i];
}

static void prio_read(struct cache *ca, uint64_t bucket)
{
	struct prio_set *p = ca->disk_buckets;
	struct bucket_disk *d = p->data + prios_per_bucket(ca), *end = d;
	struct bucket *b;
	unsigned bucket_nr = 0;

	for (b = ca->buckets;
	     b < ca->buckets + ca->sb.nbuckets;
	     b++, d++) {
		if (d == end) {
			ca->prio_buckets[bucket_nr] = bucket;
			ca->prio_last_buckets[bucket_nr] = bucket;
			bucket_nr++;

			prio_io(ca, bucket, READ_SYNC);

			if (p->csum != bch_crc64(&p->magic, bucket_bytes(ca) - 8))
				pr_warn("bad csum reading priorities");

			if (p->magic != pset_magic(ca))
				pr_warn("bad magic reading priorities");

			bucket = p->next_bucket;
			d = p->data;
		}

		b->prio = le16_to_cpu(d->prio);
		b->gen = b->disk_gen = b->last_gc = b->gc_gen = d->gen;
	}
}

/* Bcache device */

static int open_dev(struct block_device *b, fmode_t mode)
{
	struct bcache_device *d = b->bd_disk->private_data;
	if (atomic_read(&d->closing))
		return -ENXIO;

	closure_get(&d->cl);
	return 0;
}

static int release_dev(struct gendisk *b, fmode_t mode)
{
	struct bcache_device *d = b->private_data;
	closure_put(&d->cl);
	return 0;
}

static int ioctl_dev(struct block_device *b, fmode_t mode,
		     unsigned int cmd, unsigned long arg)
{
	struct bcache_device *d = b->bd_disk->private_data;
	return d->ioctl(d, mode, cmd, arg);
}

static const struct block_device_operations bcache_ops = {
	.open		= open_dev,
	.release	= release_dev,
	.ioctl		= ioctl_dev,
	.owner		= THIS_MODULE,
};

void bcache_device_stop(struct bcache_device *d)
{
	if (!atomic_xchg(&d->closing, 1))
		closure_queue(&d->cl);
}

static void bcache_device_detach(struct bcache_device *d)
{
	lockdep_assert_held(&bch_register_lock);

	if (atomic_read(&d->detaching)) {
		struct uuid_entry *u = d->c->uuids + d->id;

		SET_UUID_FLASH_ONLY(u, 0);
		memcpy(u->uuid, invalid_uuid, 16);
		u->invalidated = cpu_to_le32(get_seconds());
		bch_uuid_write(d->c);

		atomic_set(&d->detaching, 0);
	}

	d->c->devices[d->id] = NULL;
	closure_put(&d->c->caching);
	d->c = NULL;
}

static void bcache_device_attach(struct bcache_device *d, struct cache_set *c,
				 unsigned id)
{
	BUG_ON(test_bit(CACHE_SET_STOPPING, &c->flags));

	d->id = id;
	d->c = c;
	c->devices[id] = d;

	closure_get(&c->caching);
}

static void bcache_device_link(struct bcache_device *d, struct cache_set *c,
			       const char *name)
{
	snprintf(d->name, BCACHEDEVNAME_SIZE,
		 "%s%u", name, d->id);

	WARN(sysfs_create_link(&d->kobj, &c->kobj, "cache") ||
	     sysfs_create_link(&c->kobj, &d->kobj, d->name),
	     "Couldn't create device <-> cache set symlinks");
}

static void bcache_device_free(struct bcache_device *d)
{
	lockdep_assert_held(&bch_register_lock);

	pr_info("%s stopped", d->disk->disk_name);

	if (d->c)
		bcache_device_detach(d);

	if (d->disk)
		del_gendisk(d->disk);
	if (d->disk && d->disk->queue)
		blk_cleanup_queue(d->disk->queue);
	if (d->disk)
		put_disk(d->disk);

	bio_split_pool_free(&d->bio_split_hook);
	if (d->unaligned_bvec)
		mempool_destroy(d->unaligned_bvec);
	if (d->bio_split)
		bioset_free(d->bio_split);

	closure_debug_destroy(&d->cl);
}

static int bcache_device_init(struct bcache_device *d, unsigned block_size)
{
	struct request_queue *q;

	if (!(d->bio_split = bioset_create(4, offsetof(struct bbio, bio))) ||
	    !(d->unaligned_bvec = mempool_create_kmalloc_pool(1,
				sizeof(struct bio_vec) * BIO_MAX_PAGES)) ||
	    bio_split_pool_init(&d->bio_split_hook))

		return -ENOMEM;

	d->disk = alloc_disk(1);
	if (!d->disk)
		return -ENOMEM;

	snprintf(d->disk->disk_name, DISK_NAME_LEN, "bcache%i", bcache_minor);

	d->disk->major		= bcache_major;
	d->disk->first_minor	= bcache_minor++;
	d->disk->fops		= &bcache_ops;
	d->disk->private_data	= d;

	q = blk_alloc_queue(GFP_KERNEL);
	if (!q)
		return -ENOMEM;

	blk_queue_make_request(q, NULL);
	d->disk->queue			= q;
	q->queuedata			= d;
	q->backing_dev_info.congested_data = d;
	q->limits.max_hw_sectors	= UINT_MAX;
	q->limits.max_sectors		= UINT_MAX;
	q->limits.max_segment_size	= UINT_MAX;
	q->limits.max_segments		= BIO_MAX_PAGES;
	q->limits.max_discard_sectors	= UINT_MAX;
	q->limits.io_min		= block_size;
	q->limits.logical_block_size	= block_size;
	q->limits.physical_block_size	= block_size;
	set_bit(QUEUE_FLAG_NONROT,	&d->disk->queue->queue_flags);
	set_bit(QUEUE_FLAG_DISCARD,	&d->disk->queue->queue_flags);

	return 0;
}

/* Cached device */

static void calc_cached_dev_sectors(struct cache_set *c)
{
	uint64_t sectors = 0;
	struct cached_dev *dc;

	list_for_each_entry(dc, &c->cached_devs, list)
		sectors += bdev_sectors(dc->bdev);

	c->cached_dev_sectors = sectors;
}

void bch_cached_dev_run(struct cached_dev *dc)
{
	struct bcache_device *d = &dc->disk;

	if (atomic_xchg(&dc->running, 1))
		return;

	if (!d->c &&
	    BDEV_STATE(&dc->sb) != BDEV_STATE_NONE) {
		struct closure cl;
		closure_init_stack(&cl);

		SET_BDEV_STATE(&dc->sb, BDEV_STATE_STALE);
		bch_write_bdev_super(dc, &cl);
		closure_sync(&cl);
	}

	add_disk(d->disk);
#if 0
	char *env[] = { "SYMLINK=label" , NULL };
	kobject_uevent_env(&disk_to_dev(d->disk)->kobj, KOBJ_CHANGE, env);
#endif
	if (sysfs_create_link(&d->kobj, &disk_to_dev(d->disk)->kobj, "dev") ||
	    sysfs_create_link(&disk_to_dev(d->disk)->kobj, &d->kobj, "bcache"))
		pr_debug("error creating sysfs link");
}

static void cached_dev_detach_finish(struct work_struct *w)
{
	struct cached_dev *dc = container_of(w, struct cached_dev, detach);
	char buf[BDEVNAME_SIZE];
	struct closure cl;
	closure_init_stack(&cl);

	BUG_ON(!atomic_read(&dc->disk.detaching));
	BUG_ON(atomic_read(&dc->count));

	sysfs_remove_link(&dc->disk.c->kobj, dc->disk.name);
	sysfs_remove_link(&dc->disk.kobj, "cache");

	mutex_lock(&bch_register_lock);

	memset(&dc->sb.set_uuid, 0, 16);
	SET_BDEV_STATE(&dc->sb, BDEV_STATE_NONE);

	bch_write_bdev_super(dc, &cl);
	closure_sync(&cl);

	bcache_device_detach(&dc->disk);
	list_move(&dc->list, &uncached_devices);

	mutex_unlock(&bch_register_lock);

	pr_info("Caching disabled for %s", bdevname(dc->bdev, buf));

	/* Drop ref we took in cached_dev_detach() */
	closure_put(&dc->disk.cl);
}

void bch_cached_dev_detach(struct cached_dev *dc)
{
	lockdep_assert_held(&bch_register_lock);

	if (atomic_read(&dc->disk.closing))
		return;

	if (atomic_xchg(&dc->disk.detaching, 1))
		return;

	/*
	 * Block the device from being closed and freed until we're finished
	 * detaching
	 */
	closure_get(&dc->disk.cl);

	bch_writeback_queue(dc);
	cached_dev_put(dc);
}

int bch_cached_dev_attach(struct cached_dev *dc, struct cache_set *c)
{
	uint32_t rtime = cpu_to_le32(get_seconds());
	struct uuid_entry *u;
	char buf[BDEVNAME_SIZE];

	bdevname(dc->bdev, buf);

	if (memcmp(dc->sb.set_uuid, c->sb.set_uuid, 16))
		return -ENOENT;

	if (dc->disk.c) {
		pr_err("Can't attach %s: already attached", buf);
		return -EINVAL;
	}

	if (test_bit(CACHE_SET_STOPPING, &c->flags)) {
		pr_err("Can't attach %s: shutting down", buf);
		return -EINVAL;
	}

	if (dc->sb.block_size < c->sb.block_size) {
		/* Will die */
		pr_err("Couldn't attach %s: block size less than set's block size",
		       buf);
		return -EINVAL;
	}

	u = uuid_find(c, dc->sb.uuid);

	if (u &&
	    (BDEV_STATE(&dc->sb) == BDEV_STATE_STALE ||
	     BDEV_STATE(&dc->sb) == BDEV_STATE_NONE)) {
		memcpy(u->uuid, invalid_uuid, 16);
		u->invalidated = cpu_to_le32(get_seconds());
		u = NULL;
	}

	if (!u) {
		if (BDEV_STATE(&dc->sb) == BDEV_STATE_DIRTY) {
			pr_err("Couldn't find uuid for %s in set", buf);
			return -ENOENT;
		}

		u = uuid_find_empty(c);
		if (!u) {
			pr_err("Not caching %s, no room for UUID", buf);
			return -EINVAL;
		}
	}

	/* Deadlocks since we're called via sysfs...
	sysfs_remove_file(&dc->kobj, &sysfs_attach);
	 */

	if (bch_is_zero(u->uuid, 16)) {
		struct closure cl;
		closure_init_stack(&cl);

		memcpy(u->uuid, dc->sb.uuid, 16);
		memcpy(u->label, dc->sb.label, SB_LABEL_SIZE);
		u->first_reg = u->last_reg = rtime;
		bch_uuid_write(c);

		memcpy(dc->sb.set_uuid, c->sb.set_uuid, 16);
		SET_BDEV_STATE(&dc->sb, BDEV_STATE_CLEAN);

		bch_write_bdev_super(dc, &cl);
		closure_sync(&cl);
	} else {
		u->last_reg = rtime;
		bch_uuid_write(c);
	}

	bcache_device_attach(&dc->disk, c, u - c->uuids);
	bcache_device_link(&dc->disk, c, "bdev");
	list_move(&dc->list, &c->cached_devs);
	calc_cached_dev_sectors(c);

	smp_wmb();
	/*
	 * dc->c must be set before dc->count != 0 - paired with the mb in
	 * cached_dev_get()
	 */
	atomic_set(&dc->count, 1);

	if (BDEV_STATE(&dc->sb) == BDEV_STATE_DIRTY) {
		atomic_set(&dc->has_dirty, 1);
		atomic_inc(&dc->count);
		bch_writeback_queue(dc);
	}

	bch_cached_dev_run(dc);

	pr_info("Caching %s as %s on set %pU",
		bdevname(dc->bdev, buf), dc->disk.disk->disk_name,
		dc->disk.c->sb.set_uuid);
	return 0;
}

void bch_cached_dev_release(struct kobject *kobj)
{
	struct cached_dev *dc = container_of(kobj, struct cached_dev,
					     disk.kobj);
	kfree(dc);
	module_put(THIS_MODULE);
}

static void cached_dev_free(struct closure *cl)
{
	struct cached_dev *dc = container_of(cl, struct cached_dev, disk.cl);

	cancel_delayed_work_sync(&dc->writeback_rate_update);

	mutex_lock(&bch_register_lock);

	bcache_device_free(&dc->disk);
	list_del(&dc->list);

	mutex_unlock(&bch_register_lock);

	if (!IS_ERR_OR_NULL(dc->bdev)) {
		blk_sync_queue(bdev_get_queue(dc->bdev));
		blkdev_put(dc->bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
	}

	wake_up(&unregister_wait);

	kobject_put(&dc->disk.kobj);
}

static void cached_dev_flush(struct closure *cl)
{
	struct cached_dev *dc = container_of(cl, struct cached_dev, disk.cl);
	struct bcache_device *d = &dc->disk;

	bch_cache_accounting_destroy(&dc->accounting);
	kobject_del(&d->kobj);

	continue_at(cl, cached_dev_free, system_wq);
}

static int cached_dev_init(struct cached_dev *dc, unsigned block_size)
{
	int err;
	struct io *io;

	closure_init(&dc->disk.cl, NULL);
	set_closure_fn(&dc->disk.cl, cached_dev_flush, system_wq);

	__module_get(THIS_MODULE);
	INIT_LIST_HEAD(&dc->list);
	kobject_init(&dc->disk.kobj, &bch_cached_dev_ktype);

	bch_cache_accounting_init(&dc->accounting, &dc->disk.cl);

	err = bcache_device_init(&dc->disk, block_size);
	if (err)
		goto err;

	spin_lock_init(&dc->io_lock);
	closure_init_unlocked(&dc->sb_write);
	INIT_WORK(&dc->detach, cached_dev_detach_finish);

	dc->sequential_merge		= true;
	dc->sequential_cutoff		= 4 << 20;

	INIT_LIST_HEAD(&dc->io_lru);
	dc->sb_bio.bi_max_vecs	= 1;
	dc->sb_bio.bi_io_vec	= dc->sb_bio.bi_inline_vecs;

	for (io = dc->io; io < dc->io + RECENT_IO; io++) {
		list_add(&io->lru, &dc->io_lru);
		hlist_add_head(&io->hash, dc->io_hash + RECENT_IO);
	}

	bch_writeback_init_cached_dev(dc);
	return 0;
err:
	bcache_device_stop(&dc->disk);
	return err;
}

/* Cached device - bcache superblock */

static const char *register_bdev(struct cache_sb *sb, struct page *sb_page,
				 struct block_device *bdev,
				 struct cached_dev *dc)
{
	char name[BDEVNAME_SIZE];
	const char *err = "cannot allocate memory";
	struct gendisk *g;
	struct cache_set *c;

	if (!dc || cached_dev_init(dc, sb->block_size << 9) != 0)
		return err;

	memcpy(&dc->sb, sb, sizeof(struct cache_sb));
	dc->sb_bio.bi_io_vec[0].bv_page = sb_page;
	dc->bdev = bdev;
	dc->bdev->bd_holder = dc;

	g = dc->disk.disk;

	set_capacity(g, dc->bdev->bd_part->nr_sects - dc->sb.data_offset);

	g->queue->backing_dev_info.ra_pages =
		max(g->queue->backing_dev_info.ra_pages,
		    bdev->bd_queue->backing_dev_info.ra_pages);

	bch_cached_dev_request_init(dc);

	err = "error creating kobject";
	if (kobject_add(&dc->disk.kobj, &part_to_dev(bdev->bd_part)->kobj,
			"bcache"))
		goto err;
	if (bch_cache_accounting_add_kobjs(&dc->accounting, &dc->disk.kobj))
		goto err;

	list_add(&dc->list, &uncached_devices);
	list_for_each_entry(c, &bch_cache_sets, list)
		bch_cached_dev_attach(dc, c);

	if (BDEV_STATE(&dc->sb) == BDEV_STATE_NONE ||
	    BDEV_STATE(&dc->sb) == BDEV_STATE_STALE)
		bch_cached_dev_run(dc);

	return NULL;
err:
	kobject_put(&dc->disk.kobj);
	pr_notice("error opening %s: %s", bdevname(bdev, name), err);
	/*
	 * Return NULL instead of an error because kobject_put() cleans
	 * everything up
	 */
	return NULL;
}

/* Flash only volumes */

void bch_flash_dev_release(struct kobject *kobj)
{
	struct bcache_device *d = container_of(kobj, struct bcache_device,
					       kobj);
	kfree(d);
}

static void flash_dev_free(struct closure *cl)
{
	struct bcache_device *d = container_of(cl, struct bcache_device, cl);
	bcache_device_free(d);
	kobject_put(&d->kobj);
}

static void flash_dev_flush(struct closure *cl)
{
	struct bcache_device *d = container_of(cl, struct bcache_device, cl);

	sysfs_remove_link(&d->c->kobj, d->name);
	sysfs_remove_link(&d->kobj, "cache");
	kobject_del(&d->kobj);
	continue_at(cl, flash_dev_free, system_wq);
}

static int flash_dev_run(struct cache_set *c, struct uuid_entry *u)
{
	struct bcache_device *d = kzalloc(sizeof(struct bcache_device),
					  GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	closure_init(&d->cl, NULL);
	set_closure_fn(&d->cl, flash_dev_flush, system_wq);

	kobject_init(&d->kobj, &bch_flash_dev_ktype);

	if (bcache_device_init(d, block_bytes(c)))
		goto err;

	bcache_device_attach(d, c, u - c->uuids);
	set_capacity(d->disk, u->sectors);
	bch_flash_dev_request_init(d);
	add_disk(d->disk);

	if (kobject_add(&d->kobj, &disk_to_dev(d->disk)->kobj, "bcache"))
		goto err;

	bcache_device_link(d, c, "volume");

	return 0;
err:
	kobject_put(&d->kobj);
	return -ENOMEM;
}

static int flash_devs_run(struct cache_set *c)
{
	int ret = 0;
	struct uuid_entry *u;

	for (u = c->uuids;
	     u < c->uuids + c->nr_uuids && !ret;
	     u++)
		if (UUID_FLASH_ONLY(u))
			ret = flash_dev_run(c, u);

	return ret;
}

int bch_flash_dev_create(struct cache_set *c, uint64_t size)
{
	struct uuid_entry *u;

	if (test_bit(CACHE_SET_STOPPING, &c->flags))
		return -EINTR;

	u = uuid_find_empty(c);
	if (!u) {
		pr_err("Can't create volume, no room for UUID");
		return -EINVAL;
	}

	get_random_bytes(u->uuid, 16);
	memset(u->label, 0, 32);
	u->first_reg = u->last_reg = cpu_to_le32(get_seconds());

	SET_UUID_FLASH_ONLY(u, 1);
	u->sectors = size >> 9;

	bch_uuid_write(c);

	return flash_dev_run(c, u);
}

/* Cache set */

__printf(2, 3)
bool bch_cache_set_error(struct cache_set *c, const char *fmt, ...)
{
	va_list args;

	if (test_bit(CACHE_SET_STOPPING, &c->flags))
		return false;

	/* XXX: we can be called from atomic context
	acquire_console_sem();
	*/

	printk(KERN_ERR "bcache: error on %pU: ", c->sb.set_uuid);

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	printk(", disabling caching\n");

	bch_cache_set_unregister(c);
	return true;
}

void bch_cache_set_release(struct kobject *kobj)
{
	struct cache_set *c = container_of(kobj, struct cache_set, kobj);
	kfree(c);
	module_put(THIS_MODULE);
}

static void cache_set_free(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, cl);
	struct cache *ca;
	unsigned i;

	if (!IS_ERR_OR_NULL(c->debug))
		debugfs_remove(c->debug);

	bch_open_buckets_free(c);
	bch_btree_cache_free(c);
	bch_journal_free(c);

	for_each_cache(ca, c, i)
		if (ca)
			kobject_put(&ca->kobj);

	free_pages((unsigned long) c->uuids, ilog2(bucket_pages(c)));
	free_pages((unsigned long) c->sort, ilog2(bucket_pages(c)));

	kfree(c->fill_iter);
	if (c->bio_split)
		bioset_free(c->bio_split);
	if (c->bio_meta)
		mempool_destroy(c->bio_meta);
	if (c->search)
		mempool_destroy(c->search);
	kfree(c->devices);

	mutex_lock(&bch_register_lock);
	list_del(&c->list);
	mutex_unlock(&bch_register_lock);

	pr_info("Cache set %pU unregistered", c->sb.set_uuid);
	wake_up(&unregister_wait);

	closure_debug_destroy(&c->cl);
	kobject_put(&c->kobj);
}

static void cache_set_flush(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, caching);
	struct btree *b;

	/* Shut down allocator threads */
	set_bit(CACHE_SET_STOPPING_2, &c->flags);
	wake_up(&c->alloc_wait);

	bch_cache_accounting_destroy(&c->accounting);

	kobject_put(&c->internal);
	kobject_del(&c->kobj);

	if (!IS_ERR_OR_NULL(c->root))
		list_add(&c->root->list, &c->btree_cache);

	/* Should skip this if we're unregistering because of an error */
	list_for_each_entry(b, &c->btree_cache, list)
		if (btree_node_dirty(b))
			bch_btree_write(b, true, NULL);

	closure_return(cl);
}

static void __cache_set_unregister(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, caching);
	struct cached_dev *dc, *t;
	size_t i;

	mutex_lock(&bch_register_lock);

	if (test_bit(CACHE_SET_UNREGISTERING, &c->flags))
		list_for_each_entry_safe(dc, t, &c->cached_devs, list)
			bch_cached_dev_detach(dc);

	for (i = 0; i < c->nr_uuids; i++)
		if (c->devices[i] && UUID_FLASH_ONLY(&c->uuids[i]))
			bcache_device_stop(c->devices[i]);

	mutex_unlock(&bch_register_lock);

	continue_at(cl, cache_set_flush, system_wq);
}

void bch_cache_set_stop(struct cache_set *c)
{
	if (!test_and_set_bit(CACHE_SET_STOPPING, &c->flags))
		closure_queue(&c->caching);
}

void bch_cache_set_unregister(struct cache_set *c)
{
	set_bit(CACHE_SET_UNREGISTERING, &c->flags);
	bch_cache_set_stop(c);
}

#define alloc_bucket_pages(gfp, c)			\
	((void *) __get_free_pages(__GFP_ZERO|gfp, ilog2(bucket_pages(c))))

struct cache_set *bch_cache_set_alloc(struct cache_sb *sb)
{
	int iter_size;
	struct cache_set *c = kzalloc(sizeof(struct cache_set), GFP_KERNEL);
	if (!c)
		return NULL;

	__module_get(THIS_MODULE);
	closure_init(&c->cl, NULL);
	set_closure_fn(&c->cl, cache_set_free, system_wq);

	closure_init(&c->caching, &c->cl);
	set_closure_fn(&c->caching, __cache_set_unregister, system_wq);

	/* Maybe create continue_at_noreturn() and use it here? */
	closure_set_stopped(&c->cl);
	closure_put(&c->cl);

	kobject_init(&c->kobj, &bch_cache_set_ktype);
	kobject_init(&c->internal, &bch_cache_set_internal_ktype);

	bch_cache_accounting_init(&c->accounting, &c->cl);

	memcpy(c->sb.set_uuid, sb->set_uuid, 16);
	c->sb.block_size	= sb->block_size;
	c->sb.bucket_size	= sb->bucket_size;
	c->sb.nr_in_set		= sb->nr_in_set;
	c->sb.last_mount	= sb->last_mount;
	c->bucket_bits		= ilog2(sb->bucket_size);
	c->block_bits		= ilog2(sb->block_size);
	c->nr_uuids		= bucket_bytes(c) / sizeof(struct uuid_entry);

	c->btree_pages		= c->sb.bucket_size / PAGE_SECTORS;
	if (c->btree_pages > BTREE_MAX_PAGES)
		c->btree_pages = max_t(int, c->btree_pages / 4,
				       BTREE_MAX_PAGES);

	init_waitqueue_head(&c->alloc_wait);
	mutex_init(&c->bucket_lock);
	mutex_init(&c->fill_lock);
	mutex_init(&c->sort_lock);
	spin_lock_init(&c->sort_time_lock);
	closure_init_unlocked(&c->sb_write);
	closure_init_unlocked(&c->uuid_write);
	spin_lock_init(&c->btree_read_time_lock);
	bch_moving_init_cache_set(c);

	INIT_LIST_HEAD(&c->list);
	INIT_LIST_HEAD(&c->cached_devs);
	INIT_LIST_HEAD(&c->btree_cache);
	INIT_LIST_HEAD(&c->btree_cache_freeable);
	INIT_LIST_HEAD(&c->btree_cache_freed);
	INIT_LIST_HEAD(&c->data_buckets);

	c->search = mempool_create_slab_pool(32, bch_search_cache);
	if (!c->search)
		goto err;

	iter_size = (sb->bucket_size / sb->block_size + 1) *
		sizeof(struct btree_iter_set);

	if (!(c->devices = kzalloc(c->nr_uuids * sizeof(void *), GFP_KERNEL)) ||
	    !(c->bio_meta = mempool_create_kmalloc_pool(2,
				sizeof(struct bbio) + sizeof(struct bio_vec) *
				bucket_pages(c))) ||
	    !(c->bio_split = bioset_create(4, offsetof(struct bbio, bio))) ||
	    !(c->fill_iter = kmalloc(iter_size, GFP_KERNEL)) ||
	    !(c->sort = alloc_bucket_pages(GFP_KERNEL, c)) ||
	    !(c->uuids = alloc_bucket_pages(GFP_KERNEL, c)) ||
	    bch_journal_alloc(c) ||
	    bch_btree_cache_alloc(c) ||
	    bch_open_buckets_alloc(c))
		goto err;

	c->fill_iter->size = sb->bucket_size / sb->block_size;

	c->congested_read_threshold_us	= 2000;
	c->congested_write_threshold_us	= 20000;
	c->error_limit	= 8 << IO_ERROR_SHIFT;

	return c;
err:
	bch_cache_set_unregister(c);
	return NULL;
}

static void run_cache_set(struct cache_set *c)
{
	const char *err = "cannot allocate memory";
	struct cached_dev *dc, *t;
	struct cache *ca;
	unsigned i;

	struct btree_op op;
	bch_btree_op_init_stack(&op);
	op.lock = SHRT_MAX;

	for_each_cache(ca, c, i)
		c->nbuckets += ca->sb.nbuckets;

	if (CACHE_SYNC(&c->sb)) {
		LIST_HEAD(journal);
		struct bkey *k;
		struct jset *j;

		err = "cannot allocate memory for journal";
		if (bch_journal_read(c, &journal, &op))
			goto err;

		pr_debug("btree_journal_read() done");

		err = "no journal entries found";
		if (list_empty(&journal))
			goto err;

		j = &list_entry(journal.prev, struct journal_replay, list)->j;

		err = "IO error reading priorities";
		for_each_cache(ca, c, i)
			prio_read(ca, j->prio_bucket[ca->sb.nr_this_dev]);

		/*
		 * If prio_read() fails it'll call cache_set_error and we'll
		 * tear everything down right away, but if we perhaps checked
		 * sooner we could avoid journal replay.
		 */

		k = &j->btree_root;

		err = "bad btree root";
		if (__bch_ptr_invalid(c, j->btree_level + 1, k))
			goto err;

		err = "error reading btree root";
		c->root = bch_btree_node_get(c, k, j->btree_level, &op);
		if (IS_ERR_OR_NULL(c->root))
			goto err;

		list_del_init(&c->root->list);
		rw_unlock(true, c->root);

		err = uuid_read(c, j, &op.cl);
		if (err)
			goto err;

		err = "error in recovery";
		if (bch_btree_check(c, &op))
			goto err;

		bch_journal_mark(c, &journal);
		bch_btree_gc_finish(c);
		pr_debug("btree_check() done");

		/*
		 * bcache_journal_next() can't happen sooner, or
		 * btree_gc_finish() will give spurious errors about last_gc >
		 * gc_gen - this is a hack but oh well.
		 */
		bch_journal_next(&c->journal);

		for_each_cache(ca, c, i)
			closure_call(&ca->alloc, bch_allocator_thread,
				     system_wq, &c->cl);

		/*
		 * First place it's safe to allocate: btree_check() and
		 * btree_gc_finish() have to run before we have buckets to
		 * allocate, and bch_bucket_alloc_set() might cause a journal
		 * entry to be written so bcache_journal_next() has to be called
		 * first.
		 *
		 * If the uuids were in the old format we have to rewrite them
		 * before the next journal entry is written:
		 */
		if (j->version < BCACHE_JSET_VERSION_UUID)
			__uuid_write(c);

		bch_journal_replay(c, &journal, &op);
	} else {
		pr_notice("invalidating existing data");
		/* Don't want invalidate_buckets() to queue a gc yet */
		closure_lock(&c->gc, NULL);

		for_each_cache(ca, c, i) {
			unsigned j;

			ca->sb.keys = clamp_t(int, ca->sb.nbuckets >> 7,
					      2, SB_JOURNAL_BUCKETS);

			for (j = 0; j < ca->sb.keys; j++)
				ca->sb.d[j] = ca->sb.first_bucket + j;
		}

		bch_btree_gc_finish(c);

		for_each_cache(ca, c, i)
			closure_call(&ca->alloc, bch_allocator_thread,
				     ca->alloc_workqueue, &c->cl);

		mutex_lock(&c->bucket_lock);
		for_each_cache(ca, c, i)
			bch_prio_write(ca);
		mutex_unlock(&c->bucket_lock);

		wake_up(&c->alloc_wait);

		err = "cannot allocate new UUID bucket";
		if (__uuid_write(c))
			goto err_unlock_gc;

		err = "cannot allocate new btree root";
		c->root = bch_btree_node_alloc(c, 0, &op.cl);
		if (IS_ERR_OR_NULL(c->root))
			goto err_unlock_gc;

		bkey_copy_key(&c->root->key, &MAX_KEY);
		bch_btree_write(c->root, true, &op);

		bch_btree_set_root(c->root);
		rw_unlock(true, c->root);

		/*
		 * We don't want to write the first journal entry until
		 * everything is set up - fortunately journal entries won't be
		 * written until the SET_CACHE_SYNC() here:
		 */
		SET_CACHE_SYNC(&c->sb, true);

		bch_journal_next(&c->journal);
		bch_journal_meta(c, &op.cl);

		/* Unlock */
		closure_set_stopped(&c->gc.cl);
		closure_put(&c->gc.cl);
	}

	closure_sync(&op.cl);
	c->sb.last_mount = get_seconds();
	bcache_write_super(c);

	list_for_each_entry_safe(dc, t, &uncached_devices, list)
		bch_cached_dev_attach(dc, c);

	flash_devs_run(c);

	return;
err_unlock_gc:
	closure_set_stopped(&c->gc.cl);
	closure_put(&c->gc.cl);
err:
	closure_sync(&op.cl);
	/* XXX: test this, it's broken */
	bch_cache_set_error(c, err);
}

static bool can_attach_cache(struct cache *ca, struct cache_set *c)
{
	return ca->sb.block_size	== c->sb.block_size &&
		ca->sb.bucket_size	== c->sb.block_size &&
		ca->sb.nr_in_set	== c->sb.nr_in_set;
}

static const char *register_cache_set(struct cache *ca)
{
	char buf[12];
	const char *err = "cannot allocate memory";
	struct cache_set *c;

	list_for_each_entry(c, &bch_cache_sets, list)
		if (!memcmp(c->sb.set_uuid, ca->sb.set_uuid, 16)) {
			if (c->cache[ca->sb.nr_this_dev])
				return "duplicate cache set member";

			if (!can_attach_cache(ca, c))
				return "cache sb does not match set";

			if (!CACHE_SYNC(&ca->sb))
				SET_CACHE_SYNC(&c->sb, false);

			goto found;
		}

	c = bch_cache_set_alloc(&ca->sb);
	if (!c)
		return err;

	err = "error creating kobject";
	if (kobject_add(&c->kobj, bcache_kobj, "%pU", c->sb.set_uuid) ||
	    kobject_add(&c->internal, &c->kobj, "internal"))
		goto err;

	if (bch_cache_accounting_add_kobjs(&c->accounting, &c->kobj))
		goto err;

	bch_debug_init_cache_set(c);

	list_add(&c->list, &bch_cache_sets);
found:
	sprintf(buf, "cache%i", ca->sb.nr_this_dev);
	if (sysfs_create_link(&ca->kobj, &c->kobj, "set") ||
	    sysfs_create_link(&c->kobj, &ca->kobj, buf))
		goto err;

	if (ca->sb.seq > c->sb.seq) {
		c->sb.version		= ca->sb.version;
		memcpy(c->sb.set_uuid, ca->sb.set_uuid, 16);
		c->sb.flags             = ca->sb.flags;
		c->sb.seq		= ca->sb.seq;
		pr_debug("set version = %llu", c->sb.version);
	}

	ca->set = c;
	ca->set->cache[ca->sb.nr_this_dev] = ca;
	c->cache_by_alloc[c->caches_loaded++] = ca;

	if (c->caches_loaded == c->sb.nr_in_set)
		run_cache_set(c);

	return NULL;
err:
	bch_cache_set_unregister(c);
	return err;
}

/* Cache device */

void bch_cache_release(struct kobject *kobj)
{
	struct cache *ca = container_of(kobj, struct cache, kobj);

	if (ca->set)
		ca->set->cache[ca->sb.nr_this_dev] = NULL;

	bch_cache_allocator_exit(ca);

	bio_split_pool_free(&ca->bio_split_hook);

	if (ca->alloc_workqueue)
		destroy_workqueue(ca->alloc_workqueue);

	free_pages((unsigned long) ca->disk_buckets, ilog2(bucket_pages(ca)));
	kfree(ca->prio_buckets);
	vfree(ca->buckets);

	free_heap(&ca->heap);
	free_fifo(&ca->unused);
	free_fifo(&ca->free_inc);
	free_fifo(&ca->free);

	if (ca->sb_bio.bi_inline_vecs[0].bv_page)
		put_page(ca->sb_bio.bi_io_vec[0].bv_page);

	if (!IS_ERR_OR_NULL(ca->bdev)) {
		blk_sync_queue(bdev_get_queue(ca->bdev));
		blkdev_put(ca->bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
	}

	kfree(ca);
	module_put(THIS_MODULE);
}

static int cache_alloc(struct cache_sb *sb, struct cache *ca)
{
	size_t free;
	struct bucket *b;

	if (!ca)
		return -ENOMEM;

	__module_get(THIS_MODULE);
	kobject_init(&ca->kobj, &bch_cache_ktype);

	memcpy(&ca->sb, sb, sizeof(struct cache_sb));

	INIT_LIST_HEAD(&ca->discards);

	bio_init(&ca->sb_bio);
	ca->sb_bio.bi_max_vecs	= 1;
	ca->sb_bio.bi_io_vec	= ca->sb_bio.bi_inline_vecs;

	bio_init(&ca->journal.bio);
	ca->journal.bio.bi_max_vecs = 8;
	ca->journal.bio.bi_io_vec = ca->journal.bio.bi_inline_vecs;

	free = roundup_pow_of_two(ca->sb.nbuckets) >> 9;
	free = max_t(size_t, free, (prio_buckets(ca) + 8) * 2);

	if (!init_fifo(&ca->free,	free, GFP_KERNEL) ||
	    !init_fifo(&ca->free_inc,	free << 2, GFP_KERNEL) ||
	    !init_fifo(&ca->unused,	free << 2, GFP_KERNEL) ||
	    !init_heap(&ca->heap,	free << 3, GFP_KERNEL) ||
	    !(ca->buckets	= vmalloc(sizeof(struct bucket) *
					  ca->sb.nbuckets)) ||
	    !(ca->prio_buckets	= kzalloc(sizeof(uint64_t) * prio_buckets(ca) *
					  2, GFP_KERNEL)) ||
	    !(ca->disk_buckets	= alloc_bucket_pages(GFP_KERNEL, ca)) ||
	    !(ca->alloc_workqueue = alloc_workqueue("bch_allocator", 0, 1)) ||
	    bio_split_pool_init(&ca->bio_split_hook))
		goto err;

	ca->prio_last_buckets = ca->prio_buckets + prio_buckets(ca);

	memset(ca->buckets, 0, ca->sb.nbuckets * sizeof(struct bucket));
	for_each_bucket(b, ca)
		atomic_set(&b->pin, 0);

	if (bch_cache_allocator_init(ca))
		goto err;

	return 0;
err:
	kobject_put(&ca->kobj);
	return -ENOMEM;
}

static const char *register_cache(struct cache_sb *sb, struct page *sb_page,
				  struct block_device *bdev, struct cache *ca)
{
	char name[BDEVNAME_SIZE];
	const char *err = "cannot allocate memory";

	if (cache_alloc(sb, ca) != 0)
		return err;

	ca->sb_bio.bi_io_vec[0].bv_page = sb_page;
	ca->bdev = bdev;
	ca->bdev->bd_holder = ca;

	if (blk_queue_discard(bdev_get_queue(ca->bdev)))
		ca->discard = CACHE_DISCARD(&ca->sb);

	err = "error creating kobject";
	if (kobject_add(&ca->kobj, &part_to_dev(bdev->bd_part)->kobj, "bcache"))
		goto err;

	err = register_cache_set(ca);
	if (err)
		goto err;

	pr_info("registered cache device %s", bdevname(bdev, name));

	return NULL;
err:
	kobject_put(&ca->kobj);
	pr_info("error opening %s: %s", bdevname(bdev, name), err);
	/* Return NULL instead of an error because kobject_put() cleans
	 * everything up
	 */
	return NULL;
}

/* Global interfaces/init */

static ssize_t register_bcache(struct kobject *, struct kobj_attribute *,
			       const char *, size_t);

kobj_attribute_write(register,		register_bcache);
kobj_attribute_write(register_quiet,	register_bcache);

static ssize_t register_bcache(struct kobject *k, struct kobj_attribute *attr,
			       const char *buffer, size_t size)
{
	ssize_t ret = size;
	const char *err = "cannot allocate memory";
	char *path = NULL;
	struct cache_sb *sb = NULL;
	struct block_device *bdev = NULL;
	struct page *sb_page = NULL;

	if (!try_module_get(THIS_MODULE))
		return -EBUSY;

	mutex_lock(&bch_register_lock);

	if (!(path = kstrndup(buffer, size, GFP_KERNEL)) ||
	    !(sb = kmalloc(sizeof(struct cache_sb), GFP_KERNEL)))
		goto err;

	err = "failed to open device";
	bdev = blkdev_get_by_path(strim(path),
				  FMODE_READ|FMODE_WRITE|FMODE_EXCL,
				  sb);
	if (bdev == ERR_PTR(-EBUSY))
		err = "device busy";

	if (IS_ERR(bdev) ||
	    set_blocksize(bdev, 4096))
		goto err;

	err = read_super(sb, bdev, &sb_page);
	if (err)
		goto err_close;

	if (SB_IS_BDEV(sb)) {
		struct cached_dev *dc = kzalloc(sizeof(*dc), GFP_KERNEL);

		err = register_bdev(sb, sb_page, bdev, dc);
	} else {
		struct cache *ca = kzalloc(sizeof(*ca), GFP_KERNEL);

		err = register_cache(sb, sb_page, bdev, ca);
	}

	if (err) {
		/* register_(bdev|cache) will only return an error if they
		 * didn't get far enough to create the kobject - if they did,
		 * the kobject destructor will do this cleanup.
		 */
		put_page(sb_page);
err_close:
		blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
err:
		if (attr != &ksysfs_register_quiet)
			pr_info("error opening %s: %s", path, err);
		ret = -EINVAL;
	}

	kfree(sb);
	kfree(path);
	mutex_unlock(&bch_register_lock);
	module_put(THIS_MODULE);
	return ret;
}

static int bcache_reboot(struct notifier_block *n, unsigned long code, void *x)
{
	if (code == SYS_DOWN ||
	    code == SYS_HALT ||
	    code == SYS_POWER_OFF) {
		DEFINE_WAIT(wait);
		unsigned long start = jiffies;
		bool stopped = false;

		struct cache_set *c, *tc;
		struct cached_dev *dc, *tdc;

		mutex_lock(&bch_register_lock);

		if (list_empty(&bch_cache_sets) &&
		    list_empty(&uncached_devices))
			goto out;

		pr_info("Stopping all devices:");

		list_for_each_entry_safe(c, tc, &bch_cache_sets, list)
			bch_cache_set_stop(c);

		list_for_each_entry_safe(dc, tdc, &uncached_devices, list)
			bcache_device_stop(&dc->disk);

		/* What's a condition variable? */
		while (1) {
			long timeout = start + 2 * HZ - jiffies;

			stopped = list_empty(&bch_cache_sets) &&
				list_empty(&uncached_devices);

			if (timeout < 0 || stopped)
				break;

			prepare_to_wait(&unregister_wait, &wait,
					TASK_UNINTERRUPTIBLE);

			mutex_unlock(&bch_register_lock);
			schedule_timeout(timeout);
			mutex_lock(&bch_register_lock);
		}

		finish_wait(&unregister_wait, &wait);

		if (stopped)
			pr_info("All devices stopped");
		else
			pr_notice("Timeout waiting for devices to be closed");
out:
		mutex_unlock(&bch_register_lock);
	}

	return NOTIFY_DONE;
}

static struct notifier_block reboot = {
	.notifier_call	= bcache_reboot,
	.priority	= INT_MAX, /* before any real devices */
};

static void bcache_exit(void)
{
	bch_debug_exit();
	bch_writeback_exit();
	bch_request_exit();
	bch_btree_exit();
	if (bcache_kobj)
		kobject_put(bcache_kobj);
	if (bcache_wq)
		destroy_workqueue(bcache_wq);
	unregister_blkdev(bcache_major, "bcache");
	unregister_reboot_notifier(&reboot);
}

static int __init bcache_init(void)
{
	static const struct attribute *files[] = {
		&ksysfs_register.attr,
		&ksysfs_register_quiet.attr,
		NULL
	};

	mutex_init(&bch_register_lock);
	init_waitqueue_head(&unregister_wait);
	register_reboot_notifier(&reboot);
	closure_debug_init();

	bcache_major = register_blkdev(0, "bcache");
	if (bcache_major < 0)
		return bcache_major;

	if (!(bcache_wq = create_workqueue("bcache")) ||
	    !(bcache_kobj = kobject_create_and_add("bcache", fs_kobj)) ||
	    sysfs_create_files(bcache_kobj, files) ||
	    bch_btree_init() ||
	    bch_request_init() ||
	    bch_writeback_init() ||
	    bch_debug_init(bcache_kobj))
		goto err;

	return 0;
err:
	bcache_exit();
	return -ENOMEM;
}

module_exit(bcache_exit);
module_init(bcache_init);
