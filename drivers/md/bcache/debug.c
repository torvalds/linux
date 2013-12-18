/*
 * Assorted bcache debug code
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcache.h"
#include "btree.h"
#include "debug.h"

#include <linux/console.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/seq_file.h>

static struct dentry *debug;

const char *bch_ptr_status(struct cache_set *c, const struct bkey *k)
{
	unsigned i;

	for (i = 0; i < KEY_PTRS(k); i++)
		if (ptr_available(c, k, i)) {
			struct cache *ca = PTR_CACHE(c, k, i);
			size_t bucket = PTR_BUCKET_NR(c, k, i);
			size_t r = bucket_remainder(c, PTR_OFFSET(k, i));

			if (KEY_SIZE(k) + r > c->sb.bucket_size)
				return "bad, length too big";
			if (bucket <  ca->sb.first_bucket)
				return "bad, short offset";
			if (bucket >= ca->sb.nbuckets)
				return "bad, offset past end of device";
			if (ptr_stale(c, k, i))
				return "stale";
		}

	if (!bkey_cmp(k, &ZERO_KEY))
		return "bad, null key";
	if (!KEY_PTRS(k))
		return "bad, no pointers";
	if (!KEY_SIZE(k))
		return "zeroed key";
	return "";
}

int bch_bkey_to_text(char *buf, size_t size, const struct bkey *k)
{
	unsigned i = 0;
	char *out = buf, *end = buf + size;

#define p(...)	(out += scnprintf(out, end - out, __VA_ARGS__))

	p("%llu:%llu len %llu -> [", KEY_INODE(k), KEY_START(k), KEY_SIZE(k));

	for (i = 0; i < KEY_PTRS(k); i++) {
		if (i)
			p(", ");

		if (PTR_DEV(k, i) == PTR_CHECK_DEV)
			p("check dev");
		else
			p("%llu:%llu gen %llu", PTR_DEV(k, i),
			  PTR_OFFSET(k, i), PTR_GEN(k, i));
	}

	p("]");

	if (KEY_DIRTY(k))
		p(" dirty");
	if (KEY_CSUM(k))
		p(" cs%llu %llx", KEY_CSUM(k), k->ptr[1]);
#undef p
	return out - buf;
}

#ifdef CONFIG_BCACHE_DEBUG

static void dump_bset(struct btree *b, struct bset *i, unsigned set)
{
	struct bkey *k, *next;
	unsigned j;
	char buf[80];

	for (k = i->start; k < end(i); k = next) {
		next = bkey_next(k);

		bch_bkey_to_text(buf, sizeof(buf), k);
		printk(KERN_ERR "b %u k %zi/%u: %s", set,
		       (uint64_t *) k - i->d, i->keys, buf);

		for (j = 0; j < KEY_PTRS(k); j++) {
			size_t n = PTR_BUCKET_NR(b->c, k, j);
			printk(" bucket %zu", n);

			if (n >= b->c->sb.first_bucket && n < b->c->sb.nbuckets)
				printk(" prio %i",
				       PTR_BUCKET(b->c, k, j)->prio);
		}

		printk(" %s\n", bch_ptr_status(b->c, k));

		if (next < end(i) &&
		    bkey_cmp(k, !b->level ? &START_KEY(next) : next) > 0)
			printk(KERN_ERR "Key skipped backwards\n");
	}
}

static void bch_dump_bucket(struct btree *b)
{
	unsigned i;

	console_lock();
	for (i = 0; i <= b->nsets; i++)
		dump_bset(b, b->sets[i].data,
			  bset_block_offset(b, b->sets[i].data));
	console_unlock();
}

#define for_each_written_bset(b, start, i)				\
	for (i = (start);						\
	     (void *) i < (void *) (start) + (KEY_SIZE(&b->key) << 9) &&\
	     i->seq == (start)->seq;					\
	     i = (void *) i + set_blocks(i, b->c) * block_bytes(b->c))

void bch_btree_verify(struct btree *b)
{
	struct btree *v = b->c->verify_data;
	struct bset *ondisk, *sorted, *inmemory;
	struct bio *bio;

	if (!b->c->verify || !b->c->verify_ondisk)
		return;

	down(&b->io_mutex);
	mutex_lock(&b->c->verify_lock);

	ondisk = b->c->verify_ondisk;
	sorted = b->c->verify_data->sets->data;
	inmemory = b->sets->data;

	bkey_copy(&v->key, &b->key);
	v->written = 0;
	v->level = b->level;

	bio = bch_bbio_alloc(b->c);
	bio->bi_bdev		= PTR_CACHE(b->c, &b->key, 0)->bdev;
	bio->bi_iter.bi_sector	= PTR_OFFSET(&b->key, 0);
	bio->bi_iter.bi_size	= KEY_SIZE(&v->key) << 9;
	bch_bio_map(bio, sorted);

	submit_bio_wait(REQ_META|READ_SYNC, bio);
	bch_bbio_free(bio, b->c);

	memcpy(ondisk, sorted, KEY_SIZE(&v->key) << 9);

	bch_btree_node_read_done(v);
	sorted = v->sets->data;

	if (inmemory->keys != sorted->keys ||
	    memcmp(inmemory->start,
		   sorted->start,
		   (void *) end(inmemory) - (void *) inmemory->start)) {
		struct bset *i;
		unsigned j;

		console_lock();

		printk(KERN_ERR "*** in memory:\n");
		dump_bset(b, inmemory, 0);

		printk(KERN_ERR "*** read back in:\n");
		dump_bset(v, sorted, 0);

		for_each_written_bset(b, ondisk, i) {
			unsigned block = ((void *) i - (void *) ondisk) /
				block_bytes(b->c);

			printk(KERN_ERR "*** on disk block %u:\n", block);
			dump_bset(b, i, block);
		}

		printk(KERN_ERR "*** block %zu not written\n",
		       ((void *) i - (void *) ondisk) / block_bytes(b->c));

		for (j = 0; j < inmemory->keys; j++)
			if (inmemory->d[j] != sorted->d[j])
				break;

		printk(KERN_ERR "b->written %u\n", b->written);

		console_unlock();
		panic("verify failed at %u\n", j);
	}

	mutex_unlock(&b->c->verify_lock);
	up(&b->io_mutex);
}

void bch_data_verify(struct cached_dev *dc, struct bio *bio)
{
	char name[BDEVNAME_SIZE];
	struct bio *check;
	struct bio_vec bv, *bv2;
	struct bvec_iter iter;
	int i;

	check = bio_clone(bio, GFP_NOIO);
	if (!check)
		return;

	if (bio_alloc_pages(check, GFP_NOIO))
		goto out_put;

	submit_bio_wait(READ_SYNC, check);

	bio_for_each_segment(bv, bio, iter) {
		void *p1 = kmap_atomic(bv.bv_page);
		void *p2 = page_address(check->bi_io_vec[iter.bi_idx].bv_page);

		cache_set_err_on(memcmp(p1 + bv.bv_offset,
					p2 + bv.bv_offset,
					bv.bv_len),
				 dc->disk.c,
				 "verify failed at dev %s sector %llu",
				 bdevname(dc->bdev, name),
				 (uint64_t) bio->bi_iter.bi_sector);

		kunmap_atomic(p1);
	}

	bio_for_each_segment_all(bv2, check, i)
		__free_page(bv2->bv_page);
out_put:
	bio_put(check);
}

int __bch_count_data(struct btree *b)
{
	unsigned ret = 0;
	struct btree_iter iter;
	struct bkey *k;

	if (!b->level)
		for_each_key(b, k, &iter)
			ret += KEY_SIZE(k);
	return ret;
}

void __bch_check_keys(struct btree *b, const char *fmt, ...)
{
	va_list args;
	struct bkey *k, *p = NULL;
	struct btree_iter iter;
	const char *err;

	for_each_key(b, k, &iter) {
		if (!b->level) {
			err = "Keys out of order";
			if (p && bkey_cmp(&START_KEY(p), &START_KEY(k)) > 0)
				goto bug;

			if (bch_ptr_invalid(b, k))
				continue;

			err =  "Overlapping keys";
			if (p && bkey_cmp(p, &START_KEY(k)) > 0)
				goto bug;
		} else {
			if (bch_ptr_bad(b, k))
				continue;

			err = "Duplicate keys";
			if (p && !bkey_cmp(p, k))
				goto bug;
		}
		p = k;
	}

	err = "Key larger than btree node key";
	if (p && bkey_cmp(p, &b->key) > 0)
		goto bug;

	return;
bug:
	bch_dump_bucket(b);

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);

	panic("bcache error: %s:\n", err);
}

void bch_btree_iter_next_check(struct btree_iter *iter)
{
	struct bkey *k = iter->data->k, *next = bkey_next(k);

	if (next < iter->data->end &&
	    bkey_cmp(k, iter->b->level ? next : &START_KEY(next)) > 0) {
		bch_dump_bucket(iter->b);
		panic("Key skipped backwards\n");
	}
}

#endif

#ifdef CONFIG_DEBUG_FS

/* XXX: cache set refcounting */

struct dump_iterator {
	char			buf[PAGE_SIZE];
	size_t			bytes;
	struct cache_set	*c;
	struct keybuf		keys;
};

static bool dump_pred(struct keybuf *buf, struct bkey *k)
{
	return true;
}

static ssize_t bch_dump_read(struct file *file, char __user *buf,
			     size_t size, loff_t *ppos)
{
	struct dump_iterator *i = file->private_data;
	ssize_t ret = 0;
	char kbuf[80];

	while (size) {
		struct keybuf_key *w;
		unsigned bytes = min(i->bytes, size);

		int err = copy_to_user(buf, i->buf, bytes);
		if (err)
			return err;

		ret	 += bytes;
		buf	 += bytes;
		size	 -= bytes;
		i->bytes -= bytes;
		memmove(i->buf, i->buf + bytes, i->bytes);

		if (i->bytes)
			break;

		w = bch_keybuf_next_rescan(i->c, &i->keys, &MAX_KEY, dump_pred);
		if (!w)
			break;

		bch_bkey_to_text(kbuf, sizeof(kbuf), &w->key);
		i->bytes = snprintf(i->buf, PAGE_SIZE, "%s\n", kbuf);
		bch_keybuf_del(&i->keys, w);
	}

	return ret;
}

static int bch_dump_open(struct inode *inode, struct file *file)
{
	struct cache_set *c = inode->i_private;
	struct dump_iterator *i;

	i = kzalloc(sizeof(struct dump_iterator), GFP_KERNEL);
	if (!i)
		return -ENOMEM;

	file->private_data = i;
	i->c = c;
	bch_keybuf_init(&i->keys);
	i->keys.last_scanned = KEY(0, 0, 0);

	return 0;
}

static int bch_dump_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static const struct file_operations cache_set_debug_ops = {
	.owner		= THIS_MODULE,
	.open		= bch_dump_open,
	.read		= bch_dump_read,
	.release	= bch_dump_release
};

void bch_debug_init_cache_set(struct cache_set *c)
{
	if (!IS_ERR_OR_NULL(debug)) {
		char name[50];
		snprintf(name, 50, "bcache-%pU", c->sb.set_uuid);

		c->debug = debugfs_create_file(name, 0400, debug, c,
					       &cache_set_debug_ops);
	}
}

#endif

void bch_debug_exit(void)
{
	if (!IS_ERR_OR_NULL(debug))
		debugfs_remove_recursive(debug);
}

int __init bch_debug_init(struct kobject *kobj)
{
	int ret = 0;

	debug = debugfs_create_dir("bcache", NULL);
	return ret;
}
