/*P:300 The I/O mechanism in lguest is simple yet flexible, allowing the Guest
 * to talk to the Launcher or directly to another Guest.  It uses familiar
 * concepts of DMA and interrupts, plus some neat code stolen from
 * futexes... :*/

/* Copyright (C) 2006 Rusty Russell IBM Corporation
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

/*L:300
 * I/O
 *
 * Getting data in and out of the Guest is quite an art.  There are numerous
 * ways to do it, and they all suck differently.  We try to keep things fairly
 * close to "real" hardware so our Guest's drivers don't look like an alien
 * visitation in the middle of the Linux code, and yet make sure that Guests
 * can talk directly to other Guests, not just the Launcher.
 *
 * To do this, the Guest gives us a key when it binds or sends DMA buffers.
 * The key corresponds to a "physical" address inside the Guest (ie. a virtual
 * address inside the Launcher process).  We don't, however, use this key
 * directly.
 *
 * We want Guests which share memory to be able to DMA to each other: two
 * Launchers can mmap memory the same file, then the Guests can communicate.
 * Fortunately, the futex code provides us with a way to get a "union
 * futex_key" corresponding to the memory lying at a virtual address: if the
 * two processes share memory, the "union futex_key" for that memory will match
 * even if the memory is mapped at different addresses in each.  So we always
 * convert the keys to "union futex_key"s to compare them.
 *
 * Before we dive into this though, we need to look at another set of helper
 * routines used throughout the Host kernel code to access Guest memory.
 :*/
static struct list_head dma_hash[61];

/* An unfortunate side effect of the Linux double-linked list implementation is
 * that there's no good way to statically initialize an array of linked
 * lists. */
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

/*L:330 This is our hash function, using the wonderful Jenkins hash.
 *
 * The futex key is a union with three parts: an unsigned long word, a pointer,
 * and an int "offset".  We could use jhash_2words() which takes three u32s.
 * (Ok, the hash functions are great: the naming sucks though).
 *
 * It's nice to be portable to 64-bit platforms, so we use the more generic
 * jhash2(), which takes an array of u32, the number of u32s, and an initial
 * u32 to roll in.  This is uglier, but breaks down to almost the same code on
 * 32-bit platforms like this one.
 *
 * We want a position in the array, so we modulo ARRAY_SIZE(dma_hash) (ie. 61).
 */
static unsigned int hash(const union futex_key *key)
{
	return jhash2((u32*)&key->both.word,
		      (sizeof(key->both.word)+sizeof(key->both.ptr))/4,
		      key->both.offset)
		% ARRAY_SIZE(dma_hash);
}

/* This is a convenience routine to compare two keys.  It's a much bemoaned C
 * weakness that it doesn't allow '==' on structures or unions, so we have to
 * open-code it like this. */
static inline int key_eq(const union futex_key *a, const union futex_key *b)
{
	return (a->both.word == b->both.word
		&& a->both.ptr == b->both.ptr
		&& a->both.offset == b->both.offset);
}

/*L:360 OK, when we need to actually free up a Guest's DMA array we do several
 * things, so we have a convenient function to do it.
 *
 * The caller must hold a read lock on dmainfo owner's current->mm->mmap_sem
 * for the drop_futex_key_refs(). */
static void unlink_dma(struct lguest_dma_info *dmainfo)
{
	/* You locked this too, right? */
	BUG_ON(!mutex_is_locked(&lguest_lock));
	/* This is how we know that the entry is free. */
	dmainfo->interrupt = 0;
	/* Remove it from the hash table. */
	list_del(&dmainfo->list);
	/* Drop the references we were holding (to the inode or mm). */
	drop_futex_key_refs(&dmainfo->key);
}

/*L:350 This is the routine which we call when the Guest asks to unregister a
 * DMA array attached to a given key.  Returns true if the array was found. */
static int unbind_dma(struct lguest *lg,
		      const union futex_key *key,
		      unsigned long dmas)
{
	int i, ret = 0;

	/* We don't bother with the hash table, just look through all this
	 * Guest's DMA arrays. */
	for (i = 0; i < LGUEST_MAX_DMA; i++) {
		/* In theory it could have more than one array on the same key,
		 * or one array on multiple keys, so we check both */
		if (key_eq(key, &lg->dma[i].key) && dmas == lg->dma[i].dmas) {
			unlink_dma(&lg->dma[i]);
			ret = 1;
			break;
		}
	}
	return ret;
}

/*L:340 BIND_DMA: this is the hypercall which sets up an array of "struct
 * lguest_dma" for receiving I/O.
 *
 * The Guest wants to bind an array of "struct lguest_dma"s to a particular key
 * to receive input.  This only happens when the Guest is setting up a new
 * device, so it doesn't have to be very fast.
 *
 * It returns 1 on a successful registration (it can fail if we hit the limit
 * of registrations for this Guest).
 */
int bind_dma(struct lguest *lg,
	     unsigned long ukey, unsigned long dmas, u16 numdmas, u8 interrupt)
{
	unsigned int i;
	int ret = 0;
	union futex_key key;
	/* Futex code needs the mmap_sem. */
	struct rw_semaphore *fshared = &current->mm->mmap_sem;

	/* Invalid interrupt?  (We could kill the guest here). */
	if (interrupt >= LGUEST_IRQS)
		return 0;

	/* We need to grab the Big Lguest Lock, because other Guests may be
	 * trying to look through this Guest's DMAs to send something while
	 * we're doing this. */
	mutex_lock(&lguest_lock);
	down_read(fshared);
	if (get_futex_key((u32 __user *)ukey, fshared, &key) != 0) {
		kill_guest(lg, "bad dma key %#lx", ukey);
		goto unlock;
	}

	/* We want to keep this key valid once we drop mmap_sem, so we have to
	 * hold a reference. */
	get_futex_key_refs(&key);

	/* If the Guest specified an interrupt of 0, that means they want to
	 * unregister this array of "struct lguest_dma"s. */
	if (interrupt == 0)
		ret = unbind_dma(lg, &key, dmas);
	else {
		/* Look through this Guest's dma array for an unused entry. */
		for (i = 0; i < LGUEST_MAX_DMA; i++) {
			/* If the interrupt is non-zero, the entry is already
			 * used. */
			if (lg->dma[i].interrupt)
				continue;

			/* OK, a free one!  Fill on our details. */
			lg->dma[i].dmas = dmas;
			lg->dma[i].num_dmas = numdmas;
			lg->dma[i].next_dma = 0;
			lg->dma[i].key = key;
			lg->dma[i].guestid = lg->guestid;
			lg->dma[i].interrupt = interrupt;

			/* Now we add it to the hash table: the position
			 * depends on the futex key that we got. */
			list_add(&lg->dma[i].list, &dma_hash[hash(&key)]);
			/* Success! */
			ret = 1;
			goto unlock;
		}
	}
	/* If we didn't find a slot to put the key in, drop the reference
	 * again. */
	drop_futex_key_refs(&key);
unlock:
	/* Unlock and out. */
 	up_read(fshared);
	mutex_unlock(&lguest_lock);
	return ret;
}

/*L:385 Note that our routines to access a different Guest's memory are called
 * lgread_other() and lgwrite_other(): these names emphasize that they are only
 * used when the Guest is *not* the current Guest.
 *
 * The interface for copying from another process's memory is called
 * access_process_vm(), with a final argument of 0 for a read, and 1 for a
 * write.
 *
 * We need lgread_other() to read the destination Guest's "struct lguest_dma"
 * array. */
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

/* "lgwrite()" to another Guest: used to update the destination "used_len" once
 * we've transferred data into the buffer. */
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

/*L:400 This is the generic engine which copies from a source "struct
 * lguest_dma" from this Guest into another Guest's "struct lguest_dma".  The
 * destination Guest's pages have already been mapped, as contained in the
 * pages array.
 *
 * If you're wondering if there's a nice "copy from one process to another"
 * routine, so was I.  But Linux isn't really set up to copy between two
 * unrelated processes, so we have to write it ourselves.
 */
static u32 copy_data(struct lguest *srclg,
		     const struct lguest_dma *src,
		     const struct lguest_dma *dst,
		     struct page *pages[])
{
	unsigned int totlen, si, di, srcoff, dstoff;
	void *maddr = NULL;

	/* We return the total length transferred. */
	totlen = 0;

	/* We keep indexes into the source and destination "struct lguest_dma",
	 * and an offset within each region. */
	si = di = 0;
	srcoff = dstoff = 0;

	/* We loop until the source or destination is exhausted. */
	while (si < LGUEST_MAX_DMA_SECTIONS && src->len[si]
	       && di < LGUEST_MAX_DMA_SECTIONS && dst->len[di]) {
		/* We can only transfer the rest of the src buffer, or as much
		 * as will fit into the destination buffer. */
		u32 len = min(src->len[si] - srcoff, dst->len[di] - dstoff);

		/* For systems using "highmem" we need to use kmap() to access
		 * the page we want.  We often use the same page over and over,
		 * so rather than kmap() it on every loop, we set the maddr
		 * pointer to NULL when we need to move to the next
		 * destination page. */
		if (!maddr)
			maddr = kmap(pages[di]);

		/* Copy directly from (this Guest's) source address to the
		 * destination Guest's kmap()ed buffer.  Note that maddr points
		 * to the start of the page: we need to add the offset of the
		 * destination address and offset within the buffer. */

		/* FIXME: This is not completely portable.  I looked at
		 * copy_to_user_page(), and some arch's seem to need special
		 * flushes.  x86 is fine. */
		if (copy_from_user(maddr + (dst->addr[di] + dstoff)%PAGE_SIZE,
				   (void __user *)src->addr[si], len) != 0) {
			/* If a copy failed, it's the source's fault. */
			kill_guest(srclg, "bad address in sending DMA");
			totlen = 0;
			break;
		}

		/* Increment the total and src & dst offsets */
		totlen += len;
		srcoff += len;
		dstoff += len;

		/* Presumably we reached the end of the src or dest buffers: */
		if (srcoff == src->len[si]) {
			/* Move to the next buffer at offset 0 */
			si++;
			srcoff = 0;
		}
		if (dstoff == dst->len[di]) {
			/* We need to unmap that destination page and reset
			 * maddr ready for the next one. */
			kunmap(pages[di]);
			maddr = NULL;
			di++;
			dstoff = 0;
		}
	}

	/* If we still had a page mapped at the end, unmap now. */
	if (maddr)
		kunmap(pages[di]);

	return totlen;
}

/*L:390 This is how we transfer a "struct lguest_dma" from the source Guest
 * (the current Guest which called SEND_DMA) to another Guest. */
static u32 do_dma(struct lguest *srclg, const struct lguest_dma *src,
		  struct lguest *dstlg, const struct lguest_dma *dst)
{
	int i;
	u32 ret;
	struct page *pages[LGUEST_MAX_DMA_SECTIONS];

	/* We check that both source and destination "struct lguest_dma"s are
	 * within the bounds of the source and destination Guests */
	if (!check_dma_list(dstlg, dst) || !check_dma_list(srclg, src))
		return 0;

	/* We need to map the pages which correspond to each parts of
	 * destination buffer. */
	for (i = 0; i < LGUEST_MAX_DMA_SECTIONS; i++) {
		if (dst->len[i] == 0)
			break;
		/* get_user_pages() is a complicated function, especially since
		 * we only want a single page.  But it works, and returns the
		 * number of pages.  Note that we're holding the destination's
		 * mmap_sem, as get_user_pages() requires. */
		if (get_user_pages(dstlg->tsk, dstlg->mm,
				   dst->addr[i], 1, 1, 1, pages+i, NULL)
		    != 1) {
			/* This means the destination gave us a bogus buffer */
			kill_guest(dstlg, "Error mapping DMA pages");
			ret = 0;
			goto drop_pages;
		}
	}

	/* Now copy the data until we run out of src or dst. */
	ret = copy_data(srclg, src, dst, pages);

drop_pages:
	while (--i >= 0)
		put_page(pages[i]);
	return ret;
}

/*L:380 Transferring data from one Guest to another is not as simple as I'd
 * like.  We've found the "struct lguest_dma_info" bound to the same address as
 * the send, we need to copy into it.
 *
 * This function returns true if the destination array was empty. */
static int dma_transfer(struct lguest *srclg,
			unsigned long udma,
			struct lguest_dma_info *dst)
{
	struct lguest_dma dst_dma, src_dma;
	struct lguest *dstlg;
	u32 i, dma = 0;

	/* From the "struct lguest_dma_info" we found in the hash, grab the
	 * Guest. */
	dstlg = &lguests[dst->guestid];
	/* Read in the source "struct lguest_dma" handed to SEND_DMA. */
	lgread(srclg, &src_dma, udma, sizeof(src_dma));

	/* We need the destination's mmap_sem, and we already hold the source's
	 * mmap_sem for the futex key lookup.  Normally this would suggest that
	 * we could deadlock if the destination Guest was trying to send to
	 * this source Guest at the same time, which is another reason that all
	 * I/O is done under the big lguest_lock. */
	down_read(&dstlg->mm->mmap_sem);

	/* Look through the destination DMA array for an available buffer. */
	for (i = 0; i < dst->num_dmas; i++) {
		/* We keep a "next_dma" pointer which often helps us avoid
		 * looking at lots of previously-filled entries. */
		dma = (dst->next_dma + i) % dst->num_dmas;
		if (!lgread_other(dstlg, &dst_dma,
				  dst->dmas + dma * sizeof(struct lguest_dma),
				  sizeof(dst_dma))) {
			goto fail;
		}
		if (!dst_dma.used_len)
			break;
	}

	/* If we found a buffer, we do the actual data copy. */
	if (i != dst->num_dmas) {
		unsigned long used_lenp;
		unsigned int ret;

		ret = do_dma(srclg, &src_dma, dstlg, &dst_dma);
		/* Put used length in the source "struct lguest_dma"'s used_len
		 * field.  It's a little tricky to figure out where that is,
		 * though. */
		lgwrite_u32(srclg,
			    udma+offsetof(struct lguest_dma, used_len), ret);
		/* Tranferring 0 bytes is OK if the source buffer was empty. */
		if (ret == 0 && src_dma.len[0] != 0)
			goto fail;

		/* The destination Guest might be running on a different CPU:
		 * we have to make sure that it will see the "used_len" field
		 * change to non-zero *after* it sees the data we copied into
		 * the buffer.  Hence a write memory barrier. */
		wmb();
		/* Figuring out where the destination's used_len field for this
		 * "struct lguest_dma" in the array is also a little ugly. */
		used_lenp = dst->dmas
			+ dma * sizeof(struct lguest_dma)
			+ offsetof(struct lguest_dma, used_len);
		lgwrite_other(dstlg, used_lenp, &ret, sizeof(ret));
		/* Move the cursor for next time. */
		dst->next_dma++;
	}
 	up_read(&dstlg->mm->mmap_sem);

	/* We trigger the destination interrupt, even if the destination was
	 * empty and we didn't transfer anything: this gives them a chance to
	 * wake up and refill. */
	set_bit(dst->interrupt, dstlg->irqs_pending);
	/* Wake up the destination process. */
	wake_up_process(dstlg->tsk);
	/* If we passed the last "struct lguest_dma", the receive had no
	 * buffers left. */
	return i == dst->num_dmas;

fail:
	up_read(&dstlg->mm->mmap_sem);
	return 0;
}

/*L:370 This is the counter-side to the BIND_DMA hypercall; the SEND_DMA
 * hypercall.  We find out who's listening, and send to them. */
void send_dma(struct lguest *lg, unsigned long ukey, unsigned long udma)
{
	union futex_key key;
	int empty = 0;
	struct rw_semaphore *fshared = &current->mm->mmap_sem;

again:
	mutex_lock(&lguest_lock);
	down_read(fshared);
	/* Get the futex key for the key the Guest gave us */
	if (get_futex_key((u32 __user *)ukey, fshared, &key) != 0) {
		kill_guest(lg, "bad sending DMA key");
		goto unlock;
	}
	/* Since the key must be a multiple of 4, the futex key uses the lower
	 * bit of the "offset" field (which would always be 0) to indicate a
	 * mapping which is shared with other processes (ie. Guests). */
	if (key.shared.offset & 1) {
		struct lguest_dma_info *i;
		/* Look through the hash for other Guests. */
		list_for_each_entry(i, &dma_hash[hash(&key)], list) {
			/* Don't send to ourselves. */
			if (i->guestid == lg->guestid)
				continue;
			if (!key_eq(&key, &i->key))
				continue;

			/* If dma_transfer() tells us the destination has no
			 * available buffers, we increment "empty". */
			empty += dma_transfer(lg, udma, i);
			break;
		}
		/* If the destination is empty, we release our locks and
		 * give the destination Guest a brief chance to restock. */
		if (empty == 1) {
			/* Give any recipients one chance to restock. */
			up_read(&current->mm->mmap_sem);
			mutex_unlock(&lguest_lock);
			/* Next time, we won't try again. */
			empty++;
			goto again;
		}
	} else {
		/* Private mapping: Guest is sending to its Launcher.  We set
		 * the "dma_is_pending" flag so that the main loop will exit
		 * and the Launcher's read() from /dev/lguest will return. */
		lg->dma_is_pending = 1;
		lg->pending_dma = udma;
		lg->pending_key = ukey;
	}
unlock:
	up_read(fshared);
	mutex_unlock(&lguest_lock);
}
/*:*/

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

/*L:320 This routine looks for a DMA buffer registered by the Guest on the
 * given key (using the BIND_DMA hypercall). */
unsigned long get_dma_buffer(struct lguest *lg,
			     unsigned long ukey, unsigned long *interrupt)
{
	unsigned long ret = 0;
	union futex_key key;
	struct lguest_dma_info *i;
	struct rw_semaphore *fshared = &current->mm->mmap_sem;

	/* Take the Big Lguest Lock to stop other Guests sending this Guest DMA
	 * at the same time. */
	mutex_lock(&lguest_lock);
	/* To match between Guests sharing the same underlying memory we steal
	 * code from the futex infrastructure.  This requires that we hold the
	 * "mmap_sem" for our process (the Launcher), and pass it to the futex
	 * code. */
	down_read(fshared);

	/* This can fail if it's not a valid address, or if the address is not
	 * divisible by 4 (the futex code needs that, we don't really). */
	if (get_futex_key((u32 __user *)ukey, fshared, &key) != 0) {
		kill_guest(lg, "bad registered DMA buffer");
		goto unlock;
	}
	/* Search the hash table for matching entries (the Launcher can only
	 * send to its own Guest for the moment, so the entry must be for this
	 * Guest) */
	list_for_each_entry(i, &dma_hash[hash(&key)], list) {
		if (key_eq(&key, &i->key) && i->guestid == lg->guestid) {
			unsigned int j;
			/* Look through the registered DMA array for an
			 * available buffer. */
			for (j = 0; j < i->num_dmas; j++) {
				struct lguest_dma dma;

				ret = i->dmas + j * sizeof(struct lguest_dma);
				lgread(lg, &dma, ret, sizeof(dma));
				if (dma.used_len == 0)
					break;
			}
			/* Store the interrupt the Guest wants when the buffer
			 * is used. */
			*interrupt = i->interrupt;
			break;
		}
	}
unlock:
	up_read(fshared);
	mutex_unlock(&lguest_lock);
	return ret;
}
/*:*/

/*L:410 This really has completed the Launcher.  Not only have we now finished
 * the longest chapter in our journey, but this also means we are over halfway
 * through!
 *
 * Enough prevaricating around the bush: it is time for us to dive into the
 * core of the Host, in "make Host".
 */
