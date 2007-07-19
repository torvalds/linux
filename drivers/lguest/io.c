/* Simple I/O model for guests, based on shared memory.
 * Copyright (C) 2006 Rusty Russell IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */
#include <linux/types.h>
#include <linux/futex.h>
#include <linux/jhash.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/uaccess.h>
#include "lg.h"

static struct list_head dma_hash[61];

void lguest_io_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dma_hash); i++)
		INIT_LIST_HEAD(&dma_hash[i]);
}

/* FIXME: allow multi-page lengths. */
static int check_dma_list(struct lguest *lg, const struct lguest_dma *dma)
{
	unsigned int i;

	for (i = 0; i < LGUEST_MAX_DMA_SECTIONS; i++) {
		if (!dma->len[i])
			return 1;
		if (!lguest_address_ok(lg, dma->addr[i], dma->len[i]))
			goto kill;
		if (dma->len[i] > PAGE_SIZE)
			goto kill;
		/* We could do over a page, but is it worth it? */
		if ((dma->addr[i] % PAGE_SIZE) + dma->len[i] > PAGE_SIZE)
			goto kill;
	}
	return 1;

kill:
	kill_guest(lg, "bad DMA entry: %u@%#lx", dma->len[i], dma->addr[i]);
	return 0;
}

static unsigned int hash(const union futex_key *key)
{
	return jhash2((u32*)&key->both.word,
		      (sizeof(key->both.word)+sizeof(key->both.ptr))/4,
		      key->both.offset)
		% ARRAY_SIZE(dma_hash);
}

static inline int key_eq(const union futex_key *a, const union futex_key *b)
{
	return (a->both.word == b->both.word
		&& a->both.ptr == b->both.ptr
		&& a->both.offset == b->both.offset);
}

/* Must hold read lock on dmainfo owner's current->mm->mmap_sem */
static void unlink_dma(struct lguest_dma_info *dmainfo)
{
	BUG_ON(!mutex_is_locked(&lguest_lock));
	dmainfo->interrupt = 0;
	list_del(&dmainfo->list);
	drop_futex_key_refs(&dmainfo->key);
}

static int unbind_dma(struct lguest *lg,
		      const union futex_key *key,
		      unsigned long dmas)
{
	int i, ret = 0;

	for (i = 0; i < LGUEST_MAX_DMA; i++) {
		if (key_eq(key, &lg->dma[i].key) && dmas == lg->dma[i].dmas) {
			unlink_dma(&lg->dma[i]);
			ret = 1;
			break;
		}
	}
	return ret;
}

int bind_dma(struct lguest *lg,
	     unsigned long ukey, unsigned long dmas, u16 numdmas, u8 interrupt)
{
	unsigned int i;
	int ret = 0;
	union futex_key key;
	struct rw_semaphore *fshared = &current->mm->mmap_sem;

	if (interrupt >= LGUEST_IRQS)
		return 0;

	mutex_lock(&lguest_lock);
	down_read(fshared);
	if (get_futex_key((u32 __user *)ukey, fshared, &key) != 0) {
		kill_guest(lg, "bad dma key %#lx", ukey);
		goto unlock;
	}
	get_futex_key_refs(&key);

	if (interrupt == 0)
		ret = unbind_dma(lg, &key, dmas);
	else {
		for (i = 0; i < LGUEST_MAX_DMA; i++) {
			if (lg->dma[i].interrupt)
				continue;

			lg->dma[i].dmas = dmas;
			lg->dma[i].num_dmas = numdmas;
			lg->dma[i].next_dma = 0;
			lg->dma[i].key = key;
			lg->dma[i].guestid = lg->guestid;
			lg->dma[i].interrupt = interrupt;
			list_add(&lg->dma[i].list, &dma_hash[hash(&key)]);
			ret = 1;
			goto unlock;
		}
	}
	drop_futex_key_refs(&key);
unlock:
 	up_read(fshared);
	mutex_unlock(&lguest_lock);
	return ret;
}

/* lgread from another guest */
static int lgread_other(struct lguest *lg,
			void *buf, u32 addr, unsigned bytes)
{
	if (!lguest_address_ok(lg, addr, bytes)
	    || access_process_vm(lg->tsk, addr, buf, bytes, 0) != bytes) {
		memset(buf, 0, bytes);
		kill_guest(lg, "bad address in registered DMA struct");
		return 0;
	}
	return 1;
}

/* lgwrite to another guest */
static int lgwrite_other(struct lguest *lg, u32 addr,
			 const void *buf, unsigned bytes)
{
	if (!lguest_address_ok(lg, addr, bytes)
	    || (access_process_vm(lg->tsk, addr, (void *)buf, bytes, 1)
		!= bytes)) {
		kill_guest(lg, "bad address writing to registered DMA");
		return 0;
	}
	return 1;
}

static u32 copy_data(struct lguest *srclg,
		     const struct lguest_dma *src,
		     const struct lguest_dma *dst,
		     struct page *pages[])
{
	unsigned int totlen, si, di, srcoff, dstoff;
	void *maddr = NULL;

	totlen = 0;
	si = di = 0;
	srcoff = dstoff = 0;
	while (si < LGUEST_MAX_DMA_SECTIONS && src->len[si]
	       && di < LGUEST_MAX_DMA_SECTIONS && dst->len[di]) {
		u32 len = min(src->len[si] - srcoff, dst->len[di] - dstoff);

		if (!maddr)
			maddr = kmap(pages[di]);

		/* FIXME: This is not completely portable, since
		   archs do different things for copy_to_user_page. */
		if (copy_from_user(maddr + (dst->addr[di] + dstoff)%PAGE_SIZE,
				   (void *__user)src->addr[si], len) != 0) {
			kill_guest(srclg, "bad address in sending DMA");
			totlen = 0;
			break;
		}

		totlen += len;
		srcoff += len;
		dstoff += len;
		if (srcoff == src->len[si]) {
			si++;
			srcoff = 0;
		}
		if (dstoff == dst->len[di]) {
			kunmap(pages[di]);
			maddr = NULL;
			di++;
			dstoff = 0;
		}
	}

	if (maddr)
		kunmap(pages[di]);

	return totlen;
}

/* Src is us, ie. current. */
static u32 do_dma(struct lguest *srclg, const struct lguest_dma *src,
		  struct lguest *dstlg, const struct lguest_dma *dst)
{
	int i;
	u32 ret;
	struct page *pages[LGUEST_MAX_DMA_SECTIONS];

	if (!check_dma_list(dstlg, dst) || !check_dma_list(srclg, src))
		return 0;

	/* First get the destination pages */
	for (i = 0; i < LGUEST_MAX_DMA_SECTIONS; i++) {
		if (dst->len[i] == 0)
			break;
		if (get_user_pages(dstlg->tsk, dstlg->mm,
				   dst->addr[i], 1, 1, 1, pages+i, NULL)
		    != 1) {
			kill_guest(dstlg, "Error mapping DMA pages");
			ret = 0;
			goto drop_pages;
		}
	}

	/* Now copy until we run out of src or dst. */
	ret = copy_data(srclg, src, dst, pages);

drop_pages:
	while (--i >= 0)
		put_page(pages[i]);
	return ret;
}

static int dma_transfer(struct lguest *srclg,
			unsigned long udma,
			struct lguest_dma_info *dst)
{
	struct lguest_dma dst_dma, src_dma;
	struct lguest *dstlg;
	u32 i, dma = 0;

	dstlg = &lguests[dst->guestid];
	/* Get our dma list. */
	lgread(srclg, &src_dma, udma, sizeof(src_dma));

	/* We can't deadlock against them dmaing to us, because this
	 * is all under the lguest_lock. */
	down_read(&dstlg->mm->mmap_sem);

	for (i = 0; i < dst->num_dmas; i++) {
		dma = (dst->next_dma + i) % dst->num_dmas;
		if (!lgread_other(dstlg, &dst_dma,
				  dst->dmas + dma * sizeof(struct lguest_dma),
				  sizeof(dst_dma))) {
			goto fail;
		}
		if (!dst_dma.used_len)
			break;
	}
	if (i != dst->num_dmas) {
		unsigned long used_lenp;
		unsigned int ret;

		ret = do_dma(srclg, &src_dma, dstlg, &dst_dma);
		/* Put used length in src. */
		lgwrite_u32(srclg,
			    udma+offsetof(struct lguest_dma, used_len), ret);
		if (ret == 0 && src_dma.len[0] != 0)
			goto fail;

		/* Make sure destination sees contents before length. */
		wmb();
		used_lenp = dst->dmas
			+ dma * sizeof(struct lguest_dma)
			+ offsetof(struct lguest_dma, used_len);
		lgwrite_other(dstlg, used_lenp, &ret, sizeof(ret));
		dst->next_dma++;
	}
 	up_read(&dstlg->mm->mmap_sem);

	/* Do this last so dst doesn't simply sleep on lock. */
	set_bit(dst->interrupt, dstlg->irqs_pending);
	wake_up_process(dstlg->tsk);
	return i == dst->num_dmas;

fail:
	up_read(&dstlg->mm->mmap_sem);
	return 0;
}

void send_dma(struct lguest *lg, unsigned long ukey, unsigned long udma)
{
	union futex_key key;
	int empty = 0;
	struct rw_semaphore *fshared = &current->mm->mmap_sem;

again:
	mutex_lock(&lguest_lock);
	down_read(fshared);
	if (get_futex_key((u32 __user *)ukey, fshared, &key) != 0) {
		kill_guest(lg, "bad sending DMA key");
		goto unlock;
	}
	/* Shared mapping?  Look for other guests... */
	if (key.shared.offset & 1) {
		struct lguest_dma_info *i;
		list_for_each_entry(i, &dma_hash[hash(&key)], list) {
			if (i->guestid == lg->guestid)
				continue;
			if (!key_eq(&key, &i->key))
				continue;

			empty += dma_transfer(lg, udma, i);
			break;
		}
		if (empty == 1) {
			/* Give any recipients one chance to restock. */
			up_read(&current->mm->mmap_sem);
			mutex_unlock(&lguest_lock);
			empty++;
			goto again;
		}
	} else {
		/* Private mapping: tell our userspace. */
		lg->dma_is_pending = 1;
		lg->pending_dma = udma;
		lg->pending_key = ukey;
	}
unlock:
	up_read(fshared);
	mutex_unlock(&lguest_lock);
}

void release_all_dma(struct lguest *lg)
{
	unsigned int i;

	BUG_ON(!mutex_is_locked(&lguest_lock));

	down_read(&lg->mm->mmap_sem);
	for (i = 0; i < LGUEST_MAX_DMA; i++) {
		if (lg->dma[i].interrupt)
			unlink_dma(&lg->dma[i]);
	}
	up_read(&lg->mm->mmap_sem);
}

/* Userspace wants a dma buffer from this guest. */
unsigned long get_dma_buffer(struct lguest *lg,
			     unsigned long ukey, unsigned long *interrupt)
{
	unsigned long ret = 0;
	union futex_key key;
	struct lguest_dma_info *i;
	struct rw_semaphore *fshared = &current->mm->mmap_sem;

	mutex_lock(&lguest_lock);
	down_read(fshared);
	if (get_futex_key((u32 __user *)ukey, fshared, &key) != 0) {
		kill_guest(lg, "bad registered DMA buffer");
		goto unlock;
	}
	list_for_each_entry(i, &dma_hash[hash(&key)], list) {
		if (key_eq(&key, &i->key) && i->guestid == lg->guestid) {
			unsigned int j;
			for (j = 0; j < i->num_dmas; j++) {
				struct lguest_dma dma;

				ret = i->dmas + j * sizeof(struct lguest_dma);
				lgread(lg, &dma, ret, sizeof(dma));
				if (dma.used_len == 0)
					break;
			}
			*interrupt = i->interrupt;
			break;
		}
	}
unlock:
	up_read(fshared);
	mutex_unlock(&lguest_lock);
	return ret;
}

