/*
   drbd_bitmap.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2004-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 2004-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2004-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/bitops.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/drbd.h>
#include <asm/kmap_types.h>
#include "drbd_int.h"

/* OPAQUE outside this file!
 * interface defined in drbd_int.h

 * convention:
 * function name drbd_bm_... => used elsewhere, "public".
 * function name      bm_... => internal to implementation, "private".

 * Note that since find_first_bit returns int, at the current granularity of
 * the bitmap (4KB per byte), this implementation "only" supports up to
 * 1<<(32+12) == 16 TB...
 */

/*
 * NOTE
 *  Access to the *bm_pages is protected by bm_lock.
 *  It is safe to read the other members within the lock.
 *
 *  drbd_bm_set_bits is called from bio_endio callbacks,
 *  We may be called with irq already disabled,
 *  so we need spin_lock_irqsave().
 *  And we need the kmap_atomic.
 */
struct drbd_bitmap {
	struct page **bm_pages;
	spinlock_t bm_lock;
	/* WARNING unsigned long bm_*:
	 * 32bit number of bit offset is just enough for 512 MB bitmap.
	 * it will blow up if we make the bitmap bigger...
	 * not that it makes much sense to have a bitmap that large,
	 * rather change the granularity to 16k or 64k or something.
	 * (that implies other problems, however...)
	 */
	unsigned long bm_set;       /* nr of set bits; THINK maybe atomic_t? */
	unsigned long bm_bits;
	size_t   bm_words;
	size_t   bm_number_of_pages;
	sector_t bm_dev_capacity;
	struct semaphore bm_change; /* serializes resize operations */

	atomic_t bm_async_io;
	wait_queue_head_t bm_io_wait;

	unsigned long  bm_flags;

	/* debugging aid, in case we are still racy somewhere */
	char          *bm_why;
	struct task_struct *bm_task;
};

/* definition of bits in bm_flags */
#define BM_LOCKED       0
#define BM_MD_IO_ERROR  1
#define BM_P_VMALLOCED  2

static int bm_is_locked(struct drbd_bitmap *b)
{
	return test_bit(BM_LOCKED, &b->bm_flags);
}

#define bm_print_lock_info(m) __bm_print_lock_info(m, __func__)
static void __bm_print_lock_info(struct drbd_conf *mdev, const char *func)
{
	struct drbd_bitmap *b = mdev->bitmap;
	if (!__ratelimit(&drbd_ratelimit_state))
		return;
	dev_err(DEV, "FIXME %s in %s, bitmap locked for '%s' by %s\n",
	    current == mdev->receiver.task ? "receiver" :
	    current == mdev->asender.task  ? "asender"  :
	    current == mdev->worker.task   ? "worker"   : current->comm,
	    func, b->bm_why ?: "?",
	    b->bm_task == mdev->receiver.task ? "receiver" :
	    b->bm_task == mdev->asender.task  ? "asender"  :
	    b->bm_task == mdev->worker.task   ? "worker"   : "?");
}

void drbd_bm_lock(struct drbd_conf *mdev, char *why)
{
	struct drbd_bitmap *b = mdev->bitmap;
	int trylock_failed;

	if (!b) {
		dev_err(DEV, "FIXME no bitmap in drbd_bm_lock!?\n");
		return;
	}

	trylock_failed = down_trylock(&b->bm_change);

	if (trylock_failed) {
		dev_warn(DEV, "%s going to '%s' but bitmap already locked for '%s' by %s\n",
		    current == mdev->receiver.task ? "receiver" :
		    current == mdev->asender.task  ? "asender"  :
		    current == mdev->worker.task   ? "worker"   : current->comm,
		    why, b->bm_why ?: "?",
		    b->bm_task == mdev->receiver.task ? "receiver" :
		    b->bm_task == mdev->asender.task  ? "asender"  :
		    b->bm_task == mdev->worker.task   ? "worker"   : "?");
		down(&b->bm_change);
	}
	if (__test_and_set_bit(BM_LOCKED, &b->bm_flags))
		dev_err(DEV, "FIXME bitmap already locked in bm_lock\n");

	b->bm_why  = why;
	b->bm_task = current;
}

void drbd_bm_unlock(struct drbd_conf *mdev)
{
	struct drbd_bitmap *b = mdev->bitmap;
	if (!b) {
		dev_err(DEV, "FIXME no bitmap in drbd_bm_unlock!?\n");
		return;
	}

	if (!__test_and_clear_bit(BM_LOCKED, &mdev->bitmap->bm_flags))
		dev_err(DEV, "FIXME bitmap not locked in bm_unlock\n");

	b->bm_why  = NULL;
	b->bm_task = NULL;
	up(&b->bm_change);
}

/* word offset to long pointer */
static unsigned long *__bm_map_paddr(struct drbd_bitmap *b, unsigned long offset, const enum km_type km)
{
	struct page *page;
	unsigned long page_nr;

	/* page_nr = (word*sizeof(long)) >> PAGE_SHIFT; */
	page_nr = offset >> (PAGE_SHIFT - LN2_BPL + 3);
	BUG_ON(page_nr >= b->bm_number_of_pages);
	page = b->bm_pages[page_nr];

	return (unsigned long *) kmap_atomic(page, km);
}

static unsigned long * bm_map_paddr(struct drbd_bitmap *b, unsigned long offset)
{
	return __bm_map_paddr(b, offset, KM_IRQ1);
}

static void __bm_unmap(unsigned long *p_addr, const enum km_type km)
{
	kunmap_atomic(p_addr, km);
};

static void bm_unmap(unsigned long *p_addr)
{
	return __bm_unmap(p_addr, KM_IRQ1);
}

/* long word offset of _bitmap_ sector */
#define S2W(s)	((s)<<(BM_EXT_SHIFT-BM_BLOCK_SHIFT-LN2_BPL))
/* word offset from start of bitmap to word number _in_page_
 * modulo longs per page
#define MLPP(X) ((X) % (PAGE_SIZE/sizeof(long))
 hm, well, Philipp thinks gcc might not optimze the % into & (... - 1)
 so do it explicitly:
 */
#define MLPP(X) ((X) & ((PAGE_SIZE/sizeof(long))-1))

/* Long words per page */
#define LWPP (PAGE_SIZE/sizeof(long))

/*
 * actually most functions herein should take a struct drbd_bitmap*, not a
 * struct drbd_conf*, but for the debug macros I like to have the mdev around
 * to be able to report device specific.
 */

static void bm_free_pages(struct page **pages, unsigned long number)
{
	unsigned long i;
	if (!pages)
		return;

	for (i = 0; i < number; i++) {
		if (!pages[i]) {
			printk(KERN_ALERT "drbd: bm_free_pages tried to free "
					  "a NULL pointer; i=%lu n=%lu\n",
					  i, number);
			continue;
		}
		__free_page(pages[i]);
		pages[i] = NULL;
	}
}

static void bm_vk_free(void *ptr, int v)
{
	if (v)
		vfree(ptr);
	else
		kfree(ptr);
}

/*
 * "have" and "want" are NUMBER OF PAGES.
 */
static struct page **bm_realloc_pages(struct drbd_bitmap *b, unsigned long want)
{
	struct page **old_pages = b->bm_pages;
	struct page **new_pages, *page;
	unsigned int i, bytes, vmalloced = 0;
	unsigned long have = b->bm_number_of_pages;

	BUG_ON(have == 0 && old_pages != NULL);
	BUG_ON(have != 0 && old_pages == NULL);

	if (have == want)
		return old_pages;

	/* Trying kmalloc first, falling back to vmalloc.
	 * GFP_KERNEL is ok, as this is done when a lower level disk is
	 * "attached" to the drbd.  Context is receiver thread or cqueue
	 * thread.  As we have no disk yet, we are not in the IO path,
	 * not even the IO path of the peer. */
	bytes = sizeof(struct page *)*want;
	new_pages = kmalloc(bytes, GFP_KERNEL);
	if (!new_pages) {
		new_pages = vmalloc(bytes);
		if (!new_pages)
			return NULL;
		vmalloced = 1;
	}

	memset(new_pages, 0, bytes);
	if (want >= have) {
		for (i = 0; i < have; i++)
			new_pages[i] = old_pages[i];
		for (; i < want; i++) {
			page = alloc_page(GFP_HIGHUSER);
			if (!page) {
				bm_free_pages(new_pages + have, i - have);
				bm_vk_free(new_pages, vmalloced);
				return NULL;
			}
			new_pages[i] = page;
		}
	} else {
		for (i = 0; i < want; i++)
			new_pages[i] = old_pages[i];
		/* NOT HERE, we are outside the spinlock!
		bm_free_pages(old_pages + want, have - want);
		*/
	}

	if (vmalloced)
		set_bit(BM_P_VMALLOCED, &b->bm_flags);
	else
		clear_bit(BM_P_VMALLOCED, &b->bm_flags);

	return new_pages;
}

/*
 * called on driver init only. TODO call when a device is created.
 * allocates the drbd_bitmap, and stores it in mdev->bitmap.
 */
int drbd_bm_init(struct drbd_conf *mdev)
{
	struct drbd_bitmap *b = mdev->bitmap;
	WARN_ON(b != NULL);
	b = kzalloc(sizeof(struct drbd_bitmap), GFP_KERNEL);
	if (!b)
		return -ENOMEM;
	spin_lock_init(&b->bm_lock);
	init_MUTEX(&b->bm_change);
	init_waitqueue_head(&b->bm_io_wait);

	mdev->bitmap = b;

	return 0;
}

sector_t drbd_bm_capacity(struct drbd_conf *mdev)
{
	ERR_IF(!mdev->bitmap) return 0;
	return mdev->bitmap->bm_dev_capacity;
}

/* called on driver unload. TODO: call when a device is destroyed.
 */
void drbd_bm_cleanup(struct drbd_conf *mdev)
{
	ERR_IF (!mdev->bitmap) return;
	bm_free_pages(mdev->bitmap->bm_pages, mdev->bitmap->bm_number_of_pages);
	bm_vk_free(mdev->bitmap->bm_pages, test_bit(BM_P_VMALLOCED, &mdev->bitmap->bm_flags));
	kfree(mdev->bitmap);
	mdev->bitmap = NULL;
}

/*
 * since (b->bm_bits % BITS_PER_LONG) != 0,
 * this masks out the remaining bits.
 * Returns the number of bits cleared.
 */
static int bm_clear_surplus(struct drbd_bitmap *b)
{
	const unsigned long mask = (1UL << (b->bm_bits & (BITS_PER_LONG-1))) - 1;
	size_t w = b->bm_bits >> LN2_BPL;
	int cleared = 0;
	unsigned long *p_addr, *bm;

	p_addr = bm_map_paddr(b, w);
	bm = p_addr + MLPP(w);
	if (w < b->bm_words) {
		cleared = hweight_long(*bm & ~mask);
		*bm &= mask;
		w++; bm++;
	}

	if (w < b->bm_words) {
		cleared += hweight_long(*bm);
		*bm = 0;
	}
	bm_unmap(p_addr);
	return cleared;
}

static void bm_set_surplus(struct drbd_bitmap *b)
{
	const unsigned long mask = (1UL << (b->bm_bits & (BITS_PER_LONG-1))) - 1;
	size_t w = b->bm_bits >> LN2_BPL;
	unsigned long *p_addr, *bm;

	p_addr = bm_map_paddr(b, w);
	bm = p_addr + MLPP(w);
	if (w < b->bm_words) {
		*bm |= ~mask;
		bm++; w++;
	}

	if (w < b->bm_words) {
		*bm = ~(0UL);
	}
	bm_unmap(p_addr);
}

static unsigned long __bm_count_bits(struct drbd_bitmap *b, const int swap_endian)
{
	unsigned long *p_addr, *bm, offset = 0;
	unsigned long bits = 0;
	unsigned long i, do_now;

	while (offset < b->bm_words) {
		i = do_now = min_t(size_t, b->bm_words-offset, LWPP);
		p_addr = __bm_map_paddr(b, offset, KM_USER0);
		bm = p_addr + MLPP(offset);
		while (i--) {
#ifndef __LITTLE_ENDIAN
			if (swap_endian)
				*bm = lel_to_cpu(*bm);
#endif
			bits += hweight_long(*bm++);
		}
		__bm_unmap(p_addr, KM_USER0);
		offset += do_now;
		cond_resched();
	}

	return bits;
}

static unsigned long bm_count_bits(struct drbd_bitmap *b)
{
	return __bm_count_bits(b, 0);
}

static unsigned long bm_count_bits_swap_endian(struct drbd_bitmap *b)
{
	return __bm_count_bits(b, 1);
}

/* offset and len in long words.*/
static void bm_memset(struct drbd_bitmap *b, size_t offset, int c, size_t len)
{
	unsigned long *p_addr, *bm;
	size_t do_now, end;

#define BM_SECTORS_PER_BIT (BM_BLOCK_SIZE/512)

	end = offset + len;

	if (end > b->bm_words) {
		printk(KERN_ALERT "drbd: bm_memset end > bm_words\n");
		return;
	}

	while (offset < end) {
		do_now = min_t(size_t, ALIGN(offset + 1, LWPP), end) - offset;
		p_addr = bm_map_paddr(b, offset);
		bm = p_addr + MLPP(offset);
		if (bm+do_now > p_addr + LWPP) {
			printk(KERN_ALERT "drbd: BUG BUG BUG! p_addr:%p bm:%p do_now:%d\n",
			       p_addr, bm, (int)do_now);
			break; /* breaks to after catch_oob_access_end() only! */
		}
		memset(bm, c, do_now * sizeof(long));
		bm_unmap(p_addr);
		offset += do_now;
	}
}

/*
 * make sure the bitmap has enough room for the attached storage,
 * if necessary, resize.
 * called whenever we may have changed the device size.
 * returns -ENOMEM if we could not allocate enough memory, 0 on success.
 * In case this is actually a resize, we copy the old bitmap into the new one.
 * Otherwise, the bitmap is initialized to all bits set.
 */
int drbd_bm_resize(struct drbd_conf *mdev, sector_t capacity)
{
	struct drbd_bitmap *b = mdev->bitmap;
	unsigned long bits, words, owords, obits, *p_addr, *bm;
	unsigned long want, have, onpages; /* number of pages */
	struct page **npages, **opages = NULL;
	int err = 0, growing;
	int opages_vmalloced;

	ERR_IF(!b) return -ENOMEM;

	drbd_bm_lock(mdev, "resize");

	dev_info(DEV, "drbd_bm_resize called with capacity == %llu\n",
			(unsigned long long)capacity);

	if (capacity == b->bm_dev_capacity)
		goto out;

	opages_vmalloced = test_bit(BM_P_VMALLOCED, &b->bm_flags);

	if (capacity == 0) {
		spin_lock_irq(&b->bm_lock);
		opages = b->bm_pages;
		onpages = b->bm_number_of_pages;
		owords = b->bm_words;
		b->bm_pages = NULL;
		b->bm_number_of_pages =
		b->bm_set   =
		b->bm_bits  =
		b->bm_words =
		b->bm_dev_capacity = 0;
		spin_unlock_irq(&b->bm_lock);
		bm_free_pages(opages, onpages);
		bm_vk_free(opages, opages_vmalloced);
		goto out;
	}
	bits  = BM_SECT_TO_BIT(ALIGN(capacity, BM_SECT_PER_BIT));

	/* if we would use
	   words = ALIGN(bits,BITS_PER_LONG) >> LN2_BPL;
	   a 32bit host could present the wrong number of words
	   to a 64bit host.
	*/
	words = ALIGN(bits, 64) >> LN2_BPL;

	if (get_ldev(mdev)) {
		D_ASSERT((u64)bits <= (((u64)mdev->ldev->md.md_size_sect-MD_BM_OFFSET) << 12));
		put_ldev(mdev);
	}

	/* one extra long to catch off by one errors */
	want = ALIGN((words+1)*sizeof(long), PAGE_SIZE) >> PAGE_SHIFT;
	have = b->bm_number_of_pages;
	if (want == have) {
		D_ASSERT(b->bm_pages != NULL);
		npages = b->bm_pages;
	} else {
		if (FAULT_ACTIVE(mdev, DRBD_FAULT_BM_ALLOC))
			npages = NULL;
		else
			npages = bm_realloc_pages(b, want);
	}

	if (!npages) {
		err = -ENOMEM;
		goto out;
	}

	spin_lock_irq(&b->bm_lock);
	opages = b->bm_pages;
	owords = b->bm_words;
	obits  = b->bm_bits;

	growing = bits > obits;
	if (opages)
		bm_set_surplus(b);

	b->bm_pages = npages;
	b->bm_number_of_pages = want;
	b->bm_bits  = bits;
	b->bm_words = words;
	b->bm_dev_capacity = capacity;

	if (growing) {
		bm_memset(b, owords, 0xff, words-owords);
		b->bm_set += bits - obits;
	}

	if (want < have) {
		/* implicit: (opages != NULL) && (opages != npages) */
		bm_free_pages(opages + want, have - want);
	}

	p_addr = bm_map_paddr(b, words);
	bm = p_addr + MLPP(words);
	*bm = DRBD_MAGIC;
	bm_unmap(p_addr);

	(void)bm_clear_surplus(b);

	spin_unlock_irq(&b->bm_lock);
	if (opages != npages)
		bm_vk_free(opages, opages_vmalloced);
	if (!growing)
		b->bm_set = bm_count_bits(b);
	dev_info(DEV, "resync bitmap: bits=%lu words=%lu\n", bits, words);

 out:
	drbd_bm_unlock(mdev);
	return err;
}

/* inherently racy:
 * if not protected by other means, return value may be out of date when
 * leaving this function...
 * we still need to lock it, since it is important that this returns
 * bm_set == 0 precisely.
 *
 * maybe bm_set should be atomic_t ?
 */
static unsigned long _drbd_bm_total_weight(struct drbd_conf *mdev)
{
	struct drbd_bitmap *b = mdev->bitmap;
	unsigned long s;
	unsigned long flags;

	ERR_IF(!b) return 0;
	ERR_IF(!b->bm_pages) return 0;

	spin_lock_irqsave(&b->bm_lock, flags);
	s = b->bm_set;
	spin_unlock_irqrestore(&b->bm_lock, flags);

	return s;
}

unsigned long drbd_bm_total_weight(struct drbd_conf *mdev)
{
	unsigned long s;
	/* if I don't have a disk, I don't know about out-of-sync status */
	if (!get_ldev_if_state(mdev, D_NEGOTIATING))
		return 0;
	s = _drbd_bm_total_weight(mdev);
	put_ldev(mdev);
	return s;
}

size_t drbd_bm_words(struct drbd_conf *mdev)
{
	struct drbd_bitmap *b = mdev->bitmap;
	ERR_IF(!b) return 0;
	ERR_IF(!b->bm_pages) return 0;

	return b->bm_words;
}

unsigned long drbd_bm_bits(struct drbd_conf *mdev)
{
	struct drbd_bitmap *b = mdev->bitmap;
	ERR_IF(!b) return 0;

	return b->bm_bits;
}

/* merge number words from buffer into the bitmap starting at offset.
 * buffer[i] is expected to be little endian unsigned long.
 * bitmap must be locked by drbd_bm_lock.
 * currently only used from receive_bitmap.
 */
void drbd_bm_merge_lel(struct drbd_conf *mdev, size_t offset, size_t number,
			unsigned long *buffer)
{
	struct drbd_bitmap *b = mdev->bitmap;
	unsigned long *p_addr, *bm;
	unsigned long word, bits;
	size_t end, do_now;

	end = offset + number;

	ERR_IF(!b) return;
	ERR_IF(!b->bm_pages) return;
	if (number == 0)
		return;
	WARN_ON(offset >= b->bm_words);
	WARN_ON(end    >  b->bm_words);

	spin_lock_irq(&b->bm_lock);
	while (offset < end) {
		do_now = min_t(size_t, ALIGN(offset+1, LWPP), end) - offset;
		p_addr = bm_map_paddr(b, offset);
		bm = p_addr + MLPP(offset);
		offset += do_now;
		while (do_now--) {
			bits = hweight_long(*bm);
			word = *bm | lel_to_cpu(*buffer++);
			*bm++ = word;
			b->bm_set += hweight_long(word) - bits;
		}
		bm_unmap(p_addr);
	}
	/* with 32bit <-> 64bit cross-platform connect
	 * this is only correct for current usage,
	 * where we _know_ that we are 64 bit aligned,
	 * and know that this function is used in this way, too...
	 */
	if (end == b->bm_words)
		b->bm_set -= bm_clear_surplus(b);

	spin_unlock_irq(&b->bm_lock);
}

/* copy number words from the bitmap starting at offset into the buffer.
 * buffer[i] will be little endian unsigned long.
 */
void drbd_bm_get_lel(struct drbd_conf *mdev, size_t offset, size_t number,
		     unsigned long *buffer)
{
	struct drbd_bitmap *b = mdev->bitmap;
	unsigned long *p_addr, *bm;
	size_t end, do_now;

	end = offset + number;

	ERR_IF(!b) return;
	ERR_IF(!b->bm_pages) return;

	spin_lock_irq(&b->bm_lock);
	if ((offset >= b->bm_words) ||
	    (end    >  b->bm_words) ||
	    (number <= 0))
		dev_err(DEV, "offset=%lu number=%lu bm_words=%lu\n",
			(unsigned long)	offset,
			(unsigned long)	number,
			(unsigned long) b->bm_words);
	else {
		while (offset < end) {
			do_now = min_t(size_t, ALIGN(offset+1, LWPP), end) - offset;
			p_addr = bm_map_paddr(b, offset);
			bm = p_addr + MLPP(offset);
			offset += do_now;
			while (do_now--)
				*buffer++ = cpu_to_lel(*bm++);
			bm_unmap(p_addr);
		}
	}
	spin_unlock_irq(&b->bm_lock);
}

/* set all bits in the bitmap */
void drbd_bm_set_all(struct drbd_conf *mdev)
{
	struct drbd_bitmap *b = mdev->bitmap;
	ERR_IF(!b) return;
	ERR_IF(!b->bm_pages) return;

	spin_lock_irq(&b->bm_lock);
	bm_memset(b, 0, 0xff, b->bm_words);
	(void)bm_clear_surplus(b);
	b->bm_set = b->bm_bits;
	spin_unlock_irq(&b->bm_lock);
}

/* clear all bits in the bitmap */
void drbd_bm_clear_all(struct drbd_conf *mdev)
{
	struct drbd_bitmap *b = mdev->bitmap;
	ERR_IF(!b) return;
	ERR_IF(!b->bm_pages) return;

	spin_lock_irq(&b->bm_lock);
	bm_memset(b, 0, 0, b->bm_words);
	b->bm_set = 0;
	spin_unlock_irq(&b->bm_lock);
}

static void bm_async_io_complete(struct bio *bio, int error)
{
	struct drbd_bitmap *b = bio->bi_private;
	int uptodate = bio_flagged(bio, BIO_UPTODATE);


	/* strange behavior of some lower level drivers...
	 * fail the request by clearing the uptodate flag,
	 * but do not return any error?!
	 * do we want to WARN() on this? */
	if (!error && !uptodate)
		error = -EIO;

	if (error) {
		/* doh. what now?
		 * for now, set all bits, and flag MD_IO_ERROR */
		__set_bit(BM_MD_IO_ERROR, &b->bm_flags);
	}
	if (atomic_dec_and_test(&b->bm_async_io))
		wake_up(&b->bm_io_wait);

	bio_put(bio);
}

static void bm_page_io_async(struct drbd_conf *mdev, struct drbd_bitmap *b, int page_nr, int rw) __must_hold(local)
{
	/* we are process context. we always get a bio */
	struct bio *bio = bio_alloc(GFP_KERNEL, 1);
	unsigned int len;
	sector_t on_disk_sector =
		mdev->ldev->md.md_offset + mdev->ldev->md.bm_offset;
	on_disk_sector += ((sector_t)page_nr) << (PAGE_SHIFT-9);

	/* this might happen with very small
	 * flexible external meta data device */
	len = min_t(unsigned int, PAGE_SIZE,
		(drbd_md_last_sector(mdev->ldev) - on_disk_sector + 1)<<9);

	bio->bi_bdev = mdev->ldev->md_bdev;
	bio->bi_sector = on_disk_sector;
	bio_add_page(bio, b->bm_pages[page_nr], len, 0);
	bio->bi_private = b;
	bio->bi_end_io = bm_async_io_complete;

	if (FAULT_ACTIVE(mdev, (rw & WRITE) ? DRBD_FAULT_MD_WR : DRBD_FAULT_MD_RD)) {
		bio->bi_rw |= rw;
		bio_endio(bio, -EIO);
	} else {
		submit_bio(rw, bio);
	}
}

# if defined(__LITTLE_ENDIAN)
	/* nothing to do, on disk == in memory */
# define bm_cpu_to_lel(x) ((void)0)
# else
void bm_cpu_to_lel(struct drbd_bitmap *b)
{
	/* need to cpu_to_lel all the pages ...
	 * this may be optimized by using
	 * cpu_to_lel(-1) == -1 and cpu_to_lel(0) == 0;
	 * the following is still not optimal, but better than nothing */
	unsigned int i;
	unsigned long *p_addr, *bm;
	if (b->bm_set == 0) {
		/* no page at all; avoid swap if all is 0 */
		i = b->bm_number_of_pages;
	} else if (b->bm_set == b->bm_bits) {
		/* only the last page */
		i = b->bm_number_of_pages - 1;
	} else {
		/* all pages */
		i = 0;
	}
	for (; i < b->bm_number_of_pages; i++) {
		p_addr = kmap_atomic(b->bm_pages[i], KM_USER0);
		for (bm = p_addr; bm < p_addr + PAGE_SIZE/sizeof(long); bm++)
			*bm = cpu_to_lel(*bm);
		kunmap_atomic(p_addr, KM_USER0);
	}
}
# endif
/* lel_to_cpu == cpu_to_lel */
# define bm_lel_to_cpu(x) bm_cpu_to_lel(x)

/*
 * bm_rw: read/write the whole bitmap from/to its on disk location.
 */
static int bm_rw(struct drbd_conf *mdev, int rw) __must_hold(local)
{
	struct drbd_bitmap *b = mdev->bitmap;
	/* sector_t sector; */
	int bm_words, num_pages, i;
	unsigned long now;
	char ppb[10];
	int err = 0;

	WARN_ON(!bm_is_locked(b));

	/* no spinlock here, the drbd_bm_lock should be enough! */

	bm_words  = drbd_bm_words(mdev);
	num_pages = (bm_words*sizeof(long) + PAGE_SIZE-1) >> PAGE_SHIFT;

	/* on disk bitmap is little endian */
	if (rw == WRITE)
		bm_cpu_to_lel(b);

	now = jiffies;
	atomic_set(&b->bm_async_io, num_pages);
	__clear_bit(BM_MD_IO_ERROR, &b->bm_flags);

	/* let the layers below us try to merge these bios... */
	for (i = 0; i < num_pages; i++)
		bm_page_io_async(mdev, b, i, rw);

	drbd_blk_run_queue(bdev_get_queue(mdev->ldev->md_bdev));
	wait_event(b->bm_io_wait, atomic_read(&b->bm_async_io) == 0);

	if (test_bit(BM_MD_IO_ERROR, &b->bm_flags)) {
		dev_alert(DEV, "we had at least one MD IO ERROR during bitmap IO\n");
		drbd_chk_io_error(mdev, 1, TRUE);
		err = -EIO;
	}

	now = jiffies;
	if (rw == WRITE) {
		/* swap back endianness */
		bm_lel_to_cpu(b);
		/* flush bitmap to stable storage */
		drbd_md_flush(mdev);
	} else /* rw == READ */ {
		/* just read, if necessary adjust endianness */
		b->bm_set = bm_count_bits_swap_endian(b);
		dev_info(DEV, "recounting of set bits took additional %lu jiffies\n",
		     jiffies - now);
	}
	now = b->bm_set;

	dev_info(DEV, "%s (%lu bits) marked out-of-sync by on disk bit-map.\n",
	     ppsize(ppb, now << (BM_BLOCK_SHIFT-10)), now);

	return err;
}

/**
 * drbd_bm_read() - Read the whole bitmap from its on disk location.
 * @mdev:	DRBD device.
 */
int drbd_bm_read(struct drbd_conf *mdev) __must_hold(local)
{
	return bm_rw(mdev, READ);
}

/**
 * drbd_bm_write() - Write the whole bitmap to its on disk location.
 * @mdev:	DRBD device.
 */
int drbd_bm_write(struct drbd_conf *mdev) __must_hold(local)
{
	return bm_rw(mdev, WRITE);
}

/**
 * drbd_bm_write_sect: Writes a 512 (MD_SECTOR_SIZE) byte piece of the bitmap
 * @mdev:	DRBD device.
 * @enr:	Extent number in the resync lru (happens to be sector offset)
 *
 * The BM_EXT_SIZE is on purpose exactly the amount of the bitmap covered
 * by a single sector write. Therefore enr == sector offset from the
 * start of the bitmap.
 */
int drbd_bm_write_sect(struct drbd_conf *mdev, unsigned long enr) __must_hold(local)
{
	sector_t on_disk_sector = enr + mdev->ldev->md.md_offset
				      + mdev->ldev->md.bm_offset;
	int bm_words, num_words, offset;
	int err = 0;

	mutex_lock(&mdev->md_io_mutex);
	bm_words  = drbd_bm_words(mdev);
	offset    = S2W(enr);	/* word offset into bitmap */
	num_words = min(S2W(1), bm_words - offset);
	if (num_words < S2W(1))
		memset(page_address(mdev->md_io_page), 0, MD_SECTOR_SIZE);
	drbd_bm_get_lel(mdev, offset, num_words,
			page_address(mdev->md_io_page));
	if (!drbd_md_sync_page_io(mdev, mdev->ldev, on_disk_sector, WRITE)) {
		int i;
		err = -EIO;
		dev_err(DEV, "IO ERROR writing bitmap sector %lu "
		    "(meta-disk sector %llus)\n",
		    enr, (unsigned long long)on_disk_sector);
		drbd_chk_io_error(mdev, 1, TRUE);
		for (i = 0; i < AL_EXT_PER_BM_SECT; i++)
			drbd_bm_ALe_set_all(mdev, enr*AL_EXT_PER_BM_SECT+i);
	}
	mdev->bm_writ_cnt++;
	mutex_unlock(&mdev->md_io_mutex);
	return err;
}

/* NOTE
 * find_first_bit returns int, we return unsigned long.
 * should not make much difference anyways, but ...
 *
 * this returns a bit number, NOT a sector!
 */
#define BPP_MASK ((1UL << (PAGE_SHIFT+3)) - 1)
static unsigned long __bm_find_next(struct drbd_conf *mdev, unsigned long bm_fo,
	const int find_zero_bit, const enum km_type km)
{
	struct drbd_bitmap *b = mdev->bitmap;
	unsigned long i = -1UL;
	unsigned long *p_addr;
	unsigned long bit_offset; /* bit offset of the mapped page. */

	if (bm_fo > b->bm_bits) {
		dev_err(DEV, "bm_fo=%lu bm_bits=%lu\n", bm_fo, b->bm_bits);
	} else {
		while (bm_fo < b->bm_bits) {
			unsigned long offset;
			bit_offset = bm_fo & ~BPP_MASK; /* bit offset of the page */
			offset = bit_offset >> LN2_BPL;    /* word offset of the page */
			p_addr = __bm_map_paddr(b, offset, km);

			if (find_zero_bit)
				i = find_next_zero_bit(p_addr, PAGE_SIZE*8, bm_fo & BPP_MASK);
			else
				i = find_next_bit(p_addr, PAGE_SIZE*8, bm_fo & BPP_MASK);

			__bm_unmap(p_addr, km);
			if (i < PAGE_SIZE*8) {
				i = bit_offset + i;
				if (i >= b->bm_bits)
					break;
				goto found;
			}
			bm_fo = bit_offset + PAGE_SIZE*8;
		}
		i = -1UL;
	}
 found:
	return i;
}

static unsigned long bm_find_next(struct drbd_conf *mdev,
	unsigned long bm_fo, const int find_zero_bit)
{
	struct drbd_bitmap *b = mdev->bitmap;
	unsigned long i = -1UL;

	ERR_IF(!b) return i;
	ERR_IF(!b->bm_pages) return i;

	spin_lock_irq(&b->bm_lock);
	if (bm_is_locked(b))
		bm_print_lock_info(mdev);

	i = __bm_find_next(mdev, bm_fo, find_zero_bit, KM_IRQ1);

	spin_unlock_irq(&b->bm_lock);
	return i;
}

unsigned long drbd_bm_find_next(struct drbd_conf *mdev, unsigned long bm_fo)
{
	return bm_find_next(mdev, bm_fo, 0);
}

#if 0
/* not yet needed for anything. */
unsigned long drbd_bm_find_next_zero(struct drbd_conf *mdev, unsigned long bm_fo)
{
	return bm_find_next(mdev, bm_fo, 1);
}
#endif

/* does not spin_lock_irqsave.
 * you must take drbd_bm_lock() first */
unsigned long _drbd_bm_find_next(struct drbd_conf *mdev, unsigned long bm_fo)
{
	/* WARN_ON(!bm_is_locked(mdev)); */
	return __bm_find_next(mdev, bm_fo, 0, KM_USER1);
}

unsigned long _drbd_bm_find_next_zero(struct drbd_conf *mdev, unsigned long bm_fo)
{
	/* WARN_ON(!bm_is_locked(mdev)); */
	return __bm_find_next(mdev, bm_fo, 1, KM_USER1);
}

/* returns number of bits actually changed.
 * for val != 0, we change 0 -> 1, return code positive
 * for val == 0, we change 1 -> 0, return code negative
 * wants bitnr, not sector.
 * expected to be called for only a few bits (e - s about BITS_PER_LONG).
 * Must hold bitmap lock already. */
int __bm_change_bits_to(struct drbd_conf *mdev, const unsigned long s,
	unsigned long e, int val, const enum km_type km)
{
	struct drbd_bitmap *b = mdev->bitmap;
	unsigned long *p_addr = NULL;
	unsigned long bitnr;
	unsigned long last_page_nr = -1UL;
	int c = 0;

	if (e >= b->bm_bits) {
		dev_err(DEV, "ASSERT FAILED: bit_s=%lu bit_e=%lu bm_bits=%lu\n",
				s, e, b->bm_bits);
		e = b->bm_bits ? b->bm_bits -1 : 0;
	}
	for (bitnr = s; bitnr <= e; bitnr++) {
		unsigned long offset = bitnr>>LN2_BPL;
		unsigned long page_nr = offset >> (PAGE_SHIFT - LN2_BPL + 3);
		if (page_nr != last_page_nr) {
			if (p_addr)
				__bm_unmap(p_addr, km);
			p_addr = __bm_map_paddr(b, offset, km);
			last_page_nr = page_nr;
		}
		if (val)
			c += (0 == __test_and_set_bit(bitnr & BPP_MASK, p_addr));
		else
			c -= (0 != __test_and_clear_bit(bitnr & BPP_MASK, p_addr));
	}
	if (p_addr)
		__bm_unmap(p_addr, km);
	b->bm_set += c;
	return c;
}

/* returns number of bits actually changed.
 * for val != 0, we change 0 -> 1, return code positive
 * for val == 0, we change 1 -> 0, return code negative
 * wants bitnr, not sector */
int bm_change_bits_to(struct drbd_conf *mdev, const unsigned long s,
	const unsigned long e, int val)
{
	unsigned long flags;
	struct drbd_bitmap *b = mdev->bitmap;
	int c = 0;

	ERR_IF(!b) return 1;
	ERR_IF(!b->bm_pages) return 0;

	spin_lock_irqsave(&b->bm_lock, flags);
	if (bm_is_locked(b))
		bm_print_lock_info(mdev);

	c = __bm_change_bits_to(mdev, s, e, val, KM_IRQ1);

	spin_unlock_irqrestore(&b->bm_lock, flags);
	return c;
}

/* returns number of bits changed 0 -> 1 */
int drbd_bm_set_bits(struct drbd_conf *mdev, const unsigned long s, const unsigned long e)
{
	return bm_change_bits_to(mdev, s, e, 1);
}

/* returns number of bits changed 1 -> 0 */
int drbd_bm_clear_bits(struct drbd_conf *mdev, const unsigned long s, const unsigned long e)
{
	return -bm_change_bits_to(mdev, s, e, 0);
}

/* sets all bits in full words,
 * from first_word up to, but not including, last_word */
static inline void bm_set_full_words_within_one_page(struct drbd_bitmap *b,
		int page_nr, int first_word, int last_word)
{
	int i;
	int bits;
	unsigned long *paddr = kmap_atomic(b->bm_pages[page_nr], KM_USER0);
	for (i = first_word; i < last_word; i++) {
		bits = hweight_long(paddr[i]);
		paddr[i] = ~0UL;
		b->bm_set += BITS_PER_LONG - bits;
	}
	kunmap_atomic(paddr, KM_USER0);
}

/* Same thing as drbd_bm_set_bits, but without taking the spin_lock_irqsave.
 * You must first drbd_bm_lock().
 * Can be called to set the whole bitmap in one go.
 * Sets bits from s to e _inclusive_. */
void _drbd_bm_set_bits(struct drbd_conf *mdev, const unsigned long s, const unsigned long e)
{
	/* First set_bit from the first bit (s)
	 * up to the next long boundary (sl),
	 * then assign full words up to the last long boundary (el),
	 * then set_bit up to and including the last bit (e).
	 *
	 * Do not use memset, because we must account for changes,
	 * so we need to loop over the words with hweight() anyways.
	 */
	unsigned long sl = ALIGN(s,BITS_PER_LONG);
	unsigned long el = (e+1) & ~((unsigned long)BITS_PER_LONG-1);
	int first_page;
	int last_page;
	int page_nr;
	int first_word;
	int last_word;

	if (e - s <= 3*BITS_PER_LONG) {
		/* don't bother; el and sl may even be wrong. */
		__bm_change_bits_to(mdev, s, e, 1, KM_USER0);
		return;
	}

	/* difference is large enough that we can trust sl and el */

	/* bits filling the current long */
	if (sl)
		__bm_change_bits_to(mdev, s, sl-1, 1, KM_USER0);

	first_page = sl >> (3 + PAGE_SHIFT);
	last_page = el >> (3 + PAGE_SHIFT);

	/* MLPP: modulo longs per page */
	/* LWPP: long words per page */
	first_word = MLPP(sl >> LN2_BPL);
	last_word = LWPP;

	/* first and full pages, unless first page == last page */
	for (page_nr = first_page; page_nr < last_page; page_nr++) {
		bm_set_full_words_within_one_page(mdev->bitmap, page_nr, first_word, last_word);
		cond_resched();
		first_word = 0;
	}

	/* last page (respectively only page, for first page == last page) */
	last_word = MLPP(el >> LN2_BPL);
	bm_set_full_words_within_one_page(mdev->bitmap, last_page, first_word, last_word);

	/* possibly trailing bits.
	 * example: (e & 63) == 63, el will be e+1.
	 * if that even was the very last bit,
	 * it would trigger an assert in __bm_change_bits_to()
	 */
	if (el <= e)
		__bm_change_bits_to(mdev, el, e, 1, KM_USER0);
}

/* returns bit state
 * wants bitnr, NOT sector.
 * inherently racy... area needs to be locked by means of {al,rs}_lru
 *  1 ... bit set
 *  0 ... bit not set
 * -1 ... first out of bounds access, stop testing for bits!
 */
int drbd_bm_test_bit(struct drbd_conf *mdev, const unsigned long bitnr)
{
	unsigned long flags;
	struct drbd_bitmap *b = mdev->bitmap;
	unsigned long *p_addr;
	int i;

	ERR_IF(!b) return 0;
	ERR_IF(!b->bm_pages) return 0;

	spin_lock_irqsave(&b->bm_lock, flags);
	if (bm_is_locked(b))
		bm_print_lock_info(mdev);
	if (bitnr < b->bm_bits) {
		unsigned long offset = bitnr>>LN2_BPL;
		p_addr = bm_map_paddr(b, offset);
		i = test_bit(bitnr & BPP_MASK, p_addr) ? 1 : 0;
		bm_unmap(p_addr);
	} else if (bitnr == b->bm_bits) {
		i = -1;
	} else { /* (bitnr > b->bm_bits) */
		dev_err(DEV, "bitnr=%lu > bm_bits=%lu\n", bitnr, b->bm_bits);
		i = 0;
	}

	spin_unlock_irqrestore(&b->bm_lock, flags);
	return i;
}

/* returns number of bits set in the range [s, e] */
int drbd_bm_count_bits(struct drbd_conf *mdev, const unsigned long s, const unsigned long e)
{
	unsigned long flags;
	struct drbd_bitmap *b = mdev->bitmap;
	unsigned long *p_addr = NULL, page_nr = -1;
	unsigned long bitnr;
	int c = 0;
	size_t w;

	/* If this is called without a bitmap, that is a bug.  But just to be
	 * robust in case we screwed up elsewhere, in that case pretend there
	 * was one dirty bit in the requested area, so we won't try to do a
	 * local read there (no bitmap probably implies no disk) */
	ERR_IF(!b) return 1;
	ERR_IF(!b->bm_pages) return 1;

	spin_lock_irqsave(&b->bm_lock, flags);
	if (bm_is_locked(b))
		bm_print_lock_info(mdev);
	for (bitnr = s; bitnr <= e; bitnr++) {
		w = bitnr >> LN2_BPL;
		if (page_nr != w >> (PAGE_SHIFT - LN2_BPL + 3)) {
			page_nr = w >> (PAGE_SHIFT - LN2_BPL + 3);
			if (p_addr)
				bm_unmap(p_addr);
			p_addr = bm_map_paddr(b, w);
		}
		ERR_IF (bitnr >= b->bm_bits) {
			dev_err(DEV, "bitnr=%lu bm_bits=%lu\n", bitnr, b->bm_bits);
		} else {
			c += (0 != test_bit(bitnr - (page_nr << (PAGE_SHIFT+3)), p_addr));
		}
	}
	if (p_addr)
		bm_unmap(p_addr);
	spin_unlock_irqrestore(&b->bm_lock, flags);
	return c;
}


/* inherently racy...
 * return value may be already out-of-date when this function returns.
 * but the general usage is that this is only use during a cstate when bits are
 * only cleared, not set, and typically only care for the case when the return
 * value is zero, or we already "locked" this "bitmap extent" by other means.
 *
 * enr is bm-extent number, since we chose to name one sector (512 bytes)
 * worth of the bitmap a "bitmap extent".
 *
 * TODO
 * I think since we use it like a reference count, we should use the real
 * reference count of some bitmap extent element from some lru instead...
 *
 */
int drbd_bm_e_weight(struct drbd_conf *mdev, unsigned long enr)
{
	struct drbd_bitmap *b = mdev->bitmap;
	int count, s, e;
	unsigned long flags;
	unsigned long *p_addr, *bm;

	ERR_IF(!b) return 0;
	ERR_IF(!b->bm_pages) return 0;

	spin_lock_irqsave(&b->bm_lock, flags);
	if (bm_is_locked(b))
		bm_print_lock_info(mdev);

	s = S2W(enr);
	e = min((size_t)S2W(enr+1), b->bm_words);
	count = 0;
	if (s < b->bm_words) {
		int n = e-s;
		p_addr = bm_map_paddr(b, s);
		bm = p_addr + MLPP(s);
		while (n--)
			count += hweight_long(*bm++);
		bm_unmap(p_addr);
	} else {
		dev_err(DEV, "start offset (%d) too large in drbd_bm_e_weight\n", s);
	}
	spin_unlock_irqrestore(&b->bm_lock, flags);
	return count;
}

/* set all bits covered by the AL-extent al_enr */
unsigned long drbd_bm_ALe_set_all(struct drbd_conf *mdev, unsigned long al_enr)
{
	struct drbd_bitmap *b = mdev->bitmap;
	unsigned long *p_addr, *bm;
	unsigned long weight;
	int count, s, e, i, do_now;
	ERR_IF(!b) return 0;
	ERR_IF(!b->bm_pages) return 0;

	spin_lock_irq(&b->bm_lock);
	if (bm_is_locked(b))
		bm_print_lock_info(mdev);
	weight = b->bm_set;

	s = al_enr * BM_WORDS_PER_AL_EXT;
	e = min_t(size_t, s + BM_WORDS_PER_AL_EXT, b->bm_words);
	/* assert that s and e are on the same page */
	D_ASSERT((e-1) >> (PAGE_SHIFT - LN2_BPL + 3)
	      ==  s    >> (PAGE_SHIFT - LN2_BPL + 3));
	count = 0;
	if (s < b->bm_words) {
		i = do_now = e-s;
		p_addr = bm_map_paddr(b, s);
		bm = p_addr + MLPP(s);
		while (i--) {
			count += hweight_long(*bm);
			*bm = -1UL;
			bm++;
		}
		bm_unmap(p_addr);
		b->bm_set += do_now*BITS_PER_LONG - count;
		if (e == b->bm_words)
			b->bm_set -= bm_clear_surplus(b);
	} else {
		dev_err(DEV, "start offset (%d) too large in drbd_bm_ALe_set_all\n", s);
	}
	weight = b->bm_set - weight;
	spin_unlock_irq(&b->bm_lock);
	return weight;
}
