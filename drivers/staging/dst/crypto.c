/*
 * 2007+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
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
 */

#include <linux/bio.h>
#include <linux/crypto.h>
#include <linux/dst.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

/*
 * Tricky bastard, but IV can be more complex with time...
 */
static inline u64 dst_gen_iv(struct dst_trans *t)
{
	return t->gen;
}

/*
 * Crypto machinery: hash/cipher support for the given crypto controls.
 */
static struct crypto_hash *dst_init_hash(struct dst_crypto_ctl *ctl, u8 *key)
{
	int err;
	struct crypto_hash *hash;

	hash = crypto_alloc_hash(ctl->hash_algo, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(hash)) {
		err = PTR_ERR(hash);
		dprintk("%s: failed to allocate hash '%s', err: %d.\n",
				__func__, ctl->hash_algo, err);
		goto err_out_exit;
	}

	ctl->crypto_attached_size = crypto_hash_digestsize(hash);

	if (!ctl->hash_keysize)
		return hash;

	err = crypto_hash_setkey(hash, key, ctl->hash_keysize);
	if (err) {
		dprintk("%s: failed to set key for hash '%s', err: %d.\n",
				__func__, ctl->hash_algo, err);
		goto err_out_free;
	}

	return hash;

err_out_free:
	crypto_free_hash(hash);
err_out_exit:
	return ERR_PTR(err);
}

static struct crypto_ablkcipher *dst_init_cipher(struct dst_crypto_ctl *ctl, u8 *key)
{
	int err = -EINVAL;
	struct crypto_ablkcipher *cipher;

	if (!ctl->cipher_keysize)
		goto err_out_exit;

	cipher = crypto_alloc_ablkcipher(ctl->cipher_algo, 0, 0);
	if (IS_ERR(cipher)) {
		err = PTR_ERR(cipher);
		dprintk("%s: failed to allocate cipher '%s', err: %d.\n",
				__func__, ctl->cipher_algo, err);
		goto err_out_exit;
	}

	crypto_ablkcipher_clear_flags(cipher, ~0);

	err = crypto_ablkcipher_setkey(cipher, key, ctl->cipher_keysize);
	if (err) {
		dprintk("%s: failed to set key for cipher '%s', err: %d.\n",
				__func__, ctl->cipher_algo, err);
		goto err_out_free;
	}

	return cipher;

err_out_free:
	crypto_free_ablkcipher(cipher);
err_out_exit:
	return ERR_PTR(err);
}

/*
 * Crypto engine has a pool of pages to encrypt data into before sending
 * it over the network. This pool is freed/allocated here.
 */
static void dst_crypto_pages_free(struct dst_crypto_engine *e)
{
	unsigned int i;

	for (i=0; i<e->page_num; ++i)
		__free_page(e->pages[i]);
	kfree(e->pages);
}

static int dst_crypto_pages_alloc(struct dst_crypto_engine *e, int num)
{
	int i;

	e->pages = kmalloc(num * sizeof(struct page **), GFP_KERNEL);
	if (!e->pages)
		return -ENOMEM;

	for (i=0; i<num; ++i) {
		e->pages[i] = alloc_page(GFP_KERNEL);
		if (!e->pages[i])
			goto err_out_free_pages;
	}

	e->page_num = num;
	return 0;

err_out_free_pages:
	while (--i >= 0)
		__free_page(e->pages[i]);

	kfree(e->pages);
	return -ENOMEM;
}

/*
 * Initialize crypto engine for given node.
 * Setup cipher/hash, keys, pool of threads and private data.
 */
static int dst_crypto_engine_init(struct dst_crypto_engine *e, struct dst_node *n)
{
	int err;
	struct dst_crypto_ctl *ctl = &n->crypto;

	err = dst_crypto_pages_alloc(e, n->max_pages);
	if (err)
		goto err_out_exit;

	e->size = PAGE_SIZE;
	e->data = kmalloc(e->size, GFP_KERNEL);
	if (!e->data) {
		err = -ENOMEM;
		goto err_out_free_pages;
	}

	if (ctl->hash_algo[0]) {
		e->hash = dst_init_hash(ctl, n->hash_key);
		if (IS_ERR(e->hash)) {
			err = PTR_ERR(e->hash);
			e->hash = NULL;
			goto err_out_free;
		}
	}

	if (ctl->cipher_algo[0]) {
		e->cipher = dst_init_cipher(ctl, n->cipher_key);
		if (IS_ERR(e->cipher)) {
			err = PTR_ERR(e->cipher);
			e->cipher = NULL;
			goto err_out_free_hash;
		}
	}

	return 0;

err_out_free_hash:
	crypto_free_hash(e->hash);
err_out_free:
	kfree(e->data);
err_out_free_pages:
	dst_crypto_pages_free(e);
err_out_exit:
	return err;
}

static void dst_crypto_engine_exit(struct dst_crypto_engine *e)
{
	if (e->hash)
		crypto_free_hash(e->hash);
	if (e->cipher)
		crypto_free_ablkcipher(e->cipher);
	dst_crypto_pages_free(e);
	kfree(e->data);
}

/*
 * Waiting for cipher processing to be completed.
 */
struct dst_crypto_completion
{
	struct completion		complete;
	int				error;
};

static void dst_crypto_complete(struct crypto_async_request *req, int err)
{
	struct dst_crypto_completion *c = req->data;

	if (err == -EINPROGRESS)
		return;

	dprintk("%s: req: %p, err: %d.\n", __func__, req, err);
	c->error = err;
	complete(&c->complete);
}

static int dst_crypto_process(struct ablkcipher_request *req,
		struct scatterlist *sg_dst, struct scatterlist *sg_src,
		void *iv, int enc, unsigned long timeout)
{
	struct dst_crypto_completion c;
	int err;

	init_completion(&c.complete);
	c.error = -EINPROGRESS;

	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					dst_crypto_complete, &c);

	ablkcipher_request_set_crypt(req, sg_src, sg_dst, sg_src->length, iv);

	if (enc)
		err = crypto_ablkcipher_encrypt(req);
	else
		err = crypto_ablkcipher_decrypt(req);

	switch (err) {
		case -EINPROGRESS:
		case -EBUSY:
			err = wait_for_completion_interruptible_timeout(&c.complete,
					timeout);
			if (!err)
				err = -ETIMEDOUT;
			else
				err = c.error;
			break;
		default:
			break;
	}

	return err;
}

/*
 * DST uses generic iteration approach for data crypto processing.
 * Single block IO request is switched into array of scatterlists,
 * which are submitted to the crypto processing iterator.
 *
 * Input and output iterator initialization are different, since
 * in output case we can not encrypt data in-place and need a
 * temporary storage, which is then being sent to the remote peer.
 */
static int dst_trans_iter_out(struct bio *bio, struct dst_crypto_engine *e,
		int (* iterator) (struct dst_crypto_engine *e,
				  struct scatterlist *dst,
				  struct scatterlist *src))
{
	struct bio_vec *bv;
	int err, i;

	sg_init_table(e->src, bio->bi_vcnt);
	sg_init_table(e->dst, bio->bi_vcnt);

	bio_for_each_segment(bv, bio, i) {
		sg_set_page(&e->src[i], bv->bv_page, bv->bv_len, bv->bv_offset);
		sg_set_page(&e->dst[i], e->pages[i], bv->bv_len, bv->bv_offset);

		err = iterator(e, &e->dst[i], &e->src[i]);
		if (err)
			return err;
	}

	return 0;
}

static int dst_trans_iter_in(struct bio *bio, struct dst_crypto_engine *e,
		int (* iterator) (struct dst_crypto_engine *e,
				  struct scatterlist *dst,
				  struct scatterlist *src))
{
	struct bio_vec *bv;
	int err, i;

	sg_init_table(e->src, bio->bi_vcnt);
	sg_init_table(e->dst, bio->bi_vcnt);

	bio_for_each_segment(bv, bio, i) {
		sg_set_page(&e->src[i], bv->bv_page, bv->bv_len, bv->bv_offset);
		sg_set_page(&e->dst[i], bv->bv_page, bv->bv_len, bv->bv_offset);

		err = iterator(e, &e->dst[i], &e->src[i]);
		if (err)
			return err;
	}

	return 0;
}

static int dst_crypt_iterator(struct dst_crypto_engine *e,
		struct scatterlist *sg_dst, struct scatterlist *sg_src)
{
	struct ablkcipher_request *req = e->data;
	u8 iv[32];

	memset(iv, 0, sizeof(iv));

	memcpy(iv, &e->iv, sizeof(e->iv));

	return dst_crypto_process(req, sg_dst, sg_src, iv, e->enc, e->timeout);
}

static int dst_crypt(struct dst_crypto_engine *e, struct bio *bio)
{
	struct ablkcipher_request *req = e->data;

	memset(req, 0, sizeof(struct ablkcipher_request));
	ablkcipher_request_set_tfm(req, e->cipher);

	if (e->enc)
		return dst_trans_iter_out(bio, e, dst_crypt_iterator);
	else
		return dst_trans_iter_in(bio, e, dst_crypt_iterator);
}

static int dst_hash_iterator(struct dst_crypto_engine *e,
		struct scatterlist *sg_dst, struct scatterlist *sg_src)
{
	return crypto_hash_update(e->data, sg_src, sg_src->length);
}

static int dst_hash(struct dst_crypto_engine *e, struct bio *bio, void *dst)
{
	struct hash_desc *desc = e->data;
	int err;

	desc->tfm = e->hash;
	desc->flags = 0;

	err = crypto_hash_init(desc);
	if (err)
		return err;

	err = dst_trans_iter_in(bio, e, dst_hash_iterator);
	if (err)
		return err;

	err = crypto_hash_final(desc, dst);
	if (err)
		return err;

	return 0;
}

/*
 * Initialize/cleanup a crypto thread. The only thing it should
 * do is to allocate a pool of pages as temporary storage.
 * And to setup cipher and/or hash.
 */
static void *dst_crypto_thread_init(void *data)
{
	struct dst_node *n = data;
	struct dst_crypto_engine *e;
	int err = -ENOMEM;

	e = kzalloc(sizeof(struct dst_crypto_engine), GFP_KERNEL);
	if (!e)
		goto err_out_exit;
	e->src = kcalloc(2 * n->max_pages, sizeof(struct scatterlist),
			GFP_KERNEL);
	if (!e->src)
		goto err_out_free;

	e->dst = e->src + n->max_pages;

	err = dst_crypto_engine_init(e, n);
	if (err)
		goto err_out_free_all;

	return e;

err_out_free_all:
	kfree(e->src);
err_out_free:
	kfree(e);
err_out_exit:
	return ERR_PTR(err);
}

static void dst_crypto_thread_cleanup(void *private)
{
	struct dst_crypto_engine *e = private;

	dst_crypto_engine_exit(e);
	kfree(e->src);
	kfree(e);
}

/*
 * Initialize crypto engine for given node: store keys, create pool
 * of threads, initialize each one.
 *
 * Each thread has unique ID, but 0 and 1 are reserved for receiving and accepting
 * threads (if export node), so IDs could start from 2, but starting them
 * from 10 allows easily understand what this thread is for.
 */
int dst_node_crypto_init(struct dst_node *n, struct dst_crypto_ctl *ctl)
{
	void *key = (ctl + 1);
	int err = -ENOMEM, i;
	char name[32];

	if (ctl->hash_keysize) {
		n->hash_key = kmalloc(ctl->hash_keysize, GFP_KERNEL);
		if (!n->hash_key)
			goto err_out_exit;
		memcpy(n->hash_key, key, ctl->hash_keysize);
	}

	if (ctl->cipher_keysize) {
		n->cipher_key = kmalloc(ctl->cipher_keysize, GFP_KERNEL);
		if (!n->cipher_key)
			goto err_out_free_hash;
		memcpy(n->cipher_key, key, ctl->cipher_keysize);
	}
	memcpy(&n->crypto, ctl, sizeof(struct dst_crypto_ctl));

	for (i=0; i<ctl->thread_num; ++i) {
		snprintf(name, sizeof(name), "%s-crypto-%d", n->name, i);
		/* Unique ids... */
		err = thread_pool_add_worker(n->pool, name, i+10,
			dst_crypto_thread_init, dst_crypto_thread_cleanup, n);
		if (err)
			goto err_out_free_threads;
	}

	return 0;

err_out_free_threads:
	while (--i >= 0)
		thread_pool_del_worker_id(n->pool, i+10);

	if (ctl->cipher_keysize)
		kfree(n->cipher_key);
	ctl->cipher_keysize = 0;
err_out_free_hash:
	if (ctl->hash_keysize)
		kfree(n->hash_key);
	ctl->hash_keysize = 0;
err_out_exit:
	return err;
}

void dst_node_crypto_exit(struct dst_node *n)
{
	struct dst_crypto_ctl *ctl = &n->crypto;

	if (ctl->cipher_algo[0] || ctl->hash_algo[0]) {
		kfree(n->hash_key);
		kfree(n->cipher_key);
	}
}

/*
 * Thrad pool setup callback. Just stores a transaction in private data.
 */
static int dst_trans_crypto_setup(void *crypto_engine, void *trans)
{
	struct dst_crypto_engine *e = crypto_engine;

	e->private = trans;
	return 0;
}

#if 0
static void dst_dump_bio(struct bio *bio)
{
	u8 *p;
	struct bio_vec *bv;
	int i;

	bio_for_each_segment(bv, bio, i) {
		dprintk("%s: %llu/%u: size: %u, offset: %u, data: ",
				__func__, bio->bi_sector, bio->bi_size,
				bv->bv_len, bv->bv_offset);

		p = kmap(bv->bv_page) + bv->bv_offset;
		for (i=0; i<bv->bv_len; ++i)
			printk("%02x ", p[i]);
		kunmap(bv->bv_page);
		printk("\n");
	}
}
#endif

/*
 * Encrypt/hash data and send it to the network.
 */
static int dst_crypto_process_sending(struct dst_crypto_engine *e,
		struct bio *bio, u8 *hash)
{
	int err;

	if (e->cipher) {
		err = dst_crypt(e, bio);
		if (err)
			goto err_out_exit;
	}

	if (e->hash) {
		err = dst_hash(e, bio, hash);
		if (err)
			goto err_out_exit;

#ifdef CONFIG_DST_DEBUG
		{
			unsigned int i;

			/* dst_dump_bio(bio); */

			printk(KERN_DEBUG "%s: bio: %llu/%u, rw: %lu, hash: ",
				__func__, (u64)bio->bi_sector,
				bio->bi_size, bio_data_dir(bio));
			for (i=0; i<crypto_hash_digestsize(e->hash); ++i)
					printk("%02x ", hash[i]);
			printk("\n");
		}
#endif
	}

	return 0;

err_out_exit:
	return err;
}

/*
 * Check if received data is valid. Decipher if it is.
 */
static int dst_crypto_process_receiving(struct dst_crypto_engine *e,
		struct bio *bio, u8 *hash, u8 *recv_hash)
{
	int err;

	if (e->hash) {
		int mismatch;

		err = dst_hash(e, bio, hash);
		if (err)
			goto err_out_exit;

		mismatch = !!memcmp(recv_hash, hash,
				crypto_hash_digestsize(e->hash));
#ifdef CONFIG_DST_DEBUG
		/* dst_dump_bio(bio); */

		printk(KERN_DEBUG "%s: bio: %llu/%u, rw: %lu, hash mismatch: %d",
			__func__, (u64)bio->bi_sector, bio->bi_size,
			bio_data_dir(bio), mismatch);
		if (mismatch) {
			unsigned int i;

			printk(", recv/calc: ");
			for (i=0; i<crypto_hash_digestsize(e->hash); ++i) {
				printk("%02x/%02x ", recv_hash[i], hash[i]);
			}
		}
		printk("\n");
#endif
		err = -1;
		if (mismatch)
			goto err_out_exit;
	}

	if (e->cipher) {
		err = dst_crypt(e, bio);
		if (err)
			goto err_out_exit;
	}

	return 0;

err_out_exit:
	return err;
}

/*
 * Thread pool callback to encrypt data and send it to the netowork.
 */
static int dst_trans_crypto_action(void *crypto_engine, void *schedule_data)
{
	struct dst_crypto_engine *e = crypto_engine;
	struct dst_trans *t = schedule_data;
	struct bio *bio = t->bio;
	int err;

	dprintk("%s: t: %p, gen: %llu, cipher: %p, hash: %p.\n",
			__func__, t, t->gen, e->cipher, e->hash);

	e->enc = t->enc;
	e->iv = dst_gen_iv(t);

	if (bio_data_dir(bio) == WRITE) {
		err = dst_crypto_process_sending(e, bio, t->cmd.hash);
		if (err)
			goto err_out_exit;

		if (e->hash) {
			t->cmd.csize = crypto_hash_digestsize(e->hash);
			t->cmd.size += t->cmd.csize;
		}

		return dst_trans_send(t);
	} else {
		u8 *hash = e->data + e->size/2;

		err = dst_crypto_process_receiving(e, bio, hash, t->cmd.hash);
		if (err)
			goto err_out_exit;

		dst_trans_remove(t);
		dst_trans_put(t);
	}

	return 0;

err_out_exit:
	t->error = err;
	dst_trans_put(t);
	return err;
}

/*
 * Schedule crypto processing for given transaction.
 */
int dst_trans_crypto(struct dst_trans *t)
{
	struct dst_node *n = t->n;
	int err;

	err = thread_pool_schedule(n->pool,
		dst_trans_crypto_setup, dst_trans_crypto_action,
		t, MAX_SCHEDULE_TIMEOUT);
	if (err)
		goto err_out_exit;

	return 0;

err_out_exit:
	dst_trans_put(t);
	return err;
}

/*
 * Crypto machinery for the export node.
 */
static int dst_export_crypto_setup(void *crypto_engine, void *bio)
{
	struct dst_crypto_engine *e = crypto_engine;

	e->private = bio;
	return 0;
}

static int dst_export_crypto_action(void *crypto_engine, void *schedule_data)
{
	struct dst_crypto_engine *e = crypto_engine;
	struct bio *bio = schedule_data;
	struct dst_export_priv *p = bio->bi_private;
	int err;

	dprintk("%s: e: %p, data: %p, bio: %llu/%u, dir: %lu.\n", __func__,
		e, e->data, (u64)bio->bi_sector, bio->bi_size, bio_data_dir(bio));

	e->enc = (bio_data_dir(bio) == READ);
	e->iv = p->cmd.id;

	if (bio_data_dir(bio) == WRITE) {
		u8 *hash = e->data + e->size/2;

		err = dst_crypto_process_receiving(e, bio, hash, p->cmd.hash);
		if (err)
			goto err_out_exit;

		generic_make_request(bio);
	} else {
		err = dst_crypto_process_sending(e, bio, p->cmd.hash);
		if (err)
			goto err_out_exit;

		if (e->hash) {
			p->cmd.csize = crypto_hash_digestsize(e->hash);
			p->cmd.size += p->cmd.csize;
		}

		err = dst_export_send_bio(bio);
	}
	return 0;

err_out_exit:
	bio_put(bio);
	return err;
}

int dst_export_crypto(struct dst_node *n, struct bio *bio)
{
	int err;

	err = thread_pool_schedule(n->pool,
		dst_export_crypto_setup, dst_export_crypto_action,
		bio, MAX_SCHEDULE_TIMEOUT);
	if (err)
		goto err_out_exit;

	return 0;

err_out_exit:
	bio_put(bio);
	return err;
}
