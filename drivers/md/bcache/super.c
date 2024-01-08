// SPDX-License-Identifier: GPL-2.0
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
#include "features.h"

#include <linux/blkdev.h>
#include <linux/pagemap.h>
#include <linux/debugfs.h>
#include <linux/idr.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/reboot.h>
#include <linux/sysfs.h>

unsigned int bch_cutoff_writeback;
unsigned int bch_cutoff_writeback_sync;

static const char bcache_magic[] = {
	0xc6, 0x85, 0x73, 0xf6, 0x4e, 0x1a, 0x45, 0xca,
	0x82, 0x65, 0xf5, 0x7f, 0x48, 0xba, 0x6d, 0x81
};

static const char invalid_uuid[] = {
	0xa0, 0x3e, 0xf8, 0xed, 0x3e, 0xe1, 0xb8, 0x78,
	0xc8, 0x50, 0xfc, 0x5e, 0xcb, 0x16, 0xcd, 0x99
};

static struct kobject *bcache_kobj;
struct mutex bch_register_lock;
bool bcache_is_reboot;
LIST_HEAD(bch_cache_sets);
static LIST_HEAD(uncached_devices);

static int bcache_major;
static DEFINE_IDA(bcache_device_idx);
static wait_queue_head_t unregister_wait;
struct workqueue_struct *bcache_wq;
struct workqueue_struct *bch_flush_wq;
struct workqueue_struct *bch_journal_wq;


#define BTREE_MAX_PAGES		(256 * 1024 / PAGE_SIZE)
/* limitation of partitions number on single bcache device */
#define BCACHE_MINORS		128
/* limitation of bcache devices number on single system */
#define BCACHE_DEVICE_IDX_MAX	((1U << MINORBITS)/BCACHE_MINORS)

/* Superblock */

static unsigned int get_bucket_size(struct cache_sb *sb, struct cache_sb_disk *s)
{
	unsigned int bucket_size = le16_to_cpu(s->bucket_size);

	if (sb->version >= BCACHE_SB_VERSION_CDEV_WITH_FEATURES) {
		if (bch_has_feature_large_bucket(sb)) {
			unsigned int max, order;

			max = sizeof(unsigned int) * BITS_PER_BYTE - 1;
			order = le16_to_cpu(s->bucket_size);
			/*
			 * bcache tool will make sure the overflow won't
			 * happen, an error message here is enough.
			 */
			if (order > max)
				pr_err("Bucket size (1 << %u) overflows\n",
					order);
			bucket_size = 1 << order;
		} else if (bch_has_feature_obso_large_bucket(sb)) {
			bucket_size +=
				le16_to_cpu(s->obso_bucket_size_hi) << 16;
		}
	}

	return bucket_size;
}

static const char *read_super_common(struct cache_sb *sb,  struct block_device *bdev,
				     struct cache_sb_disk *s)
{
	const char *err;
	unsigned int i;

	sb->first_bucket= le16_to_cpu(s->first_bucket);
	sb->nbuckets	= le64_to_cpu(s->nbuckets);
	sb->bucket_size	= get_bucket_size(sb, s);

	sb->nr_in_set	= le16_to_cpu(s->nr_in_set);
	sb->nr_this_dev	= le16_to_cpu(s->nr_this_dev);

	err = "Too many journal buckets";
	if (sb->keys > SB_JOURNAL_BUCKETS)
		goto err;

	err = "Too many buckets";
	if (sb->nbuckets > LONG_MAX)
		goto err;

	err = "Not enough buckets";
	if (sb->nbuckets < 1 << 7)
		goto err;

	err = "Bad block size (not power of 2)";
	if (!is_power_of_2(sb->block_size))
		goto err;

	err = "Bad block size (larger than page size)";
	if (sb->block_size > PAGE_SECTORS)
		goto err;

	err = "Bad bucket size (not power of 2)";
	if (!is_power_of_2(sb->bucket_size))
		goto err;

	err = "Bad bucket size (smaller than page size)";
	if (sb->bucket_size < PAGE_SECTORS)
		goto err;

	err = "Invalid superblock: device too small";
	if (get_capacity(bdev->bd_disk) <
	    sb->bucket_size * sb->nbuckets)
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

	err = NULL;
err:
	return err;
}


static const char *read_super(struct cache_sb *sb, struct block_device *bdev,
			      struct cache_sb_disk **res)
{
	const char *err;
	struct cache_sb_disk *s;
	struct page *page;
	unsigned int i;

	page = read_cache_page_gfp(bdev->bd_inode->i_mapping,
				   SB_OFFSET >> PAGE_SHIFT, GFP_KERNEL);
	if (IS_ERR(page))
		return "IO error";
	s = page_address(page) + offset_in_page(SB_OFFSET);

	sb->offset		= le64_to_cpu(s->offset);
	sb->version		= le64_to_cpu(s->version);

	memcpy(sb->magic,	s->magic, 16);
	memcpy(sb->uuid,	s->uuid, 16);
	memcpy(sb->set_uuid,	s->set_uuid, 16);
	memcpy(sb->label,	s->label, SB_LABEL_SIZE);

	sb->flags		= le64_to_cpu(s->flags);
	sb->seq			= le64_to_cpu(s->seq);
	sb->last_mount		= le32_to_cpu(s->last_mount);
	sb->keys		= le16_to_cpu(s->keys);

	for (i = 0; i < SB_JOURNAL_BUCKETS; i++)
		sb->d[i] = le64_to_cpu(s->d[i]);

	pr_debug("read sb version %llu, flags %llu, seq %llu, journal size %u\n",
		 sb->version, sb->flags, sb->seq, sb->keys);

	err = "Not a bcache superblock (bad offset)";
	if (sb->offset != SB_SECTOR)
		goto err;

	err = "Not a bcache superblock (bad magic)";
	if (memcmp(sb->magic, bcache_magic, 16))
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
	case BCACHE_SB_VERSION_BDEV_WITH_FEATURES:
		sb->data_offset	= le64_to_cpu(s->data_offset);

		err = "Bad data offset";
		if (sb->data_offset < BDEV_DATA_START_DEFAULT)
			goto err;

		break;
	case BCACHE_SB_VERSION_CDEV:
	case BCACHE_SB_VERSION_CDEV_WITH_UUID:
		err = read_super_common(sb, bdev, s);
		if (err)
			goto err;
		break;
	case BCACHE_SB_VERSION_CDEV_WITH_FEATURES:
		/*
		 * Feature bits are needed in read_super_common(),
		 * convert them firstly.
		 */
		sb->feature_compat = le64_to_cpu(s->feature_compat);
		sb->feature_incompat = le64_to_cpu(s->feature_incompat);
		sb->feature_ro_compat = le64_to_cpu(s->feature_ro_compat);

		/* Check incompatible features */
		err = "Unsupported compatible feature found";
		if (bch_has_unknown_compat_features(sb))
			goto err;

		err = "Unsupported read-only compatible feature found";
		if (bch_has_unknown_ro_compat_features(sb))
			goto err;

		err = "Unsupported incompatible feature found";
		if (bch_has_unknown_incompat_features(sb))
			goto err;

		err = read_super_common(sb, bdev, s);
		if (err)
			goto err;
		break;
	default:
		err = "Unsupported superblock version";
		goto err;
	}

	sb->last_mount = (u32)ktime_get_real_seconds();
	*res = s;
	return NULL;
err:
	put_page(page);
	return err;
}

static void write_bdev_super_endio(struct bio *bio)
{
	struct cached_dev *dc = bio->bi_private;

	if (bio->bi_status)
		bch_count_backing_io_errors(dc, bio);

	closure_put(&dc->sb_write);
}

static void __write_super(struct cache_sb *sb, struct cache_sb_disk *out,
		struct bio *bio)
{
	unsigned int i;

	bio->bi_opf = REQ_OP_WRITE | REQ_SYNC | REQ_META;
	bio->bi_iter.bi_sector	= SB_SECTOR;
	__bio_add_page(bio, virt_to_page(out), SB_SIZE,
			offset_in_page(out));

	out->offset		= cpu_to_le64(sb->offset);

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

	if (sb->version >= BCACHE_SB_VERSION_CDEV_WITH_FEATURES) {
		out->feature_compat    = cpu_to_le64(sb->feature_compat);
		out->feature_incompat  = cpu_to_le64(sb->feature_incompat);
		out->feature_ro_compat = cpu_to_le64(sb->feature_ro_compat);
	}

	out->version		= cpu_to_le64(sb->version);
	out->csum = csum_set(out);

	pr_debug("ver %llu, flags %llu, seq %llu\n",
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

	bio_init(bio, dc->bdev, dc->sb_bv, 1, 0);
	bio->bi_end_io	= write_bdev_super_endio;
	bio->bi_private = dc;

	closure_get(cl);
	/* I/O request sent to backing device */
	__write_super(&dc->sb, dc->sb_disk, bio);

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
	struct cache *ca = c->cache;
	struct bio *bio = &ca->sb_bio;
	unsigned int version = BCACHE_SB_VERSION_CDEV_WITH_UUID;

	down(&c->sb_write_mutex);
	closure_init(cl, &c->cl);

	ca->sb.seq++;

	if (ca->sb.version < version)
		ca->sb.version = version;

	bio_init(bio, ca->bdev, ca->sb_bv, 1, 0);
	bio->bi_end_io	= write_super_endio;
	bio->bi_private = ca;

	closure_get(cl);
	__write_super(&ca->sb, ca->sb_disk, bio);

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

static void uuid_io(struct cache_set *c, blk_opf_t opf, struct bkey *k,
		    struct closure *parent)
{
	struct closure *cl = &c->uuid_write;
	struct uuid_entry *u;
	unsigned int i;
	char buf[80];

	BUG_ON(!parent);
	down(&c->uuid_write_mutex);
	closure_init(cl, parent);

	for (i = 0; i < KEY_PTRS(k); i++) {
		struct bio *bio = bch_bbio_alloc(c);

		bio->bi_opf = opf | REQ_SYNC | REQ_META;
		bio->bi_iter.bi_size = KEY_SIZE(k) << 9;

		bio->bi_end_io	= uuid_endio;
		bio->bi_private = cl;
		bch_bio_map(bio, c->uuids);

		bch_submit_bbio(bio, c, k, i);

		if ((opf & REQ_OP_MASK) != REQ_OP_WRITE)
			break;
	}

	bch_extent_to_text(buf, sizeof(buf), k);
	pr_debug("%s UUIDs at %s\n", (opf & REQ_OP_MASK) == REQ_OP_WRITE ?
		 "wrote" : "read", buf);

	for (u = c->uuids; u < c->uuids + c->nr_uuids; u++)
		if (!bch_is_zero(u->uuid, 16))
			pr_debug("Slot %zi: %pU: %s: 1st: %u last: %u inv: %u\n",
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
	uuid_io(c, REQ_OP_READ, k, cl);

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
	struct cache *ca = c->cache;
	unsigned int size;

	closure_init_stack(&cl);
	lockdep_assert_held(&bch_register_lock);

	if (bch_bucket_alloc_set(c, RESERVE_BTREE, &k.key, true))
		return 1;

	size =  meta_bucket_pages(&ca->sb) * PAGE_SECTORS;
	SET_KEY_SIZE(&k.key, size);
	uuid_io(c, REQ_OP_WRITE, &k.key, &cl);
	closure_sync(&cl);

	/* Only one bucket used for uuid write */
	atomic_long_add(ca->sb.bucket_size, &ca->meta_sectors_written);

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
 *   8 bit gen
 *  16 bit priority
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

static void prio_io(struct cache *ca, uint64_t bucket, blk_opf_t opf)
{
	struct closure *cl = &ca->prio;
	struct bio *bio = bch_bbio_alloc(ca->set);

	closure_init_stack(cl);

	bio->bi_iter.bi_sector	= bucket * ca->sb.bucket_size;
	bio_set_dev(bio, ca->bdev);
	bio->bi_iter.bi_size	= meta_bucket_bytes(&ca->sb);

	bio->bi_end_io	= prio_endio;
	bio->bi_private = ca;
	bio->bi_opf = opf | REQ_SYNC | REQ_META;
	bch_bio_map(bio, ca->disk_buckets);

	closure_bio_submit(ca->set, bio, &ca->prio);
	closure_sync(cl);
}

int bch_prio_write(struct cache *ca, bool wait)
{
	int i;
	struct bucket *b;
	struct closure cl;

	pr_debug("free_prio=%zu, free_none=%zu, free_inc=%zu\n",
		 fifo_used(&ca->free[RESERVE_PRIO]),
		 fifo_used(&ca->free[RESERVE_NONE]),
		 fifo_used(&ca->free_inc));

	/*
	 * Pre-check if there are enough free buckets. In the non-blocking
	 * scenario it's better to fail early rather than starting to allocate
	 * buckets and do a cleanup later in case of failure.
	 */
	if (!wait) {
		size_t avail = fifo_used(&ca->free[RESERVE_PRIO]) +
			       fifo_used(&ca->free[RESERVE_NONE]);
		if (prio_buckets(ca) > avail)
			return -ENOMEM;
	}

	closure_init_stack(&cl);

	lockdep_assert_held(&ca->set->bucket_lock);

	ca->disk_buckets->seq++;

	atomic_long_add(ca->sb.bucket_size * prio_buckets(ca),
			&ca->meta_sectors_written);

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
		p->csum		= bch_crc64(&p->magic, meta_bucket_bytes(&ca->sb) - 8);

		bucket = bch_bucket_alloc(ca, RESERVE_PRIO, wait);
		BUG_ON(bucket == -1);

		mutex_unlock(&ca->set->bucket_lock);
		prio_io(ca, bucket, REQ_OP_WRITE);
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
	return 0;
}

static int prio_read(struct cache *ca, uint64_t bucket)
{
	struct prio_set *p = ca->disk_buckets;
	struct bucket_disk *d = p->data + prios_per_bucket(ca), *end = d;
	struct bucket *b;
	unsigned int bucket_nr = 0;
	int ret = -EIO;

	for (b = ca->buckets;
	     b < ca->buckets + ca->sb.nbuckets;
	     b++, d++) {
		if (d == end) {
			ca->prio_buckets[bucket_nr] = bucket;
			ca->prio_last_buckets[bucket_nr] = bucket;
			bucket_nr++;

			prio_io(ca, bucket, REQ_OP_READ);

			if (p->csum !=
			    bch_crc64(&p->magic, meta_bucket_bytes(&ca->sb) - 8)) {
				pr_warn("bad csum reading priorities\n");
				goto out;
			}

			if (p->magic != pset_magic(&ca->sb)) {
				pr_warn("bad magic reading priorities\n");
				goto out;
			}

			bucket = p->next_bucket;
			d = p->data;
		}

		b->prio = le16_to_cpu(d->prio);
		b->gen = b->last_gc = d->gen;
	}

	ret = 0;
out:
	return ret;
}

/* Bcache device */

static int open_dev(struct gendisk *disk, blk_mode_t mode)
{
	struct bcache_device *d = disk->private_data;

	if (test_bit(BCACHE_DEV_CLOSING, &d->flags))
		return -ENXIO;

	closure_get(&d->cl);
	return 0;
}

static void release_dev(struct gendisk *b)
{
	struct bcache_device *d = b->private_data;

	closure_put(&d->cl);
}

static int ioctl_dev(struct block_device *b, blk_mode_t mode,
		     unsigned int cmd, unsigned long arg)
{
	struct bcache_device *d = b->bd_disk->private_data;

	return d->ioctl(d, mode, cmd, arg);
}

static const struct block_device_operations bcache_cached_ops = {
	.submit_bio	= cached_dev_submit_bio,
	.open		= open_dev,
	.release	= release_dev,
	.ioctl		= ioctl_dev,
	.owner		= THIS_MODULE,
};

static const struct block_device_operations bcache_flash_ops = {
	.submit_bio	= flash_dev_submit_bio,
	.open		= open_dev,
	.release	= release_dev,
	.ioctl		= ioctl_dev,
	.owner		= THIS_MODULE,
};

void bcache_device_stop(struct bcache_device *d)
{
	if (!test_and_set_bit(BCACHE_DEV_CLOSING, &d->flags))
		/*
		 * closure_fn set to
		 * - cached device: cached_dev_flush()
		 * - flash dev: flash_dev_flush()
		 */
		closure_queue(&d->cl);
}

static void bcache_device_unlink(struct bcache_device *d)
{
	lockdep_assert_held(&bch_register_lock);

	if (d->c && !test_and_set_bit(BCACHE_DEV_UNLINK_DONE, &d->flags)) {
		struct cache *ca = d->c->cache;

		sysfs_remove_link(&d->c->kobj, d->name);
		sysfs_remove_link(&d->kobj, "cache");

		bd_unlink_disk_holder(ca->bdev, d->disk);
	}
}

static void bcache_device_link(struct bcache_device *d, struct cache_set *c,
			       const char *name)
{
	struct cache *ca = c->cache;
	int ret;

	bd_link_disk_holder(ca->bdev, d->disk);

	snprintf(d->name, BCACHEDEVNAME_SIZE,
		 "%s%u", name, d->id);

	ret = sysfs_create_link(&d->kobj, &c->kobj, "cache");
	if (ret < 0)
		pr_err("Couldn't create device -> cache set symlink\n");

	ret = sysfs_create_link(&c->kobj, &d->kobj, d->name);
	if (ret < 0)
		pr_err("Couldn't create cache set -> device symlink\n");

	clear_bit(BCACHE_DEV_UNLINK_DONE, &d->flags);
}

static void bcache_device_detach(struct bcache_device *d)
{
	lockdep_assert_held(&bch_register_lock);

	atomic_dec(&d->c->attached_dev_nr);

	if (test_bit(BCACHE_DEV_DETACHING, &d->flags)) {
		struct uuid_entry *u = d->c->uuids + d->id;

		SET_UUID_FLASH_ONLY(u, 0);
		memcpy(u->uuid, invalid_uuid, 16);
		u->invalidated = cpu_to_le32((u32)ktime_get_real_seconds());
		bch_uuid_write(d->c);
	}

	bcache_device_unlink(d);

	d->c->devices[d->id] = NULL;
	closure_put(&d->c->caching);
	d->c = NULL;
}

static void bcache_device_attach(struct bcache_device *d, struct cache_set *c,
				 unsigned int id)
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
	struct gendisk *disk = d->disk;

	lockdep_assert_held(&bch_register_lock);

	if (disk)
		pr_info("%s stopped\n", disk->disk_name);
	else
		pr_err("bcache device (NULL gendisk) stopped\n");

	if (d->c)
		bcache_device_detach(d);

	if (disk) {
		ida_simple_remove(&bcache_device_idx,
				  first_minor_to_idx(disk->first_minor));
		put_disk(disk);
	}

	bioset_exit(&d->bio_split);
	kvfree(d->full_dirty_stripes);
	kvfree(d->stripe_sectors_dirty);

	closure_debug_destroy(&d->cl);
}

static int bcache_device_init(struct bcache_device *d, unsigned int block_size,
		sector_t sectors, struct block_device *cached_bdev,
		const struct block_device_operations *ops)
{
	struct request_queue *q;
	const size_t max_stripes = min_t(size_t, INT_MAX,
					 SIZE_MAX / sizeof(atomic_t));
	uint64_t n;
	int idx;

	if (!d->stripe_size)
		d->stripe_size = 1 << 31;
	else if (d->stripe_size < BCH_MIN_STRIPE_SZ)
		d->stripe_size = roundup(BCH_MIN_STRIPE_SZ, d->stripe_size);

	n = DIV_ROUND_UP_ULL(sectors, d->stripe_size);
	if (!n || n > max_stripes) {
		pr_err("nr_stripes too large or invalid: %llu (start sector beyond end of disk?)\n",
			n);
		return -ENOMEM;
	}
	d->nr_stripes = n;

	n = d->nr_stripes * sizeof(atomic_t);
	d->stripe_sectors_dirty = kvzalloc(n, GFP_KERNEL);
	if (!d->stripe_sectors_dirty)
		return -ENOMEM;

	n = BITS_TO_LONGS(d->nr_stripes) * sizeof(unsigned long);
	d->full_dirty_stripes = kvzalloc(n, GFP_KERNEL);
	if (!d->full_dirty_stripes)
		goto out_free_stripe_sectors_dirty;

	idx = ida_simple_get(&bcache_device_idx, 0,
				BCACHE_DEVICE_IDX_MAX, GFP_KERNEL);
	if (idx < 0)
		goto out_free_full_dirty_stripes;

	if (bioset_init(&d->bio_split, 4, offsetof(struct bbio, bio),
			BIOSET_NEED_BVECS|BIOSET_NEED_RESCUER))
		goto out_ida_remove;

	d->disk = blk_alloc_disk(NUMA_NO_NODE);
	if (!d->disk)
		goto out_bioset_exit;

	set_capacity(d->disk, sectors);
	snprintf(d->disk->disk_name, DISK_NAME_LEN, "bcache%i", idx);

	d->disk->major		= bcache_major;
	d->disk->first_minor	= idx_to_first_minor(idx);
	d->disk->minors		= BCACHE_MINORS;
	d->disk->fops		= ops;
	d->disk->private_data	= d;

	q = d->disk->queue;
	q->limits.max_hw_sectors	= UINT_MAX;
	q->limits.max_sectors		= UINT_MAX;
	q->limits.max_segment_size	= UINT_MAX;
	q->limits.max_segments		= BIO_MAX_VECS;
	blk_queue_max_discard_sectors(q, UINT_MAX);
	q->limits.discard_granularity	= 512;
	q->limits.io_min		= block_size;
	q->limits.logical_block_size	= block_size;
	q->limits.physical_block_size	= block_size;

	if (q->limits.logical_block_size > PAGE_SIZE && cached_bdev) {
		/*
		 * This should only happen with BCACHE_SB_VERSION_BDEV.
		 * Block/page size is checked for BCACHE_SB_VERSION_CDEV.
		 */
		pr_info("%s: sb/logical block size (%u) greater than page size (%lu) falling back to device logical block size (%u)\n",
			d->disk->disk_name, q->limits.logical_block_size,
			PAGE_SIZE, bdev_logical_block_size(cached_bdev));

		/* This also adjusts physical block size/min io size if needed */
		blk_queue_logical_block_size(q, bdev_logical_block_size(cached_bdev));
	}

	blk_queue_flag_set(QUEUE_FLAG_NONROT, d->disk->queue);

	blk_queue_write_cache(q, true, true);

	return 0;

out_bioset_exit:
	bioset_exit(&d->bio_split);
out_ida_remove:
	ida_simple_remove(&bcache_device_idx, idx);
out_free_full_dirty_stripes:
	kvfree(d->full_dirty_stripes);
out_free_stripe_sectors_dirty:
	kvfree(d->stripe_sectors_dirty);
	return -ENOMEM;

}

/* Cached device */

static void calc_cached_dev_sectors(struct cache_set *c)
{
	uint64_t sectors = 0;
	struct cached_dev *dc;

	list_for_each_entry(dc, &c->cached_devs, list)
		sectors += bdev_nr_sectors(dc->bdev);

	c->cached_dev_sectors = sectors;
}

#define BACKING_DEV_OFFLINE_TIMEOUT 5
static int cached_dev_status_update(void *arg)
{
	struct cached_dev *dc = arg;
	struct request_queue *q;

	/*
	 * If this delayed worker is stopping outside, directly quit here.
	 * dc->io_disable might be set via sysfs interface, so check it
	 * here too.
	 */
	while (!kthread_should_stop() && !dc->io_disable) {
		q = bdev_get_queue(dc->bdev);
		if (blk_queue_dying(q))
			dc->offline_seconds++;
		else
			dc->offline_seconds = 0;

		if (dc->offline_seconds >= BACKING_DEV_OFFLINE_TIMEOUT) {
			pr_err("%pg: device offline for %d seconds\n",
			       dc->bdev,
			       BACKING_DEV_OFFLINE_TIMEOUT);
			pr_err("%s: disable I/O request due to backing device offline\n",
			       dc->disk.name);
			dc->io_disable = true;
			/* let others know earlier that io_disable is true */
			smp_mb();
			bcache_device_stop(&dc->disk);
			break;
		}
		schedule_timeout_interruptible(HZ);
	}

	wait_for_kthread_stop();
	return 0;
}


int bch_cached_dev_run(struct cached_dev *dc)
{
	int ret = 0;
	struct bcache_device *d = &dc->disk;
	char *buf = kmemdup_nul(dc->sb.label, SB_LABEL_SIZE, GFP_KERNEL);
	char *env[] = {
		"DRIVER=bcache",
		kasprintf(GFP_KERNEL, "CACHED_UUID=%pU", dc->sb.uuid),
		kasprintf(GFP_KERNEL, "CACHED_LABEL=%s", buf ? : ""),
		NULL,
	};

	if (dc->io_disable) {
		pr_err("I/O disabled on cached dev %pg\n", dc->bdev);
		ret = -EIO;
		goto out;
	}

	if (atomic_xchg(&dc->running, 1)) {
		pr_info("cached dev %pg is running already\n", dc->bdev);
		ret = -EBUSY;
		goto out;
	}

	if (!d->c &&
	    BDEV_STATE(&dc->sb) != BDEV_STATE_NONE) {
		struct closure cl;

		closure_init_stack(&cl);

		SET_BDEV_STATE(&dc->sb, BDEV_STATE_STALE);
		bch_write_bdev_super(dc, &cl);
		closure_sync(&cl);
	}

	ret = add_disk(d->disk);
	if (ret)
		goto out;
	bd_link_disk_holder(dc->bdev, dc->disk.disk);
	/*
	 * won't show up in the uevent file, use udevadm monitor -e instead
	 * only class / kset properties are persistent
	 */
	kobject_uevent_env(&disk_to_dev(d->disk)->kobj, KOBJ_CHANGE, env);

	if (sysfs_create_link(&d->kobj, &disk_to_dev(d->disk)->kobj, "dev") ||
	    sysfs_create_link(&disk_to_dev(d->disk)->kobj,
			      &d->kobj, "bcache")) {
		pr_err("Couldn't create bcache dev <-> disk sysfs symlinks\n");
		ret = -ENOMEM;
		goto out;
	}

	dc->status_update_thread = kthread_run(cached_dev_status_update,
					       dc, "bcache_status_update");
	if (IS_ERR(dc->status_update_thread)) {
		pr_warn("failed to create bcache_status_update kthread, continue to run without monitoring backing device status\n");
	}

out:
	kfree(env[1]);
	kfree(env[2]);
	kfree(buf);
	return ret;
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
		pr_warn("give up waiting for dc->writeback_write_update to quit\n");

	cancel_delayed_work_sync(&dc->writeback_rate_update);
}

static void cached_dev_detach_finish(struct work_struct *w)
{
	struct cached_dev *dc = container_of(w, struct cached_dev, detach);
	struct cache_set *c = dc->disk.c;

	BUG_ON(!test_bit(BCACHE_DEV_DETACHING, &dc->disk.flags));
	BUG_ON(refcount_read(&dc->count));


	if (test_and_clear_bit(BCACHE_DEV_WB_RUNNING, &dc->disk.flags))
		cancel_writeback_rate_update_dwork(dc);

	if (!IS_ERR_OR_NULL(dc->writeback_thread)) {
		kthread_stop(dc->writeback_thread);
		dc->writeback_thread = NULL;
	}

	mutex_lock(&bch_register_lock);

	bcache_device_detach(&dc->disk);
	list_move(&dc->list, &uncached_devices);
	calc_cached_dev_sectors(c);

	clear_bit(BCACHE_DEV_DETACHING, &dc->disk.flags);
	clear_bit(BCACHE_DEV_UNLINK_DONE, &dc->disk.flags);

	mutex_unlock(&bch_register_lock);

	pr_info("Caching disabled for %pg\n", dc->bdev);

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
	uint32_t rtime = cpu_to_le32((u32)ktime_get_real_seconds());
	struct uuid_entry *u;
	struct cached_dev *exist_dc, *t;
	int ret = 0;

	if ((set_uuid && memcmp(set_uuid, c->set_uuid, 16)) ||
	    (!set_uuid && memcmp(dc->sb.set_uuid, c->set_uuid, 16)))
		return -ENOENT;

	if (dc->disk.c) {
		pr_err("Can't attach %pg: already attached\n", dc->bdev);
		return -EINVAL;
	}

	if (test_bit(CACHE_SET_STOPPING, &c->flags)) {
		pr_err("Can't attach %pg: shutting down\n", dc->bdev);
		return -EINVAL;
	}

	if (dc->sb.block_size < c->cache->sb.block_size) {
		/* Will die */
		pr_err("Couldn't attach %pg: block size less than set's block size\n",
		       dc->bdev);
		return -EINVAL;
	}

	/* Check whether already attached */
	list_for_each_entry_safe(exist_dc, t, &c->cached_devs, list) {
		if (!memcmp(dc->sb.uuid, exist_dc->sb.uuid, 16)) {
			pr_err("Tried to attach %pg but duplicate UUID already attached\n",
				dc->bdev);

			return -EINVAL;
		}
	}

	u = uuid_find(c, dc->sb.uuid);

	if (u &&
	    (BDEV_STATE(&dc->sb) == BDEV_STATE_STALE ||
	     BDEV_STATE(&dc->sb) == BDEV_STATE_NONE)) {
		memcpy(u->uuid, invalid_uuid, 16);
		u->invalidated = cpu_to_le32((u32)ktime_get_real_seconds());
		u = NULL;
	}

	if (!u) {
		if (BDEV_STATE(&dc->sb) == BDEV_STATE_DIRTY) {
			pr_err("Couldn't find uuid for %pg in set\n", dc->bdev);
			return -ENOENT;
		}

		u = uuid_find_empty(c);
		if (!u) {
			pr_err("Not caching %pg, no room for UUID\n", dc->bdev);
			return -EINVAL;
		}
	}

	/*
	 * Deadlocks since we're called via sysfs...
	 * sysfs_remove_file(&dc->kobj, &sysfs_attach);
	 */

	if (bch_is_zero(u->uuid, 16)) {
		struct closure cl;

		closure_init_stack(&cl);

		memcpy(u->uuid, dc->sb.uuid, 16);
		memcpy(u->label, dc->sb.label, SB_LABEL_SIZE);
		u->first_reg = u->last_reg = rtime;
		bch_uuid_write(c);

		memcpy(dc->sb.set_uuid, c->set_uuid, 16);
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

	/*
	 * dc->c must be set before dc->count != 0 - paired with the mb in
	 * cached_dev_get()
	 */
	smp_wmb();
	refcount_set(&dc->count, 1);

	/* Block writeback thread, but spawn it */
	down_write(&dc->writeback_lock);
	if (bch_cached_dev_writeback_start(dc)) {
		up_write(&dc->writeback_lock);
		pr_err("Couldn't start writeback facilities for %s\n",
		       dc->disk.disk->disk_name);
		return -ENOMEM;
	}

	if (BDEV_STATE(&dc->sb) == BDEV_STATE_DIRTY) {
		atomic_set(&dc->has_dirty, 1);
		bch_writeback_queue(dc);
	}

	bch_sectors_dirty_init(&dc->disk);

	ret = bch_cached_dev_run(dc);
	if (ret && (ret != -EBUSY)) {
		up_write(&dc->writeback_lock);
		/*
		 * bch_register_lock is held, bcache_device_stop() is not
		 * able to be directly called. The kthread and kworker
		 * created previously in bch_cached_dev_writeback_start()
		 * have to be stopped manually here.
		 */
		kthread_stop(dc->writeback_thread);
		cancel_writeback_rate_update_dwork(dc);
		pr_err("Couldn't run cached device %pg\n", dc->bdev);
		return ret;
	}

	bcache_device_link(&dc->disk, c, "bdev");
	atomic_inc(&c->attached_dev_nr);

	if (bch_has_feature_obso_large_bucket(&(c->cache->sb))) {
		pr_err("The obsoleted large bucket layout is unsupported, set the bcache device into read-only\n");
		pr_err("Please update to the latest bcache-tools to create the cache device\n");
		set_disk_ro(dc->disk.disk, 1);
	}

	/* Allow the writeback thread to proceed */
	up_write(&dc->writeback_lock);

	pr_info("Caching %pg as %s on set %pU\n",
		dc->bdev,
		dc->disk.disk->disk_name,
		dc->disk.c->set_uuid);
	return 0;
}

/* when dc->disk.kobj released */
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

	if (test_and_clear_bit(BCACHE_DEV_WB_RUNNING, &dc->disk.flags))
		cancel_writeback_rate_update_dwork(dc);

	if (!IS_ERR_OR_NULL(dc->writeback_thread))
		kthread_stop(dc->writeback_thread);
	if (!IS_ERR_OR_NULL(dc->status_update_thread))
		kthread_stop(dc->status_update_thread);

	mutex_lock(&bch_register_lock);

	if (atomic_read(&dc->running)) {
		bd_unlink_disk_holder(dc->bdev, dc->disk.disk);
		del_gendisk(dc->disk.disk);
	}
	bcache_device_free(&dc->disk);
	list_del(&dc->list);

	mutex_unlock(&bch_register_lock);

	if (dc->sb_disk)
		put_page(virt_to_page(dc->sb_disk));

	if (dc->bdev_handle)
		bdev_release(dc->bdev_handle);

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

static int cached_dev_init(struct cached_dev *dc, unsigned int block_size)
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
			 bdev_nr_sectors(dc->bdev) - dc->sb.data_offset,
			 dc->bdev, &bcache_cached_ops);
	if (ret)
		return ret;

	blk_queue_io_opt(dc->disk.disk->queue,
		max(queue_io_opt(dc->disk.disk->queue), queue_io_opt(q)));

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

static int register_bdev(struct cache_sb *sb, struct cache_sb_disk *sb_disk,
				 struct bdev_handle *bdev_handle,
				 struct cached_dev *dc)
{
	const char *err = "cannot allocate memory";
	struct cache_set *c;
	int ret = -ENOMEM;

	memcpy(&dc->sb, sb, sizeof(struct cache_sb));
	dc->bdev_handle = bdev_handle;
	dc->bdev = bdev_handle->bdev;
	dc->sb_disk = sb_disk;

	if (cached_dev_init(dc, sb->block_size << 9))
		goto err;

	err = "error creating kobject";
	if (kobject_add(&dc->disk.kobj, bdev_kobj(dc->bdev), "bcache"))
		goto err;
	if (bch_cache_accounting_add_kobjs(&dc->accounting, &dc->disk.kobj))
		goto err;

	pr_info("registered backing device %pg\n", dc->bdev);

	list_add(&dc->list, &uncached_devices);
	/* attach to a matched cache set if it exists */
	list_for_each_entry(c, &bch_cache_sets, list)
		bch_cached_dev_attach(dc, c, NULL);

	if (BDEV_STATE(&dc->sb) == BDEV_STATE_NONE ||
	    BDEV_STATE(&dc->sb) == BDEV_STATE_STALE) {
		err = "failed to run cached device";
		ret = bch_cached_dev_run(dc);
		if (ret)
			goto err;
	}

	return 0;
err:
	pr_notice("error %pg: %s\n", dc->bdev, err);
	bcache_device_stop(&dc->disk);
	return ret;
}

/* Flash only volumes */

/* When d->kobj released */
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
	atomic_long_sub(bcache_dev_sectors_dirty(d),
			&d->c->flash_dev_dirty_sectors);
	del_gendisk(d->disk);
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
	int err = -ENOMEM;
	struct bcache_device *d = kzalloc(sizeof(struct bcache_device),
					  GFP_KERNEL);
	if (!d)
		goto err_ret;

	closure_init(&d->cl, NULL);
	set_closure_fn(&d->cl, flash_dev_flush, system_wq);

	kobject_init(&d->kobj, &bch_flash_dev_ktype);

	if (bcache_device_init(d, block_bytes(c->cache), u->sectors,
			NULL, &bcache_flash_ops))
		goto err;

	bcache_device_attach(d, c, u - c->uuids);
	bch_sectors_dirty_init(d);
	bch_flash_dev_request_init(d);
	err = add_disk(d->disk);
	if (err)
		goto err;

	err = kobject_add(&d->kobj, &disk_to_dev(d->disk)->kobj, "bcache");
	if (err)
		goto err;

	bcache_device_link(d, c, "volume");

	if (bch_has_feature_obso_large_bucket(&c->cache->sb)) {
		pr_err("The obsoleted large bucket layout is unsupported, set the bcache device into read-only\n");
		pr_err("Please update to the latest bcache-tools to create the cache device\n");
		set_disk_ro(d->disk, 1);
	}

	return 0;
err:
	kobject_put(&d->kobj);
err_ret:
	return err;
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
		pr_err("Can't create volume, no room for UUID\n");
		return -EINVAL;
	}

	get_random_bytes(u->uuid, 16);
	memset(u->label, 0, 32);
	u->first_reg = u->last_reg = cpu_to_le32((u32)ktime_get_real_seconds());

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

	pr_err("stop %s: too many IO errors on backing device %pg\n",
	       dc->disk.disk->disk_name, dc->bdev);

	bcache_device_stop(&dc->disk);
	return true;
}

/* Cache set */

__printf(2, 3)
bool bch_cache_set_error(struct cache_set *c, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (c->on_error != ON_ERROR_PANIC &&
	    test_bit(CACHE_SET_STOPPING, &c->flags))
		return false;

	if (test_and_set_bit(CACHE_SET_IO_DISABLE, &c->flags))
		pr_info("CACHE_SET_IO_DISABLE already set\n");

	/*
	 * XXX: we can be called from atomic context
	 * acquire_console_sem();
	 */

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	pr_err("error on %pU: %pV, disabling caching\n",
	       c->set_uuid, &vaf);

	va_end(args);

	if (c->on_error == ON_ERROR_PANIC)
		panic("panic forced after error\n");

	bch_cache_set_unregister(c);
	return true;
}

/* When c->kobj released */
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

	debugfs_remove(c->debug);

	bch_open_buckets_free(c);
	bch_btree_cache_free(c);
	bch_journal_free(c);

	mutex_lock(&bch_register_lock);
	bch_bset_sort_state_free(&c->sort);
	free_pages((unsigned long) c->uuids, ilog2(meta_bucket_pages(&c->cache->sb)));

	ca = c->cache;
	if (ca) {
		ca->set = NULL;
		c->cache = NULL;
		kobject_put(&ca->kobj);
	}


	if (c->moving_gc_wq)
		destroy_workqueue(c->moving_gc_wq);
	bioset_exit(&c->bio_split);
	mempool_exit(&c->fill_iter);
	mempool_exit(&c->bio_meta);
	mempool_exit(&c->search);
	kfree(c->devices);

	list_del(&c->list);
	mutex_unlock(&bch_register_lock);

	pr_info("Cache set %pU unregistered\n", c->set_uuid);
	wake_up(&unregister_wait);

	closure_debug_destroy(&c->cl);
	kobject_put(&c->kobj);
}

static void cache_set_flush(struct closure *cl)
{
	struct cache_set *c = container_of(cl, struct cache_set, caching);
	struct cache *ca = c->cache;
	struct btree *b;

	bch_cache_accounting_destroy(&c->accounting);

	kobject_put(&c->internal);
	kobject_del(&c->kobj);

	if (!IS_ERR_OR_NULL(c->gc_thread))
		kthread_stop(c->gc_thread);

	if (!IS_ERR(c->root))
		list_add(&c->root->list, &c->btree_cache);

	/*
	 * Avoid flushing cached nodes if cache set is retiring
	 * due to too many I/O errors detected.
	 */
	if (!test_bit(CACHE_SET_IO_DISABLE, &c->flags))
		list_for_each_entry(b, &c->btree_cache, list) {
			mutex_lock(&b->write_lock);
			if (btree_node_dirty(b))
				__bch_btree_node_write(b, NULL);
			mutex_unlock(&b->write_lock);
		}

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
		pr_warn("stop_when_cache_set_failed of %s is \"always\", stop it for failed cache set %pU.\n",
			d->disk->disk_name, c->set_uuid);
		bcache_device_stop(d);
	} else if (atomic_read(&dc->has_dirty)) {
		/*
		 * dc->stop_when_cache_set_failed == BCH_CACHED_STOP_AUTO
		 * and dc->has_dirty == 1
		 */
		pr_warn("stop_when_cache_set_failed of %s is \"auto\" and cache is dirty, stop it to avoid potential data corruption.\n",
			d->disk->disk_name);
		/*
		 * There might be a small time gap that cache set is
		 * released but bcache device is not. Inside this time
		 * gap, regular I/O requests will directly go into
		 * backing device as no cache set attached to. This
		 * behavior may also introduce potential inconsistence
		 * data in writeback mode while cache is dirty.
		 * Therefore before calling bcache_device_stop() due
		 * to a broken cache device, dc->io_disable should be
		 * explicitly set to true.
		 */
		dc->io_disable = true;
		/* make others know io_disable is true earlier */
		smp_mb();
		bcache_device_stop(d);
	} else {
		/*
		 * dc->stop_when_cache_set_failed == BCH_CACHED_STOP_AUTO
		 * and dc->has_dirty == 0
		 */
		pr_warn("stop_when_cache_set_failed of %s is \"auto\" and cache is clean, keep it alive.\n",
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
		/* closure_fn set to __cache_set_unregister() */
		closure_queue(&c->caching);
}

void bch_cache_set_unregister(struct cache_set *c)
{
	set_bit(CACHE_SET_UNREGISTERING, &c->flags);
	bch_cache_set_stop(c);
}

#define alloc_meta_bucket_pages(gfp, sb)		\
	((void *) __get_free_pages(__GFP_ZERO|__GFP_COMP|gfp, ilog2(meta_bucket_pages(sb))))

struct cache_set *bch_cache_set_alloc(struct cache_sb *sb)
{
	int iter_size;
	struct cache *ca = container_of(sb, struct cache, sb);
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

	memcpy(c->set_uuid, sb->set_uuid, 16);

	c->cache		= ca;
	c->cache->set		= c;
	c->bucket_bits		= ilog2(sb->bucket_size);
	c->block_bits		= ilog2(sb->block_size);
	c->nr_uuids		= meta_bucket_bytes(sb) / sizeof(struct uuid_entry);
	c->devices_max_used	= 0;
	atomic_set(&c->attached_dev_nr, 0);
	c->btree_pages		= meta_bucket_pages(sb);
	if (c->btree_pages > BTREE_MAX_PAGES)
		c->btree_pages = max_t(int, c->btree_pages / 4,
				       BTREE_MAX_PAGES);

	sema_init(&c->sb_write_mutex, 1);
	mutex_init(&c->bucket_lock);
	init_waitqueue_head(&c->btree_cache_wait);
	spin_lock_init(&c->btree_cannibalize_lock);
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

	iter_size = ((meta_bucket_pages(sb) * PAGE_SECTORS) / sb->block_size + 1) *
		sizeof(struct btree_iter_set);

	c->devices = kcalloc(c->nr_uuids, sizeof(void *), GFP_KERNEL);
	if (!c->devices)
		goto err;

	if (mempool_init_slab_pool(&c->search, 32, bch_search_cache))
		goto err;

	if (mempool_init_kmalloc_pool(&c->bio_meta, 2,
			sizeof(struct bbio) +
			sizeof(struct bio_vec) * meta_bucket_pages(sb)))
		goto err;

	if (mempool_init_kmalloc_pool(&c->fill_iter, 1, iter_size))
		goto err;

	if (bioset_init(&c->bio_split, 4, offsetof(struct bbio, bio),
			BIOSET_NEED_RESCUER))
		goto err;

	c->uuids = alloc_meta_bucket_pages(GFP_KERNEL, sb);
	if (!c->uuids)
		goto err;

	c->moving_gc_wq = alloc_workqueue("bcache_gc", WQ_MEM_RECLAIM, 0);
	if (!c->moving_gc_wq)
		goto err;

	if (bch_journal_alloc(c))
		goto err;

	if (bch_btree_cache_alloc(c))
		goto err;

	if (bch_open_buckets_alloc(c))
		goto err;

	if (bch_bset_sort_state_init(&c->sort, ilog2(c->btree_pages)))
		goto err;

	c->congested_read_threshold_us	= 2000;
	c->congested_write_threshold_us	= 20000;
	c->error_limit	= DEFAULT_IO_ERROR_LIMIT;
	c->idle_max_writeback_rate_enabled = 1;
	WARN_ON(test_and_clear_bit(CACHE_SET_IO_DISABLE, &c->flags));

	return c;
err:
	bch_cache_set_unregister(c);
	return NULL;
}

static int run_cache_set(struct cache_set *c)
{
	const char *err = "cannot allocate memory";
	struct cached_dev *dc, *t;
	struct cache *ca = c->cache;
	struct closure cl;
	LIST_HEAD(journal);
	struct journal_replay *l;

	closure_init_stack(&cl);

	c->nbuckets = ca->sb.nbuckets;
	set_gc_sectors(c);

	if (CACHE_SYNC(&c->cache->sb)) {
		struct bkey *k;
		struct jset *j;

		err = "cannot allocate memory for journal";
		if (bch_journal_read(c, &journal))
			goto err;

		pr_debug("btree_journal_read() done\n");

		err = "no journal entries found";
		if (list_empty(&journal))
			goto err;

		j = &list_entry(journal.prev, struct journal_replay, list)->j;

		err = "IO error reading priorities";
		if (prio_read(ca, j->prio_bucket[ca->sb.nr_this_dev]))
			goto err;

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
		c->root = bch_btree_node_get(c, NULL, k,
					     j->btree_level,
					     true, NULL);
		if (IS_ERR(c->root))
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
		pr_debug("btree_check() done\n");

		/*
		 * bcache_journal_next() can't happen sooner, or
		 * btree_gc_finish() will give spurious errors about last_gc >
		 * gc_gen - this is a hack but oh well.
		 */
		bch_journal_next(&c->journal);

		err = "error starting allocator thread";
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

		err = "bcache: replay journal failed";
		if (bch_journal_replay(c, &journal))
			goto err;
	} else {
		unsigned int j;

		pr_notice("invalidating existing data\n");
		ca->sb.keys = clamp_t(int, ca->sb.nbuckets >> 7,
					2, SB_JOURNAL_BUCKETS);

		for (j = 0; j < ca->sb.keys; j++)
			ca->sb.d[j] = ca->sb.first_bucket + j;

		bch_initial_gc_finish(c);

		err = "error starting allocator thread";
		if (bch_cache_allocator_start(ca))
			goto err;

		mutex_lock(&c->bucket_lock);
		bch_prio_write(ca, true);
		mutex_unlock(&c->bucket_lock);

		err = "cannot allocate new UUID bucket";
		if (__uuid_write(c))
			goto err;

		err = "cannot allocate new btree root";
		c->root = __bch_btree_node_alloc(c, NULL, 0, true, NULL);
		if (IS_ERR(c->root))
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
		SET_CACHE_SYNC(&c->cache->sb, true);

		bch_journal_next(&c->journal);
		bch_journal_meta(c, &cl);
	}

	err = "error starting gc thread";
	if (bch_gc_thread_start(c))
		goto err;

	closure_sync(&cl);
	c->cache->sb.last_mount = (u32)ktime_get_real_seconds();
	bcache_write_super(c);

	if (bch_has_feature_obso_large_bucket(&c->cache->sb))
		pr_err("Detect obsoleted large bucket layout, all attached bcache device will be read-only\n");

	list_for_each_entry_safe(dc, t, &uncached_devices, list)
		bch_cached_dev_attach(dc, c, NULL);

	flash_devs_run(c);

	bch_journal_space_reserve(&c->journal);
	set_bit(CACHE_SET_RUNNING, &c->flags);
	return 0;
err:
	while (!list_empty(&journal)) {
		l = list_first_entry(&journal, struct journal_replay, list);
		list_del(&l->list);
		kfree(l);
	}

	closure_sync(&cl);

	bch_cache_set_error(c, "%s", err);

	return -EIO;
}

static const char *register_cache_set(struct cache *ca)
{
	char buf[12];
	const char *err = "cannot allocate memory";
	struct cache_set *c;

	list_for_each_entry(c, &bch_cache_sets, list)
		if (!memcmp(c->set_uuid, ca->sb.set_uuid, 16)) {
			if (c->cache)
				return "duplicate cache set member";

			goto found;
		}

	c = bch_cache_set_alloc(&ca->sb);
	if (!c)
		return err;

	err = "error creating kobject";
	if (kobject_add(&c->kobj, bcache_kobj, "%pU", c->set_uuid) ||
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

	kobject_get(&ca->kobj);
	ca->set = c;
	ca->set->cache = ca;

	err = "failed to run cache set";
	if (run_cache_set(c) < 0)
		goto err;

	return NULL;
err:
	bch_cache_set_unregister(c);
	return err;
}

/* Cache device */

/* When ca->kobj released */
void bch_cache_release(struct kobject *kobj)
{
	struct cache *ca = container_of(kobj, struct cache, kobj);
	unsigned int i;

	if (ca->set) {
		BUG_ON(ca->set->cache != ca);
		ca->set->cache = NULL;
	}

	free_pages((unsigned long) ca->disk_buckets, ilog2(meta_bucket_pages(&ca->sb)));
	kfree(ca->prio_buckets);
	vfree(ca->buckets);

	free_heap(&ca->heap);
	free_fifo(&ca->free_inc);

	for (i = 0; i < RESERVE_NR; i++)
		free_fifo(&ca->free[i]);

	if (ca->sb_disk)
		put_page(virt_to_page(ca->sb_disk));

	if (ca->bdev_handle)
		bdev_release(ca->bdev_handle);

	kfree(ca);
	module_put(THIS_MODULE);
}

static int cache_alloc(struct cache *ca)
{
	size_t free;
	size_t btree_buckets;
	struct bucket *b;
	int ret = -ENOMEM;
	const char *err = NULL;

	__module_get(THIS_MODULE);
	kobject_init(&ca->kobj, &bch_cache_ktype);

	bio_init(&ca->journal.bio, NULL, ca->journal.bio.bi_inline_vecs, 8, 0);

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
	if (!free) {
		ret = -EPERM;
		err = "ca->sb.nbuckets is too small";
		goto err_free;
	}

	if (!init_fifo(&ca->free[RESERVE_BTREE], btree_buckets,
						GFP_KERNEL)) {
		err = "ca->free[RESERVE_BTREE] alloc failed";
		goto err_btree_alloc;
	}

	if (!init_fifo_exact(&ca->free[RESERVE_PRIO], prio_buckets(ca),
							GFP_KERNEL)) {
		err = "ca->free[RESERVE_PRIO] alloc failed";
		goto err_prio_alloc;
	}

	if (!init_fifo(&ca->free[RESERVE_MOVINGGC], free, GFP_KERNEL)) {
		err = "ca->free[RESERVE_MOVINGGC] alloc failed";
		goto err_movinggc_alloc;
	}

	if (!init_fifo(&ca->free[RESERVE_NONE], free, GFP_KERNEL)) {
		err = "ca->free[RESERVE_NONE] alloc failed";
		goto err_none_alloc;
	}

	if (!init_fifo(&ca->free_inc, free << 2, GFP_KERNEL)) {
		err = "ca->free_inc alloc failed";
		goto err_free_inc_alloc;
	}

	if (!init_heap(&ca->heap, free << 3, GFP_KERNEL)) {
		err = "ca->heap alloc failed";
		goto err_heap_alloc;
	}

	ca->buckets = vzalloc(array_size(sizeof(struct bucket),
			      ca->sb.nbuckets));
	if (!ca->buckets) {
		err = "ca->buckets alloc failed";
		goto err_buckets_alloc;
	}

	ca->prio_buckets = kzalloc(array3_size(sizeof(uint64_t),
				   prio_buckets(ca), 2),
				   GFP_KERNEL);
	if (!ca->prio_buckets) {
		err = "ca->prio_buckets alloc failed";
		goto err_prio_buckets_alloc;
	}

	ca->disk_buckets = alloc_meta_bucket_pages(GFP_KERNEL, &ca->sb);
	if (!ca->disk_buckets) {
		err = "ca->disk_buckets alloc failed";
		goto err_disk_buckets_alloc;
	}

	ca->prio_last_buckets = ca->prio_buckets + prio_buckets(ca);

	for_each_bucket(b, ca)
		atomic_set(&b->pin, 0);
	return 0;

err_disk_buckets_alloc:
	kfree(ca->prio_buckets);
err_prio_buckets_alloc:
	vfree(ca->buckets);
err_buckets_alloc:
	free_heap(&ca->heap);
err_heap_alloc:
	free_fifo(&ca->free_inc);
err_free_inc_alloc:
	free_fifo(&ca->free[RESERVE_NONE]);
err_none_alloc:
	free_fifo(&ca->free[RESERVE_MOVINGGC]);
err_movinggc_alloc:
	free_fifo(&ca->free[RESERVE_PRIO]);
err_prio_alloc:
	free_fifo(&ca->free[RESERVE_BTREE]);
err_btree_alloc:
err_free:
	module_put(THIS_MODULE);
	if (err)
		pr_notice("error %pg: %s\n", ca->bdev, err);
	return ret;
}

static int register_cache(struct cache_sb *sb, struct cache_sb_disk *sb_disk,
				struct bdev_handle *bdev_handle,
				struct cache *ca)
{
	const char *err = NULL; /* must be set for any error case */
	int ret = 0;

	memcpy(&ca->sb, sb, sizeof(struct cache_sb));
	ca->bdev_handle = bdev_handle;
	ca->bdev = bdev_handle->bdev;
	ca->sb_disk = sb_disk;

	if (bdev_max_discard_sectors((bdev_handle->bdev)))
		ca->discard = CACHE_DISCARD(&ca->sb);

	ret = cache_alloc(ca);
	if (ret != 0) {
		if (ret == -ENOMEM)
			err = "cache_alloc(): -ENOMEM";
		else if (ret == -EPERM)
			err = "cache_alloc(): cache device is too small";
		else
			err = "cache_alloc(): unknown error";
		pr_notice("error %pg: %s\n", bdev_handle->bdev, err);
		/*
		 * If we failed here, it means ca->kobj is not initialized yet,
		 * kobject_put() won't be called and there is no chance to
		 * call bdev_release() to bdev in bch_cache_release(). So
		 * we explicitly call bdev_release() here.
		 */
		bdev_release(bdev_handle);
		return ret;
	}

	if (kobject_add(&ca->kobj, bdev_kobj(bdev_handle->bdev), "bcache")) {
		pr_notice("error %pg: error calling kobject_add\n",
			  bdev_handle->bdev);
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

	pr_info("registered cache device %pg\n", ca->bdev_handle->bdev);

out:
	kobject_put(&ca->kobj);
	return ret;
}

/* Global interfaces/init */

static ssize_t register_bcache(struct kobject *k, struct kobj_attribute *attr,
			       const char *buffer, size_t size);
static ssize_t bch_pending_bdevs_cleanup(struct kobject *k,
					 struct kobj_attribute *attr,
					 const char *buffer, size_t size);

kobj_attribute_write(register,		register_bcache);
kobj_attribute_write(register_quiet,	register_bcache);
kobj_attribute_write(pendings_cleanup,	bch_pending_bdevs_cleanup);

static bool bch_is_open_backing(dev_t dev)
{
	struct cache_set *c, *tc;
	struct cached_dev *dc, *t;

	list_for_each_entry_safe(c, tc, &bch_cache_sets, list)
		list_for_each_entry_safe(dc, t, &c->cached_devs, list)
			if (dc->bdev->bd_dev == dev)
				return true;
	list_for_each_entry_safe(dc, t, &uncached_devices, list)
		if (dc->bdev->bd_dev == dev)
			return true;
	return false;
}

static bool bch_is_open_cache(dev_t dev)
{
	struct cache_set *c, *tc;

	list_for_each_entry_safe(c, tc, &bch_cache_sets, list) {
		struct cache *ca = c->cache;

		if (ca->bdev->bd_dev == dev)
			return true;
	}

	return false;
}

static bool bch_is_open(dev_t dev)
{
	return bch_is_open_cache(dev) || bch_is_open_backing(dev);
}

struct async_reg_args {
	struct delayed_work reg_work;
	char *path;
	struct cache_sb *sb;
	struct cache_sb_disk *sb_disk;
	struct bdev_handle *bdev_handle;
	void *holder;
};

static void register_bdev_worker(struct work_struct *work)
{
	int fail = false;
	struct async_reg_args *args =
		container_of(work, struct async_reg_args, reg_work.work);

	mutex_lock(&bch_register_lock);
	if (register_bdev(args->sb, args->sb_disk, args->bdev_handle,
			  args->holder) < 0)
		fail = true;
	mutex_unlock(&bch_register_lock);

	if (fail)
		pr_info("error %s: fail to register backing device\n",
			args->path);
	kfree(args->sb);
	kfree(args->path);
	kfree(args);
	module_put(THIS_MODULE);
}

static void register_cache_worker(struct work_struct *work)
{
	int fail = false;
	struct async_reg_args *args =
		container_of(work, struct async_reg_args, reg_work.work);

	/* blkdev_put() will be called in bch_cache_release() */
	if (register_cache(args->sb, args->sb_disk, args->bdev_handle,
			   args->holder))
		fail = true;

	if (fail)
		pr_info("error %s: fail to register cache device\n",
			args->path);
	kfree(args->sb);
	kfree(args->path);
	kfree(args);
	module_put(THIS_MODULE);
}

static void register_device_async(struct async_reg_args *args)
{
	if (SB_IS_BDEV(args->sb))
		INIT_DELAYED_WORK(&args->reg_work, register_bdev_worker);
	else
		INIT_DELAYED_WORK(&args->reg_work, register_cache_worker);

	/* 10 jiffies is enough for a delay */
	queue_delayed_work(system_wq, &args->reg_work, 10);
}

static void *alloc_holder_object(struct cache_sb *sb)
{
	if (SB_IS_BDEV(sb))
		return kzalloc(sizeof(struct cached_dev), GFP_KERNEL);
	return kzalloc(sizeof(struct cache), GFP_KERNEL);
}

static ssize_t register_bcache(struct kobject *k, struct kobj_attribute *attr,
			       const char *buffer, size_t size)
{
	const char *err;
	char *path = NULL;
	struct cache_sb *sb;
	struct cache_sb_disk *sb_disk;
	struct bdev_handle *bdev_handle, *bdev_handle2;
	void *holder = NULL;
	ssize_t ret;
	bool async_registration = false;
	bool quiet = false;

#ifdef CONFIG_BCACHE_ASYNC_REGISTRATION
	async_registration = true;
#endif

	ret = -EBUSY;
	err = "failed to reference bcache module";
	if (!try_module_get(THIS_MODULE))
		goto out;

	/* For latest state of bcache_is_reboot */
	smp_mb();
	err = "bcache is in reboot";
	if (bcache_is_reboot)
		goto out_module_put;

	ret = -ENOMEM;
	err = "cannot allocate memory";
	path = kstrndup(buffer, size, GFP_KERNEL);
	if (!path)
		goto out_module_put;

	sb = kmalloc(sizeof(struct cache_sb), GFP_KERNEL);
	if (!sb)
		goto out_free_path;

	ret = -EINVAL;
	err = "failed to open device";
	bdev_handle = bdev_open_by_path(strim(path), BLK_OPEN_READ, NULL, NULL);
	if (IS_ERR(bdev_handle))
		goto out_free_sb;

	err = "failed to set blocksize";
	if (set_blocksize(bdev_handle->bdev, 4096))
		goto out_blkdev_put;

	err = read_super(sb, bdev_handle->bdev, &sb_disk);
	if (err)
		goto out_blkdev_put;

	holder = alloc_holder_object(sb);
	if (!holder) {
		ret = -ENOMEM;
		err = "cannot allocate memory";
		goto out_put_sb_page;
	}

	/* Now reopen in exclusive mode with proper holder */
	bdev_handle2 = bdev_open_by_dev(bdev_handle->bdev->bd_dev,
			BLK_OPEN_READ | BLK_OPEN_WRITE, holder, NULL);
	bdev_release(bdev_handle);
	bdev_handle = bdev_handle2;
	if (IS_ERR(bdev_handle)) {
		ret = PTR_ERR(bdev_handle);
		bdev_handle = NULL;
		if (ret == -EBUSY) {
			dev_t dev;

			mutex_lock(&bch_register_lock);
			if (lookup_bdev(strim(path), &dev) == 0 &&
			    bch_is_open(dev))
				err = "device already registered";
			else
				err = "device busy";
			mutex_unlock(&bch_register_lock);
			if (attr == &ksysfs_register_quiet) {
				quiet = true;
				ret = size;
			}
		}
		goto out_free_holder;
	}

	err = "failed to register device";

	if (async_registration) {
		/* register in asynchronous way */
		struct async_reg_args *args =
			kzalloc(sizeof(struct async_reg_args), GFP_KERNEL);

		if (!args) {
			ret = -ENOMEM;
			err = "cannot allocate memory";
			goto out_free_holder;
		}

		args->path	= path;
		args->sb	= sb;
		args->sb_disk	= sb_disk;
		args->bdev_handle	= bdev_handle;
		args->holder	= holder;
		register_device_async(args);
		/* No wait and returns to user space */
		goto async_done;
	}

	if (SB_IS_BDEV(sb)) {
		mutex_lock(&bch_register_lock);
		ret = register_bdev(sb, sb_disk, bdev_handle, holder);
		mutex_unlock(&bch_register_lock);
		/* blkdev_put() will be called in cached_dev_free() */
		if (ret < 0)
			goto out_free_sb;
	} else {
		/* blkdev_put() will be called in bch_cache_release() */
		ret = register_cache(sb, sb_disk, bdev_handle, holder);
		if (ret)
			goto out_free_sb;
	}

	kfree(sb);
	kfree(path);
	module_put(THIS_MODULE);
async_done:
	return size;

out_free_holder:
	kfree(holder);
out_put_sb_page:
	put_page(virt_to_page(sb_disk));
out_blkdev_put:
	if (bdev_handle)
		bdev_release(bdev_handle);
out_free_sb:
	kfree(sb);
out_free_path:
	kfree(path);
	path = NULL;
out_module_put:
	module_put(THIS_MODULE);
out:
	if (!quiet)
		pr_info("error %s: %s\n", path?path:"", err);
	return ret;
}


struct pdev {
	struct list_head list;
	struct cached_dev *dc;
};

static ssize_t bch_pending_bdevs_cleanup(struct kobject *k,
					 struct kobj_attribute *attr,
					 const char *buffer,
					 size_t size)
{
	LIST_HEAD(pending_devs);
	ssize_t ret = size;
	struct cached_dev *dc, *tdc;
	struct pdev *pdev, *tpdev;
	struct cache_set *c, *tc;

	mutex_lock(&bch_register_lock);
	list_for_each_entry_safe(dc, tdc, &uncached_devices, list) {
		pdev = kmalloc(sizeof(struct pdev), GFP_KERNEL);
		if (!pdev)
			break;
		pdev->dc = dc;
		list_add(&pdev->list, &pending_devs);
	}

	list_for_each_entry_safe(pdev, tpdev, &pending_devs, list) {
		char *pdev_set_uuid = pdev->dc->sb.set_uuid;
		list_for_each_entry_safe(c, tc, &bch_cache_sets, list) {
			char *set_uuid = c->set_uuid;

			if (!memcmp(pdev_set_uuid, set_uuid, 16)) {
				list_del(&pdev->list);
				kfree(pdev);
				break;
			}
		}
	}
	mutex_unlock(&bch_register_lock);

	list_for_each_entry_safe(pdev, tpdev, &pending_devs, list) {
		pr_info("delete pdev %p\n", pdev);
		list_del(&pdev->list);
		bcache_device_stop(&pdev->dc->disk);
		kfree(pdev);
	}

	return ret;
}

static int bcache_reboot(struct notifier_block *n, unsigned long code, void *x)
{
	if (bcache_is_reboot)
		return NOTIFY_DONE;

	if (code == SYS_DOWN ||
	    code == SYS_HALT ||
	    code == SYS_POWER_OFF) {
		DEFINE_WAIT(wait);
		unsigned long start = jiffies;
		bool stopped = false;

		struct cache_set *c, *tc;
		struct cached_dev *dc, *tdc;

		mutex_lock(&bch_register_lock);

		if (bcache_is_reboot)
			goto out;

		/* New registration is rejected since now */
		bcache_is_reboot = true;
		/*
		 * Make registering caller (if there is) on other CPU
		 * core know bcache_is_reboot set to true earlier
		 */
		smp_mb();

		if (list_empty(&bch_cache_sets) &&
		    list_empty(&uncached_devices))
			goto out;

		mutex_unlock(&bch_register_lock);

		pr_info("Stopping all devices:\n");

		/*
		 * The reason bch_register_lock is not held to call
		 * bch_cache_set_stop() and bcache_device_stop() is to
		 * avoid potential deadlock during reboot, because cache
		 * set or bcache device stopping process will acquire
		 * bch_register_lock too.
		 *
		 * We are safe here because bcache_is_reboot sets to
		 * true already, register_bcache() will reject new
		 * registration now. bcache_is_reboot also makes sure
		 * bcache_reboot() won't be re-entered on by other thread,
		 * so there is no race in following list iteration by
		 * list_for_each_entry_safe().
		 */
		list_for_each_entry_safe(c, tc, &bch_cache_sets, list)
			bch_cache_set_stop(c);

		list_for_each_entry_safe(dc, tdc, &uncached_devices, list)
			bcache_device_stop(&dc->disk);


		/*
		 * Give an early chance for other kthreads and
		 * kworkers to stop themselves
		 */
		schedule();

		/* What's a condition variable? */
		while (1) {
			long timeout = start + 10 * HZ - jiffies;

			mutex_lock(&bch_register_lock);
			stopped = list_empty(&bch_cache_sets) &&
				list_empty(&uncached_devices);

			if (timeout < 0 || stopped)
				break;

			prepare_to_wait(&unregister_wait, &wait,
					TASK_UNINTERRUPTIBLE);

			mutex_unlock(&bch_register_lock);
			schedule_timeout(timeout);
		}

		finish_wait(&unregister_wait, &wait);

		if (stopped)
			pr_info("All devices stopped\n");
		else
			pr_notice("Timeout waiting for devices to be closed\n");
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
	if (bch_journal_wq)
		destroy_workqueue(bch_journal_wq);
	if (bch_flush_wq)
		destroy_workqueue(bch_flush_wq);
	bch_btree_exit();

	if (bcache_major)
		unregister_blkdev(bcache_major, "bcache");
	unregister_reboot_notifier(&reboot);
	mutex_destroy(&bch_register_lock);
}

/* Check and fixup module parameters */
static void check_module_parameters(void)
{
	if (bch_cutoff_writeback_sync == 0)
		bch_cutoff_writeback_sync = CUTOFF_WRITEBACK_SYNC;
	else if (bch_cutoff_writeback_sync > CUTOFF_WRITEBACK_SYNC_MAX) {
		pr_warn("set bch_cutoff_writeback_sync (%u) to max value %u\n",
			bch_cutoff_writeback_sync, CUTOFF_WRITEBACK_SYNC_MAX);
		bch_cutoff_writeback_sync = CUTOFF_WRITEBACK_SYNC_MAX;
	}

	if (bch_cutoff_writeback == 0)
		bch_cutoff_writeback = CUTOFF_WRITEBACK;
	else if (bch_cutoff_writeback > CUTOFF_WRITEBACK_MAX) {
		pr_warn("set bch_cutoff_writeback (%u) to max value %u\n",
			bch_cutoff_writeback, CUTOFF_WRITEBACK_MAX);
		bch_cutoff_writeback = CUTOFF_WRITEBACK_MAX;
	}

	if (bch_cutoff_writeback > bch_cutoff_writeback_sync) {
		pr_warn("set bch_cutoff_writeback (%u) to %u\n",
			bch_cutoff_writeback, bch_cutoff_writeback_sync);
		bch_cutoff_writeback = bch_cutoff_writeback_sync;
	}
}

static int __init bcache_init(void)
{
	static const struct attribute *files[] = {
		&ksysfs_register.attr,
		&ksysfs_register_quiet.attr,
		&ksysfs_pendings_cleanup.attr,
		NULL
	};

	check_module_parameters();

	mutex_init(&bch_register_lock);
	init_waitqueue_head(&unregister_wait);
	register_reboot_notifier(&reboot);

	bcache_major = register_blkdev(0, "bcache");
	if (bcache_major < 0) {
		unregister_reboot_notifier(&reboot);
		mutex_destroy(&bch_register_lock);
		return bcache_major;
	}

	if (bch_btree_init())
		goto err;

	bcache_wq = alloc_workqueue("bcache", WQ_MEM_RECLAIM, 0);
	if (!bcache_wq)
		goto err;

	/*
	 * Let's not make this `WQ_MEM_RECLAIM` for the following reasons:
	 *
	 * 1. It used `system_wq` before which also does no memory reclaim.
	 * 2. With `WQ_MEM_RECLAIM` desktop stalls, increased boot times, and
	 *    reduced throughput can be observed.
	 *
	 * We still want to user our own queue to not congest the `system_wq`.
	 */
	bch_flush_wq = alloc_workqueue("bch_flush", 0, 0);
	if (!bch_flush_wq)
		goto err;

	bch_journal_wq = alloc_workqueue("bch_journal", WQ_MEM_RECLAIM, 0);
	if (!bch_journal_wq)
		goto err;

	bcache_kobj = kobject_create_and_add("bcache", fs_kobj);
	if (!bcache_kobj)
		goto err;

	if (bch_request_init() ||
	    sysfs_create_files(bcache_kobj, files))
		goto err;

	bch_debug_init();

	bcache_is_reboot = false;

	return 0;
err:
	bcache_exit();
	return -ENOMEM;
}

/*
 * Module hooks
 */
module_exit(bcache_exit);
module_init(bcache_init);

module_param(bch_cutoff_writeback, uint, 0);
MODULE_PARM_DESC(bch_cutoff_writeback, "threshold to cutoff writeback");

module_param(bch_cutoff_writeback_sync, uint, 0);
MODULE_PARM_DESC(bch_cutoff_writeback_sync, "hard threshold to cutoff writeback");

MODULE_DESCRIPTION("Bcache: a Linux block layer cache");
MODULE_AUTHOR("Kent Overstreet <kent.overstreet@gmail.com>");
MODULE_LICENSE("GPL");
