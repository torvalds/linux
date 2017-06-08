/*
 * Assorted bcache debug code
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcache.h"
#include "btree.h"
#include "debug.h"
#include "extents.h"

#include <linux/console.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/seq_file.h>

static struct dentry *debug;

#ifdef CONFIG_BCACHE_DEBUG

#define for_each_written_bset(b, start, i)				\
	for (i = (start);						\
	     (void *) i < (void *) (start) + (KEY_SIZE(&b->key) << 9) &&\
	     i->seq == (start)->seq;					\
	     i = (void *) i + set_blocks(i, block_bytes(b->c)) *	\
		 block_bytes(b->c))

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
	sorted = b->c->verify_data->keys.set->data;
	inmemory = b->keys.set->data;

	bkey_copy(&v->key, &b->key);
	v->written = 0;
	v->level = b->level;
	v->keys.ops = b->keys.ops;

	bio = bch_bbio_alloc(b->c);
	bio->bi_bdev		= PTR_CACHE(b->c, &b->key, 0)->bdev;
	bio->bi_iter.bi_sector	= PTR_OFFSET(&b->key, 0);
	bio->bi_iter.bi_size	= KEY_SIZE(&v->key) << 9;
	bio->bi_opf		= REQ_OP_READ | REQ_META;
	bch_bio_map(bio, sorted);

	submit_bio_wait(bio);
	bch_bbio_free(bio, b->c);

	memcpy(ondisk, sorted, KEY_SIZE(&v->key) << 9);

	bch_btree_node_read_done(v);
	sorted = v->keys.set->data;

	if (inmemory->keys != sorted->keys ||
	    memcmp(inmemory->start,
		   sorted->start,
		   (void *) bset_bkey_last(inmemory) - (void *) inmemory->start)) {
		struct bset *i;
		unsigned j;

		console_lock();

		printk(KERN_ERR "*** in memory:\n");
		bch_dump_bset(&b->keys, inmemory, 0);

		printk(KERN_ERR "*** read back in:\n");
		bch_dump_bset(&v->keys, sorted, 0);

		for_each_written_bset(b, ondisk, i) {
			unsigned block = ((void *) i - (void *) ondisk) /
				block_bytes(b->c);

			printk(KERN_ERR "*** on disk block %u:\n", block);
			bch_dump_bset(&b->keys, i, block);
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
	struct bio_vec bv, cbv;
	struct bvec_iter iter, citer = { 0 };

	check = bio_clone(bio, GFP_NOIO);
	if (!check)
		return;
	check->bi_opf = REQ_OP_READ;

	if (bio_alloc_pages(check, GFP_NOIO))
		goto out_put;

	submit_bio_wait(check);

	citer.bi_size = UINT_MAX;
	bio_for_each_segment(bv, bio, iter) {
		void *p1 = kmap_atomic(bv.bv_page);
		void *p2;

		cbv = bio_iter_iovec(check, citer);
		p2 = page_address(cbv.bv_page);

		cache_set_err_on(memcmp(p1 + bv.bv_offset,
					p2 + bv.bv_offset,
					bv.bv_len),
				 dc->disk.c,
				 "verify failed at dev %s sector %llu",
				 bdevname(dc->bdev, name),
				 (uint64_t) bio->bi_iter.bi_sector);

		kunmap_atomic(p1);
		bio_advance_iter(check, &citer, bv.bv_len);
	}

	bio_free_pages(check);
out_put:
	bio_put(check);
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

		bch_extent_to_text(kbuf, sizeof(kbuf), &w->key);
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
