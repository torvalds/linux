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

#include <linux/crypto.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>
#include <linux/slab.h>

#include "netfs.h"

static struct crypto_hash *pohmelfs_init_hash(struct pohmelfs_sb *psb)
{
	int err;
	struct crypto_hash *hash;

	hash = crypto_alloc_hash(psb->hash_string, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(hash)) {
		err = PTR_ERR(hash);
		dprintk("%s: idx: %u: failed to allocate hash '%s', err: %d.\n",
				__func__, psb->idx, psb->hash_string, err);
		goto err_out_exit;
	}

	psb->crypto_attached_size = crypto_hash_digestsize(hash);

	if (!psb->hash_keysize)
		return hash;

	err = crypto_hash_setkey(hash, psb->hash_key, psb->hash_keysize);
	if (err) {
		dprintk("%s: idx: %u: failed to set key for hash '%s', err: %d.\n",
				__func__, psb->idx, psb->hash_string, err);
		goto err_out_free;
	}

	return hash;

err_out_free:
	crypto_free_hash(hash);
err_out_exit:
	return ERR_PTR(err);
}

static struct crypto_ablkcipher *pohmelfs_init_cipher(struct pohmelfs_sb *psb)
{
	int err = -EINVAL;
	struct crypto_ablkcipher *cipher;

	if (!psb->cipher_keysize)
		goto err_out_exit;

	cipher = crypto_alloc_ablkcipher(psb->cipher_string, 0, 0);
	if (IS_ERR(cipher)) {
		err = PTR_ERR(cipher);
		dprintk("%s: idx: %u: failed to allocate cipher '%s', err: %d.\n",
				__func__, psb->idx, psb->cipher_string, err);
		goto err_out_exit;
	}

	crypto_ablkcipher_clear_flags(cipher, ~0);

	err = crypto_ablkcipher_setkey(cipher, psb->cipher_key, psb->cipher_keysize);
	if (err) {
		dprintk("%s: idx: %u: failed to set key for cipher '%s', err: %d.\n",
				__func__, psb->idx, psb->cipher_string, err);
		goto err_out_free;
	}

	return cipher;

err_out_free:
	crypto_free_ablkcipher(cipher);
err_out_exit:
	return ERR_PTR(err);
}

int pohmelfs_crypto_engine_init(struct pohmelfs_crypto_engine *e, struct pohmelfs_sb *psb)
{
	int err;

	e->page_num = 0;

	e->size = PAGE_SIZE;
	e->data = kmalloc(e->size, GFP_KERNEL);
	if (!e->data) {
		err = -ENOMEM;
		goto err_out_exit;
	}

	if (psb->hash_string) {
		e->hash = pohmelfs_init_hash(psb);
		if (IS_ERR(e->hash)) {
			err = PTR_ERR(e->hash);
			e->hash = NULL;
			goto err_out_free;
		}
	}

	if (psb->cipher_string) {
		e->cipher = pohmelfs_init_cipher(psb);
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
err_out_exit:
	return err;
}

void pohmelfs_crypto_engine_exit(struct pohmelfs_crypto_engine *e)
{
	if (e->hash)
		crypto_free_hash(e->hash);
	if (e->cipher)
		crypto_free_ablkcipher(e->cipher);
	kfree(e->data);
}

static void pohmelfs_crypto_complete(struct crypto_async_request *req, int err)
{
	struct pohmelfs_crypto_completion *c = req->data;

	if (err == -EINPROGRESS)
		return;

	dprintk("%s: req: %p, err: %d.\n", __func__, req, err);
	c->error = err;
	complete(&c->complete);
}

static int pohmelfs_crypto_process(struct ablkcipher_request *req,
		struct scatterlist *sg_dst, struct scatterlist *sg_src,
		void *iv, int enc, unsigned long timeout)
{
	struct pohmelfs_crypto_completion complete;
	int err;

	init_completion(&complete.complete);
	complete.error = -EINPROGRESS;

	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					pohmelfs_crypto_complete, &complete);

	ablkcipher_request_set_crypt(req, sg_src, sg_dst, sg_src->length, iv);

	if (enc)
		err = crypto_ablkcipher_encrypt(req);
	else
		err = crypto_ablkcipher_decrypt(req);

	switch (err) {
		case -EINPROGRESS:
		case -EBUSY:
			err = wait_for_completion_interruptible_timeout(&complete.complete,
					timeout);
			if (!err)
				err = -ETIMEDOUT;
			else
				err = complete.error;
			break;
		default:
			break;
	}

	return err;
}

int pohmelfs_crypto_process_input_data(struct pohmelfs_crypto_engine *e, u64 cmd_iv,
		void *data, struct page *page, unsigned int size)
{
	int err;
	struct scatterlist sg;

	if (!e->cipher && !e->hash)
		return 0;

	dprintk("%s: eng: %p, iv: %llx, data: %p, page: %p/%lu, size: %u.\n",
		__func__, e, cmd_iv, data, page, (page)?page->index:0, size);

	if (data) {
		sg_init_one(&sg, data, size);
	} else {
		sg_init_table(&sg, 1);
		sg_set_page(&sg, page, size, 0);
	}

	if (e->cipher) {
		struct ablkcipher_request *req = e->data + crypto_hash_digestsize(e->hash);
		u8 iv[32];

		memset(iv, 0, sizeof(iv));
		memcpy(iv, &cmd_iv, sizeof(cmd_iv));

		ablkcipher_request_set_tfm(req, e->cipher);

		err = pohmelfs_crypto_process(req, &sg, &sg, iv, 0, e->timeout);
		if (err)
			goto err_out_exit;
	}

	if (e->hash) {
		struct hash_desc desc;
		void *dst = e->data + e->size/2;

		desc.tfm = e->hash;
		desc.flags = 0;

		err = crypto_hash_init(&desc);
		if (err)
			goto err_out_exit;

		err = crypto_hash_update(&desc, &sg, size);
		if (err)
			goto err_out_exit;

		err = crypto_hash_final(&desc, dst);
		if (err)
			goto err_out_exit;

		err = !!memcmp(dst, e->data, crypto_hash_digestsize(e->hash));

		if (err) {
#ifdef CONFIG_POHMELFS_DEBUG
			unsigned int i;
			unsigned char *recv = e->data, *calc = dst;

			dprintk("%s: eng: %p, hash: %p, cipher: %p: iv : %llx, hash mismatch (recv/calc): ",
					__func__, e, e->hash, e->cipher, cmd_iv);
			for (i=0; i<crypto_hash_digestsize(e->hash); ++i) {
#if 0
				dprintka("%02x ", recv[i]);
				if (recv[i] != calc[i]) {
					dprintka("| calc byte: %02x.\n", calc[i]);
					break;
				}
#else
				dprintka("%02x/%02x ", recv[i], calc[i]);
#endif
			}
			dprintk("\n");
#endif
			goto err_out_exit;
		} else {
			dprintk("%s: eng: %p, hash: %p, cipher: %p: hashes matched.\n",
					__func__, e, e->hash, e->cipher);
		}
	}

	dprintk("%s: eng: %p, size: %u, hash: %p, cipher: %p: completed.\n",
			__func__, e, e->size, e->hash, e->cipher);

	return 0;

err_out_exit:
	dprintk("%s: eng: %p, hash: %p, cipher: %p: err: %d.\n",
			__func__, e, e->hash, e->cipher, err);
	return err;
}

static int pohmelfs_trans_iter(struct netfs_trans *t, struct pohmelfs_crypto_engine *e,
		int (* iterator) (struct pohmelfs_crypto_engine *e,
				  struct scatterlist *dst,
				  struct scatterlist *src))
{
	void *data = t->iovec.iov_base + sizeof(struct netfs_cmd) + t->psb->crypto_attached_size;
	unsigned int size = t->iovec.iov_len - sizeof(struct netfs_cmd) - t->psb->crypto_attached_size;
	struct netfs_cmd *cmd = data;
	unsigned int sz, pages = t->attached_pages, i, csize, cmd_cmd, dpage_idx;
	struct scatterlist sg_src, sg_dst;
	int err;

	while (size) {
		cmd = data;
		cmd_cmd = __be16_to_cpu(cmd->cmd);
		csize = __be32_to_cpu(cmd->size);
		cmd->iv = __cpu_to_be64(e->iv);

		if (cmd_cmd == NETFS_READ_PAGES || cmd_cmd == NETFS_READ_PAGE)
			csize = __be16_to_cpu(cmd->ext);

		sz = csize + __be16_to_cpu(cmd->cpad) + sizeof(struct netfs_cmd);

		dprintk("%s: size: %u, sz: %u, cmd_size: %u, cmd_cpad: %u.\n",
				__func__, size, sz, __be32_to_cpu(cmd->size), __be16_to_cpu(cmd->cpad));

		data += sz;
		size -= sz;

		sg_init_one(&sg_src, cmd->data, sz - sizeof(struct netfs_cmd));
		sg_init_one(&sg_dst, cmd->data, sz - sizeof(struct netfs_cmd));

		err = iterator(e, &sg_dst, &sg_src);
		if (err)
			return err;
	}

	if (!pages)
		return 0;

	dpage_idx = 0;
	for (i=0; i<t->page_num; ++i) {
		struct page *page = t->pages[i];
		struct page *dpage = e->pages[dpage_idx];

		if (!page)
			continue;

		sg_init_table(&sg_src, 1);
		sg_init_table(&sg_dst, 1);
		sg_set_page(&sg_src, page, page_private(page), 0);
		sg_set_page(&sg_dst, dpage, page_private(page), 0);

		err = iterator(e, &sg_dst, &sg_src);
		if (err)
			return err;

		pages--;
		if (!pages)
			break;
		dpage_idx++;
	}

	return 0;
}

static int pohmelfs_encrypt_iterator(struct pohmelfs_crypto_engine *e,
		struct scatterlist *sg_dst, struct scatterlist *sg_src)
{
	struct ablkcipher_request *req = e->data;
	u8 iv[32];

	memset(iv, 0, sizeof(iv));

	memcpy(iv, &e->iv, sizeof(e->iv));

	return pohmelfs_crypto_process(req, sg_dst, sg_src, iv, 1, e->timeout);
}

static int pohmelfs_encrypt(struct pohmelfs_crypto_thread *tc)
{
	struct netfs_trans *t = tc->trans;
	struct pohmelfs_crypto_engine *e = &tc->eng;
	struct ablkcipher_request *req = e->data;

	memset(req, 0, sizeof(struct ablkcipher_request));
	ablkcipher_request_set_tfm(req, e->cipher);

	e->iv = pohmelfs_gen_iv(t);

	return pohmelfs_trans_iter(t, e, pohmelfs_encrypt_iterator);
}

static int pohmelfs_hash_iterator(struct pohmelfs_crypto_engine *e,
		struct scatterlist *sg_dst, struct scatterlist *sg_src)
{
	return crypto_hash_update(e->data, sg_src, sg_src->length);
}

static int pohmelfs_hash(struct pohmelfs_crypto_thread *tc)
{
	struct pohmelfs_crypto_engine *e = &tc->eng;
	struct hash_desc *desc = e->data;
	unsigned char *dst = tc->trans->iovec.iov_base + sizeof(struct netfs_cmd);
	int err;

	desc->tfm = e->hash;
	desc->flags = 0;

	err = crypto_hash_init(desc);
	if (err)
		return err;

	err = pohmelfs_trans_iter(tc->trans, e, pohmelfs_hash_iterator);
	if (err)
		return err;

	err = crypto_hash_final(desc, dst);
	if (err)
		return err;

	{
		unsigned int i;
		dprintk("%s: ", __func__);
		for (i=0; i<tc->psb->crypto_attached_size; ++i)
			dprintka("%02x ", dst[i]);
		dprintka("\n");
	}

	return 0;
}

static void pohmelfs_crypto_pages_free(struct pohmelfs_crypto_engine *e)
{
	unsigned int i;

	for (i=0; i<e->page_num; ++i)
		__free_page(e->pages[i]);
	kfree(e->pages);
}

static int pohmelfs_crypto_pages_alloc(struct pohmelfs_crypto_engine *e, struct pohmelfs_sb *psb)
{
	unsigned int i;

	e->pages = kmalloc(psb->trans_max_pages * sizeof(struct page *), GFP_KERNEL);
	if (!e->pages)
		return -ENOMEM;

	for (i=0; i<psb->trans_max_pages; ++i) {
		e->pages[i] = alloc_page(GFP_KERNEL);
		if (!e->pages[i])
			break;
	}

	e->page_num = i;
	if (!e->page_num)
		goto err_out_free;

	return 0;

err_out_free:
	kfree(e->pages);
	return -ENOMEM;
}

static void pohmelfs_sys_crypto_exit_one(struct pohmelfs_crypto_thread *t)
{
	struct pohmelfs_sb *psb = t->psb;

	if (t->thread)
		kthread_stop(t->thread);

	mutex_lock(&psb->crypto_thread_lock);
	list_del(&t->thread_entry);
	psb->crypto_thread_num--;
	mutex_unlock(&psb->crypto_thread_lock);

	pohmelfs_crypto_engine_exit(&t->eng);
	pohmelfs_crypto_pages_free(&t->eng);
	kfree(t);
}

static int pohmelfs_crypto_finish(struct netfs_trans *t, struct pohmelfs_sb *psb, int err)
{
	struct netfs_cmd *cmd = t->iovec.iov_base;
	netfs_convert_cmd(cmd);

	if (likely(!err))
		err = netfs_trans_finish_send(t, psb);

	t->result = err;
	netfs_trans_put(t);

	return err;
}

void pohmelfs_crypto_thread_make_ready(struct pohmelfs_crypto_thread *th)
{
	struct pohmelfs_sb *psb = th->psb;

	th->page = NULL;
	th->trans = NULL;

	mutex_lock(&psb->crypto_thread_lock);
	list_move_tail(&th->thread_entry, &psb->crypto_ready_list);
	mutex_unlock(&psb->crypto_thread_lock);
	wake_up(&psb->wait);
}

static int pohmelfs_crypto_thread_trans(struct pohmelfs_crypto_thread *t)
{
	struct netfs_trans *trans;
	int err = 0;

	trans = t->trans;
	trans->eng = NULL;

	if (t->eng.hash) {
		err = pohmelfs_hash(t);
		if (err)
			goto out_complete;
	}

	if (t->eng.cipher) {
		err = pohmelfs_encrypt(t);
		if (err)
			goto out_complete;
		trans->eng = &t->eng;
	}

out_complete:
	t->page = NULL;
	t->trans = NULL;

	if (!trans->eng)
		pohmelfs_crypto_thread_make_ready(t);

	pohmelfs_crypto_finish(trans, t->psb, err);
	return err;
}

static int pohmelfs_crypto_thread_page(struct pohmelfs_crypto_thread *t)
{
	struct pohmelfs_crypto_engine *e = &t->eng;
	struct page *page = t->page;
	int err;

	WARN_ON(!PageChecked(page));

	err = pohmelfs_crypto_process_input_data(e, e->iv, NULL, page, t->size);
	if (!err)
		SetPageUptodate(page);
	else
		SetPageError(page);
	unlock_page(page);
	page_cache_release(page);

	pohmelfs_crypto_thread_make_ready(t);

	return err;
}

static int pohmelfs_crypto_thread_func(void *data)
{
	struct pohmelfs_crypto_thread *t = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(t->wait, kthread_should_stop() ||
				t->trans || t->page);

		if (kthread_should_stop())
			break;

		if (!t->trans && !t->page)
			continue;

		dprintk("%s: thread: %p, trans: %p, page: %p.\n",
				__func__, t, t->trans, t->page);

		if (t->trans)
			pohmelfs_crypto_thread_trans(t);
		else if (t->page)
			pohmelfs_crypto_thread_page(t);
	}

	return 0;
}

static void pohmelfs_crypto_flush(struct pohmelfs_sb *psb, struct list_head *head)
{
	while (!list_empty(head)) {
		struct pohmelfs_crypto_thread *t = NULL;

		mutex_lock(&psb->crypto_thread_lock);
		if (!list_empty(head)) {
			t = list_first_entry(head, struct pohmelfs_crypto_thread, thread_entry);
			list_del_init(&t->thread_entry);
		}
		mutex_unlock(&psb->crypto_thread_lock);

		if (t)
			pohmelfs_sys_crypto_exit_one(t);
	}
}

static void pohmelfs_sys_crypto_exit(struct pohmelfs_sb *psb)
{
	while (!list_empty(&psb->crypto_active_list) || !list_empty(&psb->crypto_ready_list)) {
		dprintk("%s: crypto_thread_num: %u.\n", __func__, psb->crypto_thread_num);
		pohmelfs_crypto_flush(psb, &psb->crypto_active_list);
		pohmelfs_crypto_flush(psb, &psb->crypto_ready_list);
	}
}

static int pohmelfs_sys_crypto_init(struct pohmelfs_sb *psb)
{
	unsigned int i;
	struct pohmelfs_crypto_thread *t;
	struct pohmelfs_config *c;
	struct netfs_state *st;
	int err;

	list_for_each_entry(c, &psb->state_list, config_entry) {
		st = &c->state;

		err = pohmelfs_crypto_engine_init(&st->eng, psb);
		if (err)
			goto err_out_exit;

		dprintk("%s: st: %p, eng: %p, hash: %p, cipher: %p.\n",
				__func__, st, &st->eng, &st->eng.hash, &st->eng.cipher);
	}

	for (i=0; i<psb->crypto_thread_num; ++i) {
		err = -ENOMEM;
		t = kzalloc(sizeof(struct pohmelfs_crypto_thread), GFP_KERNEL);
		if (!t)
			goto err_out_free_state_engines;

		init_waitqueue_head(&t->wait);

		t->psb = psb;
		t->trans = NULL;
		t->eng.thread = t;

		err = pohmelfs_crypto_engine_init(&t->eng, psb);
		if (err)
			goto err_out_free_state_engines;

		err = pohmelfs_crypto_pages_alloc(&t->eng, psb);
		if (err)
			goto err_out_free;

		t->thread = kthread_run(pohmelfs_crypto_thread_func, t,
				"pohmelfs-crypto-%d-%d", psb->idx, i);
		if (IS_ERR(t->thread)) {
			err = PTR_ERR(t->thread);
			t->thread = NULL;
			goto err_out_free;
		}

		if (t->eng.cipher)
			psb->crypto_align_size = crypto_ablkcipher_blocksize(t->eng.cipher);

		mutex_lock(&psb->crypto_thread_lock);
		list_add_tail(&t->thread_entry, &psb->crypto_ready_list);
		mutex_unlock(&psb->crypto_thread_lock);
	}

	psb->crypto_thread_num = i;
	return 0;

err_out_free:
	pohmelfs_sys_crypto_exit_one(t);
err_out_free_state_engines:
	list_for_each_entry(c, &psb->state_list, config_entry) {
		st = &c->state;
		pohmelfs_crypto_engine_exit(&st->eng);
	}
err_out_exit:
	pohmelfs_sys_crypto_exit(psb);
	return err;
}

void pohmelfs_crypto_exit(struct pohmelfs_sb *psb)
{
	pohmelfs_sys_crypto_exit(psb);

	kfree(psb->hash_string);
	kfree(psb->cipher_string);
}

static int pohmelfs_crypt_init_complete(struct page **pages, unsigned int page_num,
		void *private, int err)
{
	struct pohmelfs_sb *psb = private;

	psb->flags = -err;
	dprintk("%s: err: %d.\n", __func__, err);

	wake_up(&psb->wait);

	return err;
}

static int pohmelfs_crypto_init_handshake(struct pohmelfs_sb *psb)
{
	struct netfs_trans *t;
	struct netfs_crypto_capabilities *cap;
	struct netfs_cmd *cmd;
	char *str;
	int err = -ENOMEM, size;

	size = sizeof(struct netfs_crypto_capabilities) +
		psb->cipher_strlen + psb->hash_strlen + 2; /* 0 bytes */

	t = netfs_trans_alloc(psb, size, 0, 0);
	if (!t)
		goto err_out_exit;

	t->complete = pohmelfs_crypt_init_complete;
	t->private = psb;

	cmd = netfs_trans_current(t);
	cap = (struct netfs_crypto_capabilities *)(cmd + 1);
	str = (char *)(cap + 1);

	cmd->cmd = NETFS_CAPABILITIES;
	cmd->id = POHMELFS_CRYPTO_CAPABILITIES;
	cmd->size = size;
	cmd->start = 0;
	cmd->ext = 0;
	cmd->csize = 0;

	netfs_convert_cmd(cmd);
	netfs_trans_update(cmd, t, size);

	cap->hash_strlen = psb->hash_strlen;
	if (cap->hash_strlen) {
		sprintf(str, "%s", psb->hash_string);
		str += cap->hash_strlen;
	}

	cap->cipher_strlen = psb->cipher_strlen;
	cap->cipher_keysize = psb->cipher_keysize;
	if (cap->cipher_strlen)
		sprintf(str, "%s", psb->cipher_string);

	netfs_convert_crypto_capabilities(cap);

	psb->flags = ~0;
	err = netfs_trans_finish(t, psb);
	if (err)
		goto err_out_exit;

	err = wait_event_interruptible_timeout(psb->wait, (psb->flags != ~0),
			psb->wait_on_page_timeout);
	if (!err)
		err = -ETIMEDOUT;
	else
		err = -psb->flags;

	if (!err)
		psb->perform_crypto = 1;
	psb->flags = 0;

	/*
	 * At this point NETFS_CAPABILITIES response command
	 * should setup superblock in a way, which is acceptible
	 * for both client and server, so if server refuses connection,
	 * it will send error in transaction response.
	 */

	if (err)
		goto err_out_exit;

	return 0;

err_out_exit:
	return err;
}

int pohmelfs_crypto_init(struct pohmelfs_sb *psb)
{
	int err;

	if (!psb->cipher_string && !psb->hash_string)
		return 0;

	err = pohmelfs_crypto_init_handshake(psb);
	if (err)
		return err;

	err = pohmelfs_sys_crypto_init(psb);
	if (err)
		return err;

	return 0;
}

static int pohmelfs_crypto_thread_get(struct pohmelfs_sb *psb,
		int (* action)(struct pohmelfs_crypto_thread *t, void *data), void *data)
{
	struct pohmelfs_crypto_thread *t = NULL;
	int err;

	while (!t) {
		err = wait_event_interruptible_timeout(psb->wait,
				!list_empty(&psb->crypto_ready_list),
				psb->wait_on_page_timeout);

		t = NULL;
		err = 0;
		mutex_lock(&psb->crypto_thread_lock);
		if (!list_empty(&psb->crypto_ready_list)) {
			t = list_entry(psb->crypto_ready_list.prev,
					struct pohmelfs_crypto_thread,
					thread_entry);

			list_move_tail(&t->thread_entry,
					&psb->crypto_active_list);

			action(t, data);
			wake_up(&t->wait);

		}
		mutex_unlock(&psb->crypto_thread_lock);
	}

	return err;
}

static int pohmelfs_trans_crypt_action(struct pohmelfs_crypto_thread *t, void *data)
{
	struct netfs_trans *trans = data;

	netfs_trans_get(trans);
	t->trans = trans;

	dprintk("%s: t: %p, gen: %u, thread: %p.\n", __func__, trans, trans->gen, t);
	return 0;
}

int pohmelfs_trans_crypt(struct netfs_trans *trans, struct pohmelfs_sb *psb)
{
	if ((!psb->hash_string && !psb->cipher_string) || !psb->perform_crypto) {
		netfs_trans_get(trans);
		return pohmelfs_crypto_finish(trans, psb, 0);
	}

	return pohmelfs_crypto_thread_get(psb, pohmelfs_trans_crypt_action, trans);
}

struct pohmelfs_crypto_input_action_data {
	struct page			*page;
	struct pohmelfs_crypto_engine	*e;
	u64				iv;
	unsigned int			size;
};

static int pohmelfs_crypt_input_page_action(struct pohmelfs_crypto_thread *t, void *data)
{
	struct pohmelfs_crypto_input_action_data *act = data;

	memcpy(t->eng.data, act->e->data, t->psb->crypto_attached_size);

	t->size = act->size;
	t->eng.iv = act->iv;

	t->page = act->page;
	return 0;
}

int pohmelfs_crypto_process_input_page(struct pohmelfs_crypto_engine *e,
		struct page *page, unsigned int size, u64 iv)
{
	struct inode *inode = page->mapping->host;
	struct pohmelfs_crypto_input_action_data act;
	int err = -ENOENT;

	act.page = page;
	act.e = e;
	act.size = size;
	act.iv = iv;

	err = pohmelfs_crypto_thread_get(POHMELFS_SB(inode->i_sb),
			pohmelfs_crypt_input_page_action, &act);
	if (err)
		goto err_out_exit;

	return 0;

err_out_exit:
	SetPageUptodate(page);
	page_cache_release(page);

	return err;
}
