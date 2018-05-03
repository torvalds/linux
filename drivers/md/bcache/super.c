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
#include "extents.h"
#include "request.h"
#include "writeback.h"

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/debugfs.h>
#include <linux/genhd.h>
#include <linux/idr.h>
#include <linux/kthread.h>
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

/* Default is -1; we skip past it for stop_when_cache_set_failed */
const char * const bch_stop_on_failure_modes[] = {
	"default",
	"auto",
	"always",
	NULL
};

static struct kobject *bcache_kobj;
struct mutex bch_register_lock;
LIST_HEAD(bch_cache_sets);
static LIST_HEAD(uncached_devices);

static int bcache_major;
static DEFINE_IDA(bcache_device_idx);
static wait_queue_head_t unregister_wait;
struct workqueue_struct *bcache_wq;

#define BTREE_MAX_PAGES		(256 * 1024 / PAGE_SIZE)
/* limitation of partitions number on single bcache device */
#define BCACHE_MINORS		128
/* limitation of bcache devices number on single system */
#define BCACHE_DEVICE_IDX_MAX	((1U << MINORBITS)/BCACHE_MINORS)

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

static void write_bdev_super_endio(struct bio *bio)
{
	struct cached_dev *dc = bio->bi_private;
	/* XXX: error checking */

	closure_put(&dc->sb_write);
}

static void __write_super(struct cache_sb *sb, struct bio *bio)
{
	struct cache_sb *out = page_address(bio_first_page_all(bio));
	unsigned i;

	bio->bi_iter.bi_sector	= SB_SECTOR;
	bio->bi_iter.bi_size	= SB_SIZE;
	bio_set_op_attrs(bio, REQ_OP_WRITE, REQ_SYNC|REQ_META);
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

	submit_bio(bio);
}

static void bch_write_bdev_super_unlock(struct closure *cl)
{
	struct cached_dev *dc = container_of(cl, struct cached_dev, sb_write);

	up(&dc->sb_write_mutex);
}

void bch_write_bdev_super(struct cached_dev *dc, struct closure *parent)
{
	struct closure *cl = &dc->sb_write;
	struct bio *bio = &dc->sb_bio;

	down(&dc->sb_write_mutex);
	closure_init(cl, parent);

	bio_reset(bio);
	bio_set_dev(bio, dc->bdev);
	bio->bi_end_io	= write_bdev_super_endio;
	bio->bi_private = dc;

	closure_get(cl);
	/* I/O request sent to backing device */
	__write_super(&dc->sb, bio);

	closure_return_with_destructor(cl, bch_write_bdev_super_unlock);
}

static void write_super_endio(struct bio *bio)
{
	struct cache *ca = bio->bi_private;

	/* is_read = 0 */
	bch_count_io_errors(ca, bio->bi_status, 0,
			    "writing superblock");
	closure_put(&ca->set->sb_write);
}

static void bcache_write_super_unlock(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, sb_write);

	up(&c->sb_write_mutex);
}

void bcache_write_super(struct cache_set *c)
{
	struct closure *cl = &c->sb_write;
	struct cache *ca;
	unsigned i;

	down(&c->sb_write_mutex);
	closure_init(cl, &c->cl);

	c->sb.seq++;

	for_each_cache(ca, c, i) {
		struct bio *bio = &ca->sb_bio;

		ca->sb.version		= BCACHE_SB_VERSION_CDEV_WITH_UUID;
		ca->sb.seq		= c->sb.seq;
		ca->sb.last_mount	= c->sb.last_mount;

		SET_CACHE_SYNC(&ca->sb, CACHE_SYNC(&c->sb));

		bio_reset(bio);
		bio_set_dev(bio, ca->bdev);
		bio->bi_end_io	= write_super_endio;
		bio->bi_private = ca;

		closure_get(cl);
		__write_super(&ca->sb, bio);
	}

	closure_return_with_destructor(cl, bcache_write_super_unlock);
}

/* UUID io */

static void uuid_endio(struct bio *bio)
{
	struct closure *cl = bio->bi_private;
	struct cache_set *c = container_of(cl, struct cache_set, uuid_write);

	cache_set_err_on(bio->bi_status, c, "accessing uuids");
	bch_bbio_free(bio, c);
	closure_put(cl);
}

static void uuid_io_unlock(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, uuid_write);

	up(&c->uuid_write_mutex);
}

static void uuid_io(struct cache_set *c, int op, unsigned long op_flags,
		    struct bkey *k, struct closure *parent)
{
	struct closure *cl = &c->uuid_write;
	struct uuid_entry *u;
	unsigned i;
	char buf[80];

	BUG_ON(!parent);
	down(&c->uuid_write_mutex);
	closure_init(cl, parent);

	for (i = 0; i < KEY_PTRS(k); i++) {
		struct bio *bio = bch_bbio_alloc(c);

		bio->bi_opf = REQ_SYNC | REQ_META | op_flags;
		bio->bi_iter.bi_size = KEY_SIZE(k) << 9;

		bio->bi_end_io	= uuid_endio;
		bio->bi_private = cl;
		bio_set_op_attrs(bio, op, REQ_SYNC|REQ_META|op_flags);
		bch_bio_map(bio, c->uuids);

		bch_submit_bbio(bio, c, k, i);

		if (op != REQ_OP_WRITE)
			break;
	}

	bch_extent_to_text(buf, sizeof(buf), k);
	pr_debug("%s UUIDs at %s", op == REQ_OP_WRITE ? "wrote" : "read", buf);

	for (u = c->uuids; u < c->uuids + c->nr_uuids; u++)
		if (!bch_is_zero(u->uuid, 16))
			pr_debug("Slot %zi: %pU: %s: 1st: %u last: %u inv: %u",
				 u - c->uuids, u->uuid, u->label,
				 u->first_reg, u->last_reg, u->invalidated);

	closure_return_with_destructor(cl, uuid_io_unlock);
}

static char *uuid_read(struct cache_set *c, struct jset *j, struct closure *cl)
{
	struct bkey *k = &j->uuid_bucket;

	if (__bch_btree_ptr_invalid(c, k))
		return "bad uuid pointer";

	bkey_copy(&c->uuid_bucket, k);
	uuid_io(c, REQ_OP_READ, 0, k, cl);

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

	if (bch_bucket_alloc_set(c, RESERVE_BTREE, &k.key, 1, true))
		return 1;

	SET_KEY_SIZE(&k.key, c->sb.bucket_size);
	uuid_io(c, REQ_OP_WRITE, 0, &k.key, &cl);
	closure_sync(&cl);

	bkey_copy(&c->uuid_bucket, &k.key);
	bkey_put(c, &k.key);
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

static void prio_endio(struct bio *bio)
{
	struct cache *ca = bio->bi_private;

	cache_set_err_on(bio->bi_status, ca->set, "accessing priorities");
	bch_bbio_free(bio, ca->set);
	closure_put(&ca->prio);
}

static void prio_io(struct cache *ca, uint64_t bucket, int op,
		    unsigned long op_flags)
{
	struct closure *cl = &ca->prio;
	struct bio *bio = bch_bbio_alloc(ca->set);

	closure_init_stack(cl);

	bio->bi_iter.bi_sector	= bucket * ca->sb.bucket_size;
	bio_set_dev(bio, ca->bdev);
	bio->bi_iter.bi_size	= bucket_bytes(ca);

	bio->bi_end_io	= prio_endio;
	bio->bi_private = ca;
	bio_set_op_attrs(bio, op, REQ_SYNC|REQ_META|op_flags);
	bch_bio_map(bio, ca->disk_buckets);

	closure_bio_submit(ca->set, bio, &ca->prio);
	closure_sync(cl);
}

void bch_prio_write(struct cache *ca)
{
	int i;
	struct bucket *b;
	struct closure cl;

	closure_init_stack(&cl);

	lockdep_assert_held(&ca->set->bucket_lock);

	ca->disk_buckets->seq++;

	atomic_long_add(ca->sb.bucket_size * prio_buckets(ca),
			&ca->meta_sectors_written);

	//pr_debug("free %zu, free_inc %zu, unused %zu", fifo_used(&ca->free),
	//	 fifo_used(&ca->free_inc), fifo_used(&ca->unused));

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
		p->magic	= pset_magic(&ca->sb);
		p->csum		= bch_crc64(&p->magic, bucket_bytes(ca) - 8);

		bucket = bch_bucket_alloc(ca, RESERVE_PRIO, true);
		BUG_ON(bucket == -1);

		mutex_unlock(&ca->set->bucket_lock);
		prio_io(ca, bucket, REQ_OP_WRITE, 0);
		mutex_lock(&ca->set->bucket_lock);

		ca->prio_buckets[i] = bucket;
		atomic_dec_bug(&ca->buckets[bucket].pin);
	}

	mutex_unlock(&ca->set->bucket_lock);

	bch_journal_meta(ca->set, &cl);
	closure_sync(&cl);

	mutex_lock(&ca->set->bucket_lock);

	/*
	 * Don't want the old priorities to get garbage collected until after we
	 * finish writing the new ones, and they're journalled
	 */
	for (i = 0; i < prio_buckets(ca); i++) {
		if (ca->prio_last_buckets[i])
			__bch_bucket_free(ca,
				&ca->buckets[ca->prio_last_buckets[i]]);

		ca->prio_last_buckets[i] = ca->prio_buckets[i];
	}
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

			prio_io(ca, bucket, REQ_OP_READ, 0);

			if (p->csum != bch_crc64(&p->magic, bucket_bytes(ca) - 8))
				pr_warn("bad csum reading priorities");

			if (p->magic != pset_magic(&ca->sb))
				pr_warn("bad magic reading priorities");

			bucket = p->next_bucket;
			d = p->data;
		}

		b->prio = le16_to_cpu(d->prio);
		b->gen = b->last_gc = d->gen;
	}
}

/* Bcache device */

static int open_dev(struct block_device *b, fmode_t mode)
{
	struct bcache_device *d = b->bd_disk->private_data;
	if (test_bit(BCACHE_DEV_CLOSING, &d->flags))
		return -ENXIO;

	closure_get(&d->cl);
	return 0;
}

static void release_dev(struct gendisk *b, fmode_t mode)
{
	struct bcache_device *d = b->private_data;
	closure_put(&d->cl);
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
	if (!test_and_set_bit(BCACHE_DEV_CLOSING, &d->flags))
		closure_queue(&d->cl);
}

static void bcache_device_unlink(struct bcache_device *d)
{
	lockdep_assert_held(&bch_register_lock);

	if (d->c && !test_and_set_bit(BCACHE_DEV_UNLINK_DONE, &d->flags)) {
		unsigned i;
		struct cache *ca;

		sysfs_remove_link(&d->c->kobj, d->name);
		sysfs_remove_link(&d->kobj, "cache");

		for_each_cache(ca, d->c, i)
			bd_unlink_disk_holder(ca->bdev, d->disk);
	}
}

static void bcache_device_link(struct bcache_device *d, struct cache_set *c,
			       const char *name)
{
	unsigned i;
	struct cache *ca;

	for_each_cache(ca, d->c, i)
		bd_link_disk_holder(ca->bdev, d->disk);

	snprintf(d->name, BCACHEDEVNAME_SIZE,
		 "%s%u", name, d->id);

	WARN(sysfs_create_link(&d->kobj, &c->kobj, "cache") ||
	     sysfs_create_link(&c->kobj, &d->kobj, d->name),
	     "Couldn't create device <-> cache set symlinks");

	clear_bit(BCACHE_DEV_UNLINK_DONE, &d->flags);
}

static void bcache_device_detach(struct bcache_device *d)
{
	lockdep_assert_held(&bch_register_lock);

	if (test_bit(BCACHE_DEV_DETACHING, &d->flags)) {
		struct uuid_entry *u = d->c->uuids + d->id;

		SET_UUID_FLASH_ONLY(u, 0);
		memcpy(u->uuid, invalid_uuid, 16);
		u->invalidated = cpu_to_le32(get_seconds());
		bch_uuid_write(d->c);
	}

	bcache_device_unlink(d);

	d->c->devices[d->id] = NULL;
	closure_put(&d->c->caching);
	d->c = NULL;
}

static void bcache_device_attach(struct bcache_device *d, struct cache_set *c,
				 unsigned id)
{
	d->id = id;
	d->c = c;
	c->devices[id] = d;

	if (id >= c->devices_max_used)
		c->devices_max_used = id + 1;

	closure_get(&c->caching);
}

static inline int first_minor_to_idx(int first_minor)
{
	return (first_minor/BCACHE_MINORS);
}

static inline int idx_to_first_minor(int idx)
{
	return (idx * BCACHE_MINORS);
}

static void bcache_device_free(struct bcache_device *d)
{
	lockdep_assert_held(&bch_register_lock);

	pr_info("%s stopped", d->disk->disk_name);

	if (d->c)
		bcache_device_detach(d);
	if (d->disk && d->disk->flags & GENHD_FL_UP)
		del_gendisk(d->disk);
	if (d->disk && d->disk->queue)
		blk_cleanup_queue(d->disk->queue);
	if (d->disk) {
		ida_simple_remove(&bcache_device_idx,
				  first_minor_to_idx(d->disk->first_minor));
		put_disk(d->disk);
	}

	if (d->bio_split)
		bioset_free(d->bio_split);
	kvfree(d->full_dirty_stripes);
	kvfree(d->stripe_sectors_dirty);

	closure_debug_destroy(&d->cl);
}

static int bcache_device_init(struct bcache_device *d, unsigned block_size,
			      sector_t sectors)
{
	struct request_queue *q;
	const size_t max_stripes = min_t(size_t, INT_MAX,
					 SIZE_MAX / sizeof(atomic_t));
	size_t n;
	int idx;

	if (!d->stripe_size)
		d->stripe_size = 1 << 31;

	d->nr_stripes = DIV_ROUND_UP_ULL(sectors, d->stripe_size);

	if (!d->nr_stripes || d->nr_stripes > max_stripes) {
		pr_err("nr_stripes too large or invalid: %u (start sector beyond end of disk?)",
			(unsigned)d->nr_stripes);
		return -ENOMEM;
	}

	n = d->nr_stripes * sizeof(atomic_t);
	d->stripe_sectors_dirty = kvzalloc(n, GFP_KERNEL);
	if (!d->stripe_sectors_dirty)
		return -ENOMEM;

	n = BITS_TO_LONGS(d->nr_stripes) * sizeof(unsigned long);
	d->full_dirty_stripes = kvzalloc(n, GFP_KERNEL);
	if (!d->full_dirty_stripes)
		return -ENOMEM;

	idx = ida_simple_get(&bcache_device_idx, 0,
				BCACHE_DEVICE_IDX_MAX, GFP_KERNEL);
	if (idx < 0)
		return idx;

	if (!(d->bio_split = bioset_create(4, offsetof(struct bbio, bio),
					   BIOSET_NEED_BVECS |
					   BIOSET_NEED_RESCUER)) ||
	    !(d->disk = alloc_disk(BCACHE_MINORS))) {
		ida_simple_remove(&bcache_device_idx, idx);
		return -ENOMEM;
	}

	set_capacity(d->disk, sectors);
	snprintf(d->disk->disk_name, DISK_NAME_LEN, "bcache%i", idx);

	d->disk->major		= bcache_major;
	d->disk->first_minor	= idx_to_first_minor(idx);
	d->disk->fops		= &bcache_ops;
	d->disk->private_data	= d;

	q = blk_alloc_queue(GFP_KERNEL);
	if (!q)
		return -ENOMEM;

	blk_queue_make_request(q, NULL);
	d->disk->queue			= q;
	q->queuedata			= d;
	q->backing_dev_info->congested_data = d;
	q->limits.max_hw_sectors	= UINT_MAX;
	q->limits.max_sectors		= UINT_MAX;
	q->limits.max_segment_size	= UINT_MAX;
	q->limits.max_segments		= BIO_MAX_PAGES;
	blk_queue_max_discard_sectors(q, UINT_MAX);
	q->limits.discard_granularity	= 512;
	q->limits.io_min		= block_size;
	q->limits.logical_block_size	= block_size;
	q->limits.physical_block_size	= block_size;
	blk_queue_flag_set(QUEUE_FLAG_NONROT, d->disk->queue);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, d->disk->queue);
	blk_queue_flag_set(QUEUE_FLAG_DISCARD, d->disk->queue);

	blk_queue_write_cache(q, true, true);

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
	char buf[SB_LABEL_SIZE + 1];
	char *env[] = {
		"DRIVER=bcache",
		kasprintf(GFP_KERNEL, "CACHED_UUID=%pU", dc->sb.uuid),
		NULL,
		NULL,
	};

	memcpy(buf, dc->sb.label, SB_LABEL_SIZE);
	buf[SB_LABEL_SIZE] = '\0';
	env[2] = kasprintf(GFP_KERNEL, "CACHED_LABEL=%s", buf);

	if (atomic_xchg(&dc->running, 1)) {
		kfree(env[1]);
		kfree(env[2]);
		return;
	}

	if (!d->c &&
	    BDEV_STATE(&dc->sb) != BDEV_STATE_NONE) {
		struct closure cl;
		closure_init_stack(&cl);

		SET_BDEV_STATE(&dc->sb, BDEV_STATE_STALE);
		bch_write_bdev_super(dc, &cl);
		closure_sync(&cl);
	}

	add_disk(d->disk);
	bd_link_disk_holder(dc->bdev, dc->disk.disk);
	/* won't show up in the uevent file, use udevadm monitor -e instead
	 * only class / kset properties are persistent */
	kobject_uevent_env(&disk_to_dev(d->disk)->kobj, KOBJ_CHANGE, env);
	kfree(env[1]);
	kfree(env[2]);

	if (sysfs_create_link(&d->kobj, &disk_to_dev(d->disk)->kobj, "dev") ||
	    sysfs_create_link(&disk_to_dev(d->disk)->kobj, &d->kobj, "bcache"))
		pr_debug("error creating sysfs link");
}

/*
 * If BCACHE_DEV_RATE_DW_RUNNING is set, it means routine of the delayed
 * work dc->writeback_rate_update is running. Wait until the routine
 * quits (BCACHE_DEV_RATE_DW_RUNNING is clear), then continue to
 * cancel it. If BCACHE_DEV_RATE_DW_RUNNING is not clear after time_out
 * seconds, give up waiting here and continue to cancel it too.
 */
static void cancel_writeback_rate_update_dwork(struct cached_dev *dc)
{
	int time_out = WRITEBACK_RATE_UPDATE_SECS_MAX * HZ;

	do {
		if (!test_bit(BCACHE_DEV_RATE_DW_RUNNING,
			      &dc->disk.flags))
			break;
		time_out--;
		schedule_timeout_interruptible(1);
	} while (time_out > 0);

	if (time_out == 0)
		pr_warn("give up waiting for dc->writeback_write_update to quit");

	cancel_delayed_work_sync(&dc->writeback_rate_update);
}

static void cached_dev_detach_finish(struct work_struct *w)
{
	struct cached_dev *dc = container_of(w, struct cached_dev, detach);
	struct closure cl;
	closure_init_stack(&cl);

	BUG_ON(!test_bit(BCACHE_DEV_DETACHING, &dc->disk.flags));
	BUG_ON(refcount_read(&dc->count));

	mutex_lock(&bch_register_lock);

	if (test_and_clear_bit(BCACHE_DEV_WB_RUNNING, &dc->disk.flags))
		cancel_writeback_rate_update_dwork(dc);

	if (!IS_ERR_OR_NULL(dc->writeback_thread)) {
		kthread_stop(dc->writeback_thread);
		dc->writeback_thread = NULL;
	}

	memset(&dc->sb.set_uuid, 0, 16);
	SET_BDEV_STATE(&dc->sb, BDEV_STATE_NONE);

	bch_write_bdev_super(dc, &cl);
	closure_sync(&cl);

	bcache_device_detach(&dc->disk);
	list_move(&dc->list, &uncached_devices);

	clear_bit(BCACHE_DEV_DETACHING, &dc->disk.flags);
	clear_bit(BCACHE_DEV_UNLINK_DONE, &dc->disk.flags);

	mutex_unlock(&bch_register_lock);

	pr_info("Caching disabled for %s", dc->backing_dev_name);

	/* Drop ref we took in cached_dev_detach() */
	closure_put(&dc->disk.cl);
}

void bch_cached_dev_detach(struct cached_dev *dc)
{
	lockdep_assert_held(&bch_register_lock);

	if (test_bit(BCACHE_DEV_CLOSING, &dc->disk.flags))
		return;

	if (test_and_set_bit(BCACHE_DEV_DETACHING, &dc->disk.flags))
		return;

	/*
	 * Block the device from being closed and freed until we're finished
	 * detaching
	 */
	closure_get(&dc->disk.cl);

	bch_writeback_queue(dc);

	cached_dev_put(dc);
}

int bch_cached_dev_attach(struct cached_dev *dc, struct cache_set *c,
			  uint8_t *set_uuid)
{
	uint32_t rtime = cpu_to_le32(get_seconds());
	struct uuid_entry *u;
	struct cached_dev *exist_dc, *t;

	if ((set_uuid && memcmp(set_uuid, c->sb.set_uuid, 16)) ||
	    (!set_uuid && memcmp(dc->sb.set_uuid, c->sb.set_uuid, 16)))
		return -ENOENT;

	if (dc->disk.c) {
		pr_err("Can't attach %s: already attached",
		       dc->backing_dev_name);
		return -EINVAL;
	}

	if (test_bit(CACHE_SET_STOPPING, &c->flags)) {
		pr_err("Can't attach %s: shutting down",
		       dc->backing_dev_name);
		return -EINVAL;
	}

	if (dc->sb.block_size < c->sb.block_size) {
		/* Will die */
		pr_err("Couldn't attach %s: block size less than set's block size",
		       dc->backing_dev_name);
		return -EINVAL;
	}

	/* Check whether already attached */
	list_for_each_entry_safe(exist_dc, t, &c->cached_devs, list) {
		if (!memcmp(dc->sb.uuid, exist_dc->sb.uuid, 16)) {
			pr_err("Tried to attach %s but duplicate UUID already attached",
				dc->backing_dev_name);

			return -EINVAL;
		}
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
			pr_err("Couldn't find uuid for %s in set",
			       dc->backing_dev_name);
			return -ENOENT;
		}

		u = uuid_find_empty(c);
		if (!u) {
			pr_err("Not caching %s, no room for UUID",
			       dc->backing_dev_name);
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
	list_move(&dc->list, &c->cached_devs);
	calc_cached_dev_sectors(c);

	smp_wmb();
	/*
	 * dc->c must be set before dc->count != 0 - paired with the mb in
	 * cached_dev_get()
	 */
	refcount_set(&dc->count, 1);

	/* Block writeback thread, but spawn it */
	down_write(&dc->writeback_lock);
	if (bch_cached_dev_writeback_start(dc)) {
		up_write(&dc->writeback_lock);
		return -ENOMEM;
	}

	if (BDEV_STATE(&dc->sb) == BDEV_STATE_DIRTY) {
		bch_sectors_dirty_init(&dc->disk);
		atomic_set(&dc->has_dirty, 1);
		bch_writeback_queue(dc);
	}

	bch_cached_dev_run(dc);
	bcache_device_link(&dc->disk, c, "bdev");

	/* Allow the writeback thread to proceed */
	up_write(&dc->writeback_lock);

	pr_info("Caching %s as %s on set %pU",
		dc->backing_dev_name,
		dc->disk.disk->disk_name,
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

	mutex_lock(&bch_register_lock);

	if (test_and_clear_bit(BCACHE_DEV_WB_RUNNING, &dc->disk.flags))
		cancel_writeback_rate_update_dwork(dc);

	if (!IS_ERR_OR_NULL(dc->writeback_thread))
		kthread_stop(dc->writeback_thread);
	if (dc->writeback_write_wq)
		destroy_workqueue(dc->writeback_write_wq);

	if (atomic_read(&dc->running))
		bd_unlink_disk_holder(dc->bdev, dc->disk.disk);
	bcache_device_free(&dc->disk);
	list_del(&dc->list);

	mutex_unlock(&bch_register_lock);

	if (!IS_ERR_OR_NULL(dc->bdev))
		blkdev_put(dc->bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);

	wake_up(&unregister_wait);

	kobject_put(&dc->disk.kobj);
}

static void cached_dev_flush(struct closure *cl)
{
	struct cached_dev *dc = container_of(cl, struct cached_dev, disk.cl);
	struct bcache_device *d = &dc->disk;

	mutex_lock(&bch_register_lock);
	bcache_device_unlink(d);
	mutex_unlock(&bch_register_lock);

	bch_cache_accounting_destroy(&dc->accounting);
	kobject_del(&d->kobj);

	continue_at(cl, cached_dev_free, system_wq);
}

static int cached_dev_init(struct cached_dev *dc, unsigned block_size)
{
	int ret;
	struct io *io;
	struct request_queue *q = bdev_get_queue(dc->bdev);

	__module_get(THIS_MODULE);
	INIT_LIST_HEAD(&dc->list);
	closure_init(&dc->disk.cl, NULL);
	set_closure_fn(&dc->disk.cl, cached_dev_flush, system_wq);
	kobject_init(&dc->disk.kobj, &bch_cached_dev_ktype);
	INIT_WORK(&dc->detach, cached_dev_detach_finish);
	sema_init(&dc->sb_write_mutex, 1);
	INIT_LIST_HEAD(&dc->io_lru);
	spin_lock_init(&dc->io_lock);
	bch_cache_accounting_init(&dc->accounting, &dc->disk.cl);

	dc->sequential_cutoff		= 4 << 20;

	for (io = dc->io; io < dc->io + RECENT_IO; io++) {
		list_add(&io->lru, &dc->io_lru);
		hlist_add_head(&io->hash, dc->io_hash + RECENT_IO);
	}

	dc->disk.stripe_size = q->limits.io_opt >> 9;

	if (dc->disk.stripe_size)
		dc->partial_stripes_expensive =
			q->limits.raid_partial_stripes_expensive;

	ret = bcache_device_init(&dc->disk, block_size,
			 dc->bdev->bd_part->nr_sects - dc->sb.data_offset);
	if (ret)
		return ret;

	dc->disk.disk->queue->backing_dev_info->ra_pages =
		max(dc->disk.disk->queue->backing_dev_info->ra_pages,
		    q->backing_dev_info->ra_pages);

	atomic_set(&dc->io_errors, 0);
	dc->io_disable = false;
	dc->error_limit = DEFAULT_CACHED_DEV_ERROR_LIMIT;
	/* default to auto */
	dc->stop_when_cache_set_failed = BCH_CACHED_DEV_STOP_AUTO;

	bch_cached_dev_request_init(dc);
	bch_cached_dev_writeback_init(dc);
	return 0;
}

/* Cached device - bcache superblock */

static void register_bdev(struct cache_sb *sb, struct page *sb_page,
				 struct block_device *bdev,
				 struct cached_dev *dc)
{
	const char *err = "cannot allocate memory";
	struct cache_set *c;

	bdevname(bdev, dc->backing_dev_name);
	memcpy(&dc->sb, sb, sizeof(struct cache_sb));
	dc->bdev = bdev;
	dc->bdev->bd_holder = dc;

	bio_init(&dc->sb_bio, dc->sb_bio.bi_inline_vecs, 1);
	bio_first_bvec_all(&dc->sb_bio)->bv_page = sb_page;
	get_page(sb_page);


	if (cached_dev_init(dc, sb->block_size << 9))
		goto err;

	err = "error creating kobject";
	if (kobject_add(&dc->disk.kobj, &part_to_dev(bdev->bd_part)->kobj,
			"bcache"))
		goto err;
	if (bch_cache_accounting_add_kobjs(&dc->accounting, &dc->disk.kobj))
		goto err;

	pr_info("registered backing device %s", dc->backing_dev_name);

	list_add(&dc->list, &uncached_devices);
	list_for_each_entry(c, &bch_cache_sets, list)
		bch_cached_dev_attach(dc, c, NULL);

	if (BDEV_STATE(&dc->sb) == BDEV_STATE_NONE ||
	    BDEV_STATE(&dc->sb) == BDEV_STATE_STALE)
		bch_cached_dev_run(dc);

	return;
err:
	pr_notice("error %s: %s", dc->backing_dev_name, err);
	bcache_device_stop(&dc->disk);
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
	mutex_lock(&bch_register_lock);
	bcache_device_free(d);
	mutex_unlock(&bch_register_lock);
	kobject_put(&d->kobj);
}

static void flash_dev_flush(struct closure *cl)
{
	struct bcache_device *d = container_of(cl, struct bcache_device, cl);

	mutex_lock(&bch_register_lock);
	bcache_device_unlink(d);
	mutex_unlock(&bch_register_lock);
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

	if (bcache_device_init(d, block_bytes(c), u->sectors))
		goto err;

	bcache_device_attach(d, c, u - c->uuids);
	bch_sectors_dirty_init(d);
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

	if (!test_bit(CACHE_SET_RUNNING, &c->flags))
		return -EPERM;

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

bool bch_cached_dev_error(struct cached_dev *dc)
{
	if (!dc || test_bit(BCACHE_DEV_CLOSING, &dc->disk.flags))
		return false;

	dc->io_disable = true;
	/* make others know io_disable is true earlier */
	smp_mb();

	pr_err("stop %s: too many IO errors on backing device %s\n",
		dc->disk.disk->disk_name, dc->backing_dev_name);

	bcache_device_stop(&dc->disk);
	return true;
}

/* Cache set */

__printf(2, 3)
bool bch_cache_set_error(struct cache_set *c, const char *fmt, ...)
{
	va_list args;

	if (c->on_error != ON_ERROR_PANIC &&
	    test_bit(CACHE_SET_STOPPING, &c->flags))
		return false;

	if (test_and_set_bit(CACHE_SET_IO_DISABLE, &c->flags))
		pr_warn("CACHE_SET_IO_DISABLE already set");

	/* XXX: we can be called from atomic context
	acquire_console_sem();
	*/

	printk(KERN_ERR "bcache: error on %pU: ", c->sb.set_uuid);

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	printk(", disabling caching\n");

	if (c->on_error == ON_ERROR_PANIC)
		panic("panic forced after error\n");

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
		if (ca) {
			ca->set = NULL;
			c->cache[ca->sb.nr_this_dev] = NULL;
			kobject_put(&ca->kobj);
		}

	bch_bset_sort_state_free(&c->sort);
	free_pages((unsigned long) c->uuids, ilog2(bucket_pages(c)));

	if (c->moving_gc_wq)
		destroy_workqueue(c->moving_gc_wq);
	if (c->bio_split)
		bioset_free(c->bio_split);
	if (c->fill_iter)
		mempool_destroy(c->fill_iter);
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
	struct cache *ca;
	struct btree *b;
	unsigned i;

	bch_cache_accounting_destroy(&c->accounting);

	kobject_put(&c->internal);
	kobject_del(&c->kobj);

	if (c->gc_thread)
		kthread_stop(c->gc_thread);

	if (!IS_ERR_OR_NULL(c->root))
		list_add(&c->root->list, &c->btree_cache);

	/* Should skip this if we're unregistering because of an error */
	list_for_each_entry(b, &c->btree_cache, list) {
		mutex_lock(&b->write_lock);
		if (btree_node_dirty(b))
			__bch_btree_node_write(b, NULL);
		mutex_unlock(&b->write_lock);
	}

	for_each_cache(ca, c, i)
		if (ca->alloc_thread)
			kthread_stop(ca->alloc_thread);

	if (c->journal.cur) {
		cancel_delayed_work_sync(&c->journal.work);
		/* flush last journal entry if needed */
		c->journal.work.work.func(&c->journal.work.work);
	}

	closure_return(cl);
}

/*
 * This function is only called when CACHE_SET_IO_DISABLE is set, which means
 * cache set is unregistering due to too many I/O errors. In this condition,
 * the bcache device might be stopped, it depends on stop_when_cache_set_failed
 * value and whether the broken cache has dirty data:
 *
 * dc->stop_when_cache_set_failed    dc->has_dirty   stop bcache device
 *  BCH_CACHED_STOP_AUTO               0               NO
 *  BCH_CACHED_STOP_AUTO               1               YES
 *  BCH_CACHED_DEV_STOP_ALWAYS         0               YES
 *  BCH_CACHED_DEV_STOP_ALWAYS         1               YES
 *
 * The expected behavior is, if stop_when_cache_set_failed is configured to
 * "auto" via sysfs interface, the bcache device will not be stopped if the
 * backing device is clean on the broken cache device.
 */
static void conditional_stop_bcache_device(struct cache_set *c,
					   struct bcache_device *d,
					   struct cached_dev *dc)
{
	if (dc->stop_when_cache_set_failed == BCH_CACHED_DEV_STOP_ALWAYS) {
		pr_warn("stop_when_cache_set_failed of %s is \"always\", stop it for failed cache set %pU.",
			d->disk->disk_name, c->sb.set_uuid);
		bcache_device_stop(d);
	} else if (atomic_read(&dc->has_dirty)) {
		/*
		 * dc->stop_when_cache_set_failed == BCH_CACHED_STOP_AUTO
		 * and dc->has_dirty == 1
		 */
		pr_warn("stop_when_cache_set_failed of %s is \"auto\" and cache is dirty, stop it to avoid potential data corruption.",
			d->disk->disk_name);
			bcache_device_stop(d);
	} else {
		/*
		 * dc->stop_when_cache_set_failed == BCH_CACHED_STOP_AUTO
		 * and dc->has_dirty == 0
		 */
		pr_warn("stop_when_cache_set_failed of %s is \"auto\" and cache is clean, keep it alive.",
			d->disk->disk_name);
	}
}

static void __cache_set_unregister(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, caching);
	struct cached_dev *dc;
	struct bcache_device *d;
	size_t i;

	mutex_lock(&bch_register_lock);

	for (i = 0; i < c->devices_max_used; i++) {
		d = c->devices[i];
		if (!d)
			continue;

		if (!UUID_FLASH_ONLY(&c->uuids[i]) &&
		    test_bit(CACHE_SET_UNREGISTERING, &c->flags)) {
			dc = container_of(d, struct cached_dev, disk);
			bch_cached_dev_detach(dc);
			if (test_bit(CACHE_SET_IO_DISABLE, &c->flags))
				conditional_stop_bcache_device(c, d, dc);
		} else {
			bcache_device_stop(d);
		}
	}

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
	c->devices_max_used	= 0;
	c->btree_pages		= bucket_pages(c);
	if (c->btree_pages > BTREE_MAX_PAGES)
		c->btree_pages = max_t(int, c->btree_pages / 4,
				       BTREE_MAX_PAGES);

	sema_init(&c->sb_write_mutex, 1);
	mutex_init(&c->bucket_lock);
	init_waitqueue_head(&c->btree_cache_wait);
	init_waitqueue_head(&c->bucket_wait);
	init_waitqueue_head(&c->gc_wait);
	sema_init(&c->uuid_write_mutex, 1);

	spin_lock_init(&c->btree_gc_time.lock);
	spin_lock_init(&c->btree_split_time.lock);
	spin_lock_init(&c->btree_read_time.lock);

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
	    !(c->fill_iter = mempool_create_kmalloc_pool(1, iter_size)) ||
	    !(c->bio_split = bioset_create(4, offsetof(struct bbio, bio),
					   BIOSET_NEED_BVECS |
					   BIOSET_NEED_RESCUER)) ||
	    !(c->uuids = alloc_bucket_pages(GFP_KERNEL, c)) ||
	    !(c->moving_gc_wq = alloc_workqueue("bcache_gc",
						WQ_MEM_RECLAIM, 0)) ||
	    bch_journal_alloc(c) ||
	    bch_btree_cache_alloc(c) ||
	    bch_open_buckets_alloc(c) ||
	    bch_bset_sort_state_init(&c->sort, ilog2(c->btree_pages)))
		goto err;

	c->congested_read_threshold_us	= 2000;
	c->congested_write_threshold_us	= 20000;
	c->error_limit	= DEFAULT_IO_ERROR_LIMIT;
	WARN_ON(test_and_clear_bit(CACHE_SET_IO_DISABLE, &c->flags));

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
	struct closure cl;
	unsigned i;

	closure_init_stack(&cl);

	for_each_cache(ca, c, i)
		c->nbuckets += ca->sb.nbuckets;
	set_gc_sectors(c);

	if (CACHE_SYNC(&c->sb)) {
		LIST_HEAD(journal);
		struct bkey *k;
		struct jset *j;

		err = "cannot allocate memory for journal";
		if (bch_journal_read(c, &journal))
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
		if (__bch_btree_ptr_invalid(c, k))
			goto err;

		err = "error reading btree root";
		c->root = bch_btree_node_get(c, NULL, k, j->btree_level, true, NULL);
		if (IS_ERR_OR_NULL(c->root))
			goto err;

		list_del_init(&c->root->list);
		rw_unlock(true, c->root);

		err = uuid_read(c, j, &cl);
		if (err)
			goto err;

		err = "error in recovery";
		if (bch_btree_check(c))
			goto err;

		bch_journal_mark(c, &journal);
		bch_initial_gc_finish(c);
		pr_debug("btree_check() done");

		/*
		 * bcache_journal_next() can't happen sooner, or
		 * btree_gc_finish() will give spurious errors about last_gc >
		 * gc_gen - this is a hack but oh well.
		 */
		bch_journal_next(&c->journal);

		err = "error starting allocator thread";
		for_each_cache(ca, c, i)
			if (bch_cache_allocator_start(ca))
				goto err;

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

		bch_journal_replay(c, &journal);
	} else {
		pr_notice("invalidating existing data");

		for_each_cache(ca, c, i) {
			unsigned j;

			ca->sb.keys = clamp_t(int, ca->sb.nbuckets >> 7,
					      2, SB_JOURNAL_BUCKETS);

			for (j = 0; j < ca->sb.keys; j++)
				ca->sb.d[j] = ca->sb.first_bucket + j;
		}

		bch_initial_gc_finish(c);

		err = "error starting allocator thread";
		for_each_cache(ca, c, i)
			if (bch_cache_allocator_start(ca))
				goto err;

		mutex_lock(&c->bucket_lock);
		for_each_cache(ca, c, i)
			bch_prio_write(ca);
		mutex_unlock(&c->bucket_lock);

		err = "cannot allocate new UUID bucket";
		if (__uuid_write(c))
			goto err;

		err = "cannot allocate new btree root";
		c->root = __bch_btree_node_alloc(c, NULL, 0, true, NULL);
		if (IS_ERR_OR_NULL(c->root))
			goto err;

		mutex_lock(&c->root->write_lock);
		bkey_copy_key(&c->root->key, &MAX_KEY);
		bch_btree_node_write(c->root, &cl);
		mutex_unlock(&c->root->write_lock);

		bch_btree_set_root(c->root);
		rw_unlock(true, c->root);

		/*
		 * We don't want to write the first journal entry until
		 * everything is set up - fortunately journal entries won't be
		 * written until the SET_CACHE_SYNC() here:
		 */
		SET_CACHE_SYNC(&c->sb, true);

		bch_journal_next(&c->journal);
		bch_journal_meta(c, &cl);
	}

	err = "error starting gc thread";
	if (bch_gc_thread_start(c))
		goto err;

	closure_sync(&cl);
	c->sb.last_mount = get_seconds();
	bcache_write_super(c);

	list_for_each_entry_safe(dc, t, &uncached_devices, list)
		bch_cached_dev_attach(dc, c, NULL);

	flash_devs_run(c);

	set_bit(CACHE_SET_RUNNING, &c->flags);
	return;
err:
	closure_sync(&cl);
	/* XXX: test this, it's broken */
	bch_cache_set_error(c, "%s", err);
}

static bool can_attach_cache(struct cache *ca, struct cache_set *c)
{
	return ca->sb.block_size	== c->sb.block_size &&
		ca->sb.bucket_size	== c->sb.bucket_size &&
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

	kobject_get(&ca->kobj);
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
	unsigned i;

	if (ca->set) {
		BUG_ON(ca->set->cache[ca->sb.nr_this_dev] != ca);
		ca->set->cache[ca->sb.nr_this_dev] = NULL;
	}

	free_pages((unsigned long) ca->disk_buckets, ilog2(bucket_pages(ca)));
	kfree(ca->prio_buckets);
	vfree(ca->buckets);

	free_heap(&ca->heap);
	free_fifo(&ca->free_inc);

	for (i = 0; i < RESERVE_NR; i++)
		free_fifo(&ca->free[i]);

	if (ca->sb_bio.bi_inline_vecs[0].bv_page)
		put_page(bio_first_page_all(&ca->sb_bio));

	if (!IS_ERR_OR_NULL(ca->bdev))
		blkdev_put(ca->bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);

	kfree(ca);
	module_put(THIS_MODULE);
}

static int cache_alloc(struct cache *ca)
{
	size_t free;
	size_t btree_buckets;
	struct bucket *b;

	__module_get(THIS_MODULE);
	kobject_init(&ca->kobj, &bch_cache_ktype);

	bio_init(&ca->journal.bio, ca->journal.bio.bi_inline_vecs, 8);

	/*
	 * when ca->sb.njournal_buckets is not zero, journal exists,
	 * and in bch_journal_replay(), tree node may split,
	 * so bucket of RESERVE_BTREE type is needed,
	 * the worst situation is all journal buckets are valid journal,
	 * and all the keys need to replay,
	 * so the number of  RESERVE_BTREE type buckets should be as much
	 * as journal buckets
	 */
	btree_buckets = ca->sb.njournal_buckets ?: 8;
	free = roundup_pow_of_two(ca->sb.nbuckets) >> 10;

	if (!init_fifo(&ca->free[RESERVE_BTREE], btree_buckets, GFP_KERNEL) ||
	    !init_fifo_exact(&ca->free[RESERVE_PRIO], prio_buckets(ca), GFP_KERNEL) ||
	    !init_fifo(&ca->free[RESERVE_MOVINGGC], free, GFP_KERNEL) ||
	    !init_fifo(&ca->free[RESERVE_NONE], free, GFP_KERNEL) ||
	    !init_fifo(&ca->free_inc,	free << 2, GFP_KERNEL) ||
	    !init_heap(&ca->heap,	free << 3, GFP_KERNEL) ||
	    !(ca->buckets	= vzalloc(sizeof(struct bucket) *
					  ca->sb.nbuckets)) ||
	    !(ca->prio_buckets	= kzalloc(sizeof(uint64_t) * prio_buckets(ca) *
					  2, GFP_KERNEL)) ||
	    !(ca->disk_buckets	= alloc_bucket_pages(GFP_KERNEL, ca)))
		return -ENOMEM;

	ca->prio_last_buckets = ca->prio_buckets + prio_buckets(ca);

	for_each_bucket(b, ca)
		atomic_set(&b->pin, 0);

	return 0;
}

static int register_cache(struct cache_sb *sb, struct page *sb_page,
				struct block_device *bdev, struct cache *ca)
{
	const char *err = NULL; /* must be set for any error case */
	int ret = 0;

	bdevname(bdev, ca->cache_dev_name);
	memcpy(&ca->sb, sb, sizeof(struct cache_sb));
	ca->bdev = bdev;
	ca->bdev->bd_holder = ca;

	bio_init(&ca->sb_bio, ca->sb_bio.bi_inline_vecs, 1);
	bio_first_bvec_all(&ca->sb_bio)->bv_page = sb_page;
	get_page(sb_page);

	if (blk_queue_discard(bdev_get_queue(bdev)))
		ca->discard = CACHE_DISCARD(&ca->sb);

	ret = cache_alloc(ca);
	if (ret != 0) {
		blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
		if (ret == -ENOMEM)
			err = "cache_alloc(): -ENOMEM";
		else
			err = "cache_alloc(): unknown error";
		goto err;
	}

	if (kobject_add(&ca->kobj, &part_to_dev(bdev->bd_part)->kobj, "bcache")) {
		err = "error calling kobject_add";
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&bch_register_lock);
	err = register_cache_set(ca);
	mutex_unlock(&bch_register_lock);

	if (err) {
		ret = -ENODEV;
		goto out;
	}

	pr_info("registered cache device %s", ca->cache_dev_name);

out:
	kobject_put(&ca->kobj);

err:
	if (err)
		pr_notice("error %s: %s", ca->cache_dev_name, err);

	return ret;
}

/* Global interfaces/init */

static ssize_t register_bcache(struct kobject *, struct kobj_attribute *,
			       const char *, size_t);

kobj_attribute_write(register,		register_bcache);
kobj_attribute_write(register_quiet,	register_bcache);

static bool bch_is_open_backing(struct block_device *bdev) {
	struct cache_set *c, *tc;
	struct cached_dev *dc, *t;

	list_for_each_entry_safe(c, tc, &bch_cache_sets, list)
		list_for_each_entry_safe(dc, t, &c->cached_devs, list)
			if (dc->bdev == bdev)
				return true;
	list_for_each_entry_safe(dc, t, &uncached_devices, list)
		if (dc->bdev == bdev)
			return true;
	return false;
}

static bool bch_is_open_cache(struct block_device *bdev) {
	struct cache_set *c, *tc;
	struct cache *ca;
	unsigned i;

	list_for_each_entry_safe(c, tc, &bch_cache_sets, list)
		for_each_cache(ca, c, i)
			if (ca->bdev == bdev)
				return true;
	return false;
}

static bool bch_is_open(struct block_device *bdev) {
	return bch_is_open_cache(bdev) || bch_is_open_backing(bdev);
}

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

	if (!(path = kstrndup(buffer, size, GFP_KERNEL)) ||
	    !(sb = kmalloc(sizeof(struct cache_sb), GFP_KERNEL)))
		goto err;

	err = "failed to open device";
	bdev = blkdev_get_by_path(strim(path),
				  FMODE_READ|FMODE_WRITE|FMODE_EXCL,
				  sb);
	if (IS_ERR(bdev)) {
		if (bdev == ERR_PTR(-EBUSY)) {
			bdev = lookup_bdev(strim(path));
			mutex_lock(&bch_register_lock);
			if (!IS_ERR(bdev) && bch_is_open(bdev))
				err = "device already registered";
			else
				err = "device busy";
			mutex_unlock(&bch_register_lock);
			if (!IS_ERR(bdev))
				bdput(bdev);
			if (attr == &ksysfs_register_quiet)
				goto out;
		}
		goto err;
	}

	err = "failed to set blocksize";
	if (set_blocksize(bdev, 4096))
		goto err_close;

	err = read_super(sb, bdev, &sb_page);
	if (err)
		goto err_close;

	err = "failed to register device";
	if (SB_IS_BDEV(sb)) {
		struct cached_dev *dc = kzalloc(sizeof(*dc), GFP_KERNEL);
		if (!dc)
			goto err_close;

		mutex_lock(&bch_register_lock);
		register_bdev(sb, sb_page, bdev, dc);
		mutex_unlock(&bch_register_lock);
	} else {
		struct cache *ca = kzalloc(sizeof(*ca), GFP_KERNEL);
		if (!ca)
			goto err_close;

		if (register_cache(sb, sb_page, bdev, ca) != 0)
			goto err;
	}
out:
	if (sb_page)
		put_page(sb_page);
	kfree(sb);
	kfree(path);
	module_put(THIS_MODULE);
	return ret;

err_close:
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
err:
	pr_info("error %s: %s", path, err);
	ret = -EINVAL;
	goto out;
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
	bch_request_exit();
	if (bcache_kobj)
		kobject_put(bcache_kobj);
	if (bcache_wq)
		destroy_workqueue(bcache_wq);
	if (bcache_major)
		unregister_blkdev(bcache_major, "bcache");
	unregister_reboot_notifier(&reboot);
	mutex_destroy(&bch_register_lock);
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

	bcache_major = register_blkdev(0, "bcache");
	if (bcache_major < 0) {
		unregister_reboot_notifier(&reboot);
		mutex_destroy(&bch_register_lock);
		return bcache_major;
	}

	if (!(bcache_wq = alloc_workqueue("bcache", WQ_MEM_RECLAIM, 0)) ||
	    !(bcache_kobj = kobject_create_and_add("bcache", fs_kobj)) ||
	    bch_request_init() ||
	    bch_debug_init(bcache_kobj) || closure_debug_init() ||
	    sysfs_create_files(bcache_kobj, files))
		goto err;

	return 0;
err:
	bcache_exit();
	return -ENOMEM;
}

module_exit(bcache_exit);
module_init(bcache_init);
