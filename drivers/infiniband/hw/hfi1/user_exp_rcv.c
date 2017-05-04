/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <asm/page.h>

#include "user_exp_rcv.h"
#include "trace.h"
#include "mmu_rb.h"

struct tid_group {
	struct list_head list;
	unsigned base;
	u8 size;
	u8 used;
	u8 map;
};

struct tid_rb_node {
	struct mmu_rb_node mmu;
	unsigned long phys;
	struct tid_group *grp;
	u32 rcventry;
	dma_addr_t dma_addr;
	bool freed;
	unsigned npages;
	struct page *pages[0];
};

struct tid_pageset {
	u16 idx;
	u16 count;
};

#define EXP_TID_SET_EMPTY(set) (set.count == 0 && list_empty(&set.list))

#define num_user_pages(vaddr, len)				       \
	(1 + (((((unsigned long)(vaddr) +			       \
		 (unsigned long)(len) - 1) & PAGE_MASK) -	       \
	       ((unsigned long)vaddr & PAGE_MASK)) >> PAGE_SHIFT))

static void unlock_exp_tids(struct hfi1_ctxtdata *, struct exp_tid_set *,
			    struct hfi1_filedata *);
static u32 find_phys_blocks(struct page **, unsigned, struct tid_pageset *);
static int set_rcvarray_entry(struct file *, unsigned long, u32,
			      struct tid_group *, struct page **, unsigned);
static int tid_rb_insert(void *, struct mmu_rb_node *);
static void cacheless_tid_rb_remove(struct hfi1_filedata *fdata,
				    struct tid_rb_node *tnode);
static void tid_rb_remove(void *, struct mmu_rb_node *);
static int tid_rb_invalidate(void *, struct mmu_rb_node *);
static int program_rcvarray(struct file *, unsigned long, struct tid_group *,
			    struct tid_pageset *, unsigned, u16, struct page **,
			    u32 *, unsigned *, unsigned *);
static int unprogram_rcvarray(struct file *, u32, struct tid_group **);
static void clear_tid_node(struct hfi1_filedata *fd, struct tid_rb_node *node);

static struct mmu_rb_ops tid_rb_ops = {
	.insert = tid_rb_insert,
	.remove = tid_rb_remove,
	.invalidate = tid_rb_invalidate
};

static inline u32 rcventry2tidinfo(u32 rcventry)
{
	u32 pair = rcventry & ~0x1;

	return EXP_TID_SET(IDX, pair >> 1) |
		EXP_TID_SET(CTRL, 1 << (rcventry - pair));
}

static inline void exp_tid_group_init(struct exp_tid_set *set)
{
	INIT_LIST_HEAD(&set->list);
	set->count = 0;
}

static inline void tid_group_remove(struct tid_group *grp,
				    struct exp_tid_set *set)
{
	list_del_init(&grp->list);
	set->count--;
}

static inline void tid_group_add_tail(struct tid_group *grp,
				      struct exp_tid_set *set)
{
	list_add_tail(&grp->list, &set->list);
	set->count++;
}

static inline struct tid_group *tid_group_pop(struct exp_tid_set *set)
{
	struct tid_group *grp =
		list_first_entry(&set->list, struct tid_group, list);
	list_del_init(&grp->list);
	set->count--;
	return grp;
}

static inline void tid_group_move(struct tid_group *group,
				  struct exp_tid_set *s1,
				  struct exp_tid_set *s2)
{
	tid_group_remove(group, s1);
	tid_group_add_tail(group, s2);
}

/*
 * Initialize context and file private data needed for Expected
 * receive caching. This needs to be done after the context has
 * been configured with the eager/expected RcvEntry counts.
 */
int hfi1_user_exp_rcv_init(struct file *fp)
{
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	unsigned tidbase;
	int i, ret = 0;

	spin_lock_init(&fd->tid_lock);
	spin_lock_init(&fd->invalid_lock);

	if (!uctxt->subctxt_cnt || !fd->subctxt) {
		exp_tid_group_init(&uctxt->tid_group_list);
		exp_tid_group_init(&uctxt->tid_used_list);
		exp_tid_group_init(&uctxt->tid_full_list);

		tidbase = uctxt->expected_base;
		for (i = 0; i < uctxt->expected_count /
			     dd->rcv_entries.group_size; i++) {
			struct tid_group *grp;

			grp = kzalloc(sizeof(*grp), GFP_KERNEL);
			if (!grp) {
				/*
				 * If we fail here, the groups already
				 * allocated will be freed by the close
				 * call.
				 */
				ret = -ENOMEM;
				goto done;
			}
			grp->size = dd->rcv_entries.group_size;
			grp->base = tidbase;
			tid_group_add_tail(grp, &uctxt->tid_group_list);
			tidbase += dd->rcv_entries.group_size;
		}
	}

	fd->entry_to_rb = kcalloc(uctxt->expected_count,
				     sizeof(struct rb_node *),
				     GFP_KERNEL);
	if (!fd->entry_to_rb)
		return -ENOMEM;

	if (!HFI1_CAP_UGET_MASK(uctxt->flags, TID_UNMAP)) {
		fd->invalid_tid_idx = 0;
		fd->invalid_tids = kzalloc(uctxt->expected_count *
					   sizeof(u32), GFP_KERNEL);
		if (!fd->invalid_tids) {
			ret = -ENOMEM;
			goto done;
		}

		/*
		 * Register MMU notifier callbacks. If the registration
		 * fails, continue without TID caching for this context.
		 */
		ret = hfi1_mmu_rb_register(fd, fd->mm, &tid_rb_ops,
					   dd->pport->hfi1_wq,
					   &fd->handler);
		if (ret) {
			dd_dev_info(dd,
				    "Failed MMU notifier registration %d\n",
				    ret);
			ret = 0;
		}
	}

	/*
	 * PSM does not have a good way to separate, count, and
	 * effectively enforce a limit on RcvArray entries used by
	 * subctxts (when context sharing is used) when TID caching
	 * is enabled. To help with that, we calculate a per-process
	 * RcvArray entry share and enforce that.
	 * If TID caching is not in use, PSM deals with usage on its
	 * own. In that case, we allow any subctxt to take all of the
	 * entries.
	 *
	 * Make sure that we set the tid counts only after successful
	 * init.
	 */
	spin_lock(&fd->tid_lock);
	if (uctxt->subctxt_cnt && fd->handler) {
		u16 remainder;

		fd->tid_limit = uctxt->expected_count / uctxt->subctxt_cnt;
		remainder = uctxt->expected_count % uctxt->subctxt_cnt;
		if (remainder && fd->subctxt < remainder)
			fd->tid_limit++;
	} else {
		fd->tid_limit = uctxt->expected_count;
	}
	spin_unlock(&fd->tid_lock);
done:
	return ret;
}

void hfi1_user_exp_rcv_grp_free(struct hfi1_ctxtdata *uctxt)
{
	struct tid_group *grp, *gptr;

	list_for_each_entry_safe(grp, gptr, &uctxt->tid_group_list.list,
				 list) {
		list_del_init(&grp->list);
		kfree(grp);
	}
	hfi1_clear_tids(uctxt);
}

int hfi1_user_exp_rcv_free(struct hfi1_filedata *fd)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;

	/*
	 * The notifier would have been removed when the process'es mm
	 * was freed.
	 */
	if (fd->handler) {
		hfi1_mmu_rb_unregister(fd->handler);
	} else {
		if (!EXP_TID_SET_EMPTY(uctxt->tid_full_list))
			unlock_exp_tids(uctxt, &uctxt->tid_full_list, fd);
		if (!EXP_TID_SET_EMPTY(uctxt->tid_used_list))
			unlock_exp_tids(uctxt, &uctxt->tid_used_list, fd);
	}

	kfree(fd->invalid_tids);
	fd->invalid_tids = NULL;

	kfree(fd->entry_to_rb);
	fd->entry_to_rb = NULL;
	return 0;
}

/*
 * Write an "empty" RcvArray entry.
 * This function exists so the TID registaration code can use it
 * to write to unused/unneeded entries and still take advantage
 * of the WC performance improvements. The HFI will ignore this
 * write to the RcvArray entry.
 */
static inline void rcv_array_wc_fill(struct hfi1_devdata *dd, u32 index)
{
	/*
	 * Doing the WC fill writes only makes sense if the device is
	 * present and the RcvArray has been mapped as WC memory.
	 */
	if ((dd->flags & HFI1_PRESENT) && dd->rcvarray_wc)
		writeq(0, dd->rcvarray_wc + (index * 8));
}

/*
 * RcvArray entry allocation for Expected Receives is done by the
 * following algorithm:
 *
 * The context keeps 3 lists of groups of RcvArray entries:
 *   1. List of empty groups - tid_group_list
 *      This list is created during user context creation and
 *      contains elements which describe sets (of 8) of empty
 *      RcvArray entries.
 *   2. List of partially used groups - tid_used_list
 *      This list contains sets of RcvArray entries which are
 *      not completely used up. Another mapping request could
 *      use some of all of the remaining entries.
 *   3. List of full groups - tid_full_list
 *      This is the list where sets that are completely used
 *      up go.
 *
 * An attempt to optimize the usage of RcvArray entries is
 * made by finding all sets of physically contiguous pages in a
 * user's buffer.
 * These physically contiguous sets are further split into
 * sizes supported by the receive engine of the HFI. The
 * resulting sets of pages are stored in struct tid_pageset,
 * which describes the sets as:
 *    * .count - number of pages in this set
 *    * .idx - starting index into struct page ** array
 *                    of this set
 *
 * From this point on, the algorithm deals with the page sets
 * described above. The number of pagesets is divided by the
 * RcvArray group size to produce the number of full groups
 * needed.
 *
 * Groups from the 3 lists are manipulated using the following
 * rules:
 *   1. For each set of 8 pagesets, a complete group from
 *      tid_group_list is taken, programmed, and moved to
 *      the tid_full_list list.
 *   2. For all remaining pagesets:
 *      2.1 If the tid_used_list is empty and the tid_group_list
 *          is empty, stop processing pageset and return only
 *          what has been programmed up to this point.
 *      2.2 If the tid_used_list is empty and the tid_group_list
 *          is not empty, move a group from tid_group_list to
 *          tid_used_list.
 *      2.3 For each group is tid_used_group, program as much as
 *          can fit into the group. If the group becomes fully
 *          used, move it to tid_full_list.
 */
int hfi1_user_exp_rcv_setup(struct file *fp, struct hfi1_tid_info *tinfo)
{
	int ret = 0, need_group = 0, pinned;
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	unsigned npages, ngroups, pageidx = 0, pageset_count, npagesets,
		tididx = 0, mapped, mapped_pages = 0;
	unsigned long vaddr = tinfo->vaddr;
	struct page **pages = NULL;
	u32 *tidlist = NULL;
	struct tid_pageset *pagesets = NULL;

	/* Get the number of pages the user buffer spans */
	npages = num_user_pages(vaddr, tinfo->length);
	if (!npages)
		return -EINVAL;

	if (npages > uctxt->expected_count) {
		dd_dev_err(dd, "Expected buffer too big\n");
		return -EINVAL;
	}

	/* Verify that access is OK for the user buffer */
	if (!access_ok(VERIFY_WRITE, (void __user *)vaddr,
		       npages * PAGE_SIZE)) {
		dd_dev_err(dd, "Fail vaddr %p, %u pages, !access_ok\n",
			   (void *)vaddr, npages);
		return -EFAULT;
	}

	pagesets = kcalloc(uctxt->expected_count, sizeof(*pagesets),
			   GFP_KERNEL);
	if (!pagesets)
		return -ENOMEM;

	/* Allocate the array of struct page pointers needed for pinning */
	pages = kcalloc(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		goto bail;
	}

	/*
	 * Pin all the pages of the user buffer. If we can't pin all the
	 * pages, accept the amount pinned so far and program only that.
	 * User space knows how to deal with partially programmed buffers.
	 */
	if (!hfi1_can_pin_pages(dd, fd->mm, fd->tid_n_pinned, npages)) {
		ret = -ENOMEM;
		goto bail;
	}

	pinned = hfi1_acquire_user_pages(fd->mm, vaddr, npages, true, pages);
	if (pinned <= 0) {
		ret = pinned;
		goto bail;
	}
	fd->tid_n_pinned += npages;

	/* Find sets of physically contiguous pages */
	npagesets = find_phys_blocks(pages, pinned, pagesets);

	/*
	 * We don't need to access this under a lock since tid_used is per
	 * process and the same process cannot be in hfi1_user_exp_rcv_clear()
	 * and hfi1_user_exp_rcv_setup() at the same time.
	 */
	spin_lock(&fd->tid_lock);
	if (fd->tid_used + npagesets > fd->tid_limit)
		pageset_count = fd->tid_limit - fd->tid_used;
	else
		pageset_count = npagesets;
	spin_unlock(&fd->tid_lock);

	if (!pageset_count)
		goto bail;

	ngroups = pageset_count / dd->rcv_entries.group_size;
	tidlist = kcalloc(pageset_count, sizeof(*tidlist), GFP_KERNEL);
	if (!tidlist) {
		ret = -ENOMEM;
		goto nomem;
	}

	tididx = 0;

	/*
	 * From this point on, we are going to be using shared (between master
	 * and subcontexts) context resources. We need to take the lock.
	 */
	mutex_lock(&uctxt->exp_lock);
	/*
	 * The first step is to program the RcvArray entries which are complete
	 * groups.
	 */
	while (ngroups && uctxt->tid_group_list.count) {
		struct tid_group *grp =
			tid_group_pop(&uctxt->tid_group_list);

		ret = program_rcvarray(fp, vaddr, grp, pagesets,
				       pageidx, dd->rcv_entries.group_size,
				       pages, tidlist, &tididx, &mapped);
		/*
		 * If there was a failure to program the RcvArray
		 * entries for the entire group, reset the grp fields
		 * and add the grp back to the free group list.
		 */
		if (ret <= 0) {
			tid_group_add_tail(grp, &uctxt->tid_group_list);
			hfi1_cdbg(TID,
				  "Failed to program RcvArray group %d", ret);
			goto unlock;
		}

		tid_group_add_tail(grp, &uctxt->tid_full_list);
		ngroups--;
		pageidx += ret;
		mapped_pages += mapped;
	}

	while (pageidx < pageset_count) {
		struct tid_group *grp, *ptr;
		/*
		 * If we don't have any partially used tid groups, check
		 * if we have empty groups. If so, take one from there and
		 * put in the partially used list.
		 */
		if (!uctxt->tid_used_list.count || need_group) {
			if (!uctxt->tid_group_list.count)
				goto unlock;

			grp = tid_group_pop(&uctxt->tid_group_list);
			tid_group_add_tail(grp, &uctxt->tid_used_list);
			need_group = 0;
		}
		/*
		 * There is an optimization opportunity here - instead of
		 * fitting as many page sets as we can, check for a group
		 * later on in the list that could fit all of them.
		 */
		list_for_each_entry_safe(grp, ptr, &uctxt->tid_used_list.list,
					 list) {
			unsigned use = min_t(unsigned, pageset_count - pageidx,
					     grp->size - grp->used);

			ret = program_rcvarray(fp, vaddr, grp, pagesets,
					       pageidx, use, pages, tidlist,
					       &tididx, &mapped);
			if (ret < 0) {
				hfi1_cdbg(TID,
					  "Failed to program RcvArray entries %d",
					  ret);
				ret = -EFAULT;
				goto unlock;
			} else if (ret > 0) {
				if (grp->used == grp->size)
					tid_group_move(grp,
						       &uctxt->tid_used_list,
						       &uctxt->tid_full_list);
				pageidx += ret;
				mapped_pages += mapped;
				need_group = 0;
				/* Check if we are done so we break out early */
				if (pageidx >= pageset_count)
					break;
			} else if (WARN_ON(ret == 0)) {
				/*
				 * If ret is 0, we did not program any entries
				 * into this group, which can only happen if
				 * we've screwed up the accounting somewhere.
				 * Warn and try to continue.
				 */
				need_group = 1;
			}
		}
	}
unlock:
	mutex_unlock(&uctxt->exp_lock);
nomem:
	hfi1_cdbg(TID, "total mapped: tidpairs:%u pages:%u (%d)", tididx,
		  mapped_pages, ret);
	if (tididx) {
		spin_lock(&fd->tid_lock);
		fd->tid_used += tididx;
		spin_unlock(&fd->tid_lock);
		tinfo->tidcnt = tididx;
		tinfo->length = mapped_pages * PAGE_SIZE;

		if (copy_to_user((void __user *)(unsigned long)tinfo->tidlist,
				 tidlist, sizeof(tidlist[0]) * tididx)) {
			/*
			 * On failure to copy to the user level, we need to undo
			 * everything done so far so we don't leak resources.
			 */
			tinfo->tidlist = (unsigned long)&tidlist;
			hfi1_user_exp_rcv_clear(fp, tinfo);
			tinfo->tidlist = 0;
			ret = -EFAULT;
			goto bail;
		}
	}

	/*
	 * If not everything was mapped (due to insufficient RcvArray entries,
	 * for example), unpin all unmapped pages so we can pin them nex time.
	 */
	if (mapped_pages != pinned) {
		hfi1_release_user_pages(fd->mm, &pages[mapped_pages],
					pinned - mapped_pages,
					false);
		fd->tid_n_pinned -= pinned - mapped_pages;
	}
bail:
	kfree(pagesets);
	kfree(pages);
	kfree(tidlist);
	return ret > 0 ? 0 : ret;
}

int hfi1_user_exp_rcv_clear(struct file *fp, struct hfi1_tid_info *tinfo)
{
	int ret = 0;
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	u32 *tidinfo;
	unsigned tididx;

	tidinfo = kcalloc(tinfo->tidcnt, sizeof(*tidinfo), GFP_KERNEL);
	if (!tidinfo)
		return -ENOMEM;

	if (copy_from_user(tidinfo, (void __user *)(unsigned long)
			   tinfo->tidlist, sizeof(tidinfo[0]) *
			   tinfo->tidcnt)) {
		ret = -EFAULT;
		goto done;
	}

	mutex_lock(&uctxt->exp_lock);
	for (tididx = 0; tididx < tinfo->tidcnt; tididx++) {
		ret = unprogram_rcvarray(fp, tidinfo[tididx], NULL);
		if (ret) {
			hfi1_cdbg(TID, "Failed to unprogram rcv array %d",
				  ret);
			break;
		}
	}
	spin_lock(&fd->tid_lock);
	fd->tid_used -= tididx;
	spin_unlock(&fd->tid_lock);
	tinfo->tidcnt = tididx;
	mutex_unlock(&uctxt->exp_lock);
done:
	kfree(tidinfo);
	return ret;
}

int hfi1_user_exp_rcv_invalid(struct file *fp, struct hfi1_tid_info *tinfo)
{
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	unsigned long *ev = uctxt->dd->events +
		(((uctxt->ctxt - uctxt->dd->first_user_ctxt) *
		  HFI1_MAX_SHARED_CTXTS) + fd->subctxt);
	u32 *array;
	int ret = 0;

	if (!fd->invalid_tids)
		return -EINVAL;

	/*
	 * copy_to_user() can sleep, which will leave the invalid_lock
	 * locked and cause the MMU notifier to be blocked on the lock
	 * for a long time.
	 * Copy the data to a local buffer so we can release the lock.
	 */
	array = kcalloc(uctxt->expected_count, sizeof(*array), GFP_KERNEL);
	if (!array)
		return -EFAULT;

	spin_lock(&fd->invalid_lock);
	if (fd->invalid_tid_idx) {
		memcpy(array, fd->invalid_tids, sizeof(*array) *
		       fd->invalid_tid_idx);
		memset(fd->invalid_tids, 0, sizeof(*fd->invalid_tids) *
		       fd->invalid_tid_idx);
		tinfo->tidcnt = fd->invalid_tid_idx;
		fd->invalid_tid_idx = 0;
		/*
		 * Reset the user flag while still holding the lock.
		 * Otherwise, PSM can miss events.
		 */
		clear_bit(_HFI1_EVENT_TID_MMU_NOTIFY_BIT, ev);
	} else {
		tinfo->tidcnt = 0;
	}
	spin_unlock(&fd->invalid_lock);

	if (tinfo->tidcnt) {
		if (copy_to_user((void __user *)tinfo->tidlist,
				 array, sizeof(*array) * tinfo->tidcnt))
			ret = -EFAULT;
	}
	kfree(array);

	return ret;
}

static u32 find_phys_blocks(struct page **pages, unsigned npages,
			    struct tid_pageset *list)
{
	unsigned pagecount, pageidx, setcount = 0, i;
	unsigned long pfn, this_pfn;

	if (!npages)
		return 0;

	/*
	 * Look for sets of physically contiguous pages in the user buffer.
	 * This will allow us to optimize Expected RcvArray entry usage by
	 * using the bigger supported sizes.
	 */
	pfn = page_to_pfn(pages[0]);
	for (pageidx = 0, pagecount = 1, i = 1; i <= npages; i++) {
		this_pfn = i < npages ? page_to_pfn(pages[i]) : 0;

		/*
		 * If the pfn's are not sequential, pages are not physically
		 * contiguous.
		 */
		if (this_pfn != ++pfn) {
			/*
			 * At this point we have to loop over the set of
			 * physically contiguous pages and break them down it
			 * sizes supported by the HW.
			 * There are two main constraints:
			 *     1. The max buffer size is MAX_EXPECTED_BUFFER.
			 *        If the total set size is bigger than that
			 *        program only a MAX_EXPECTED_BUFFER chunk.
			 *     2. The buffer size has to be a power of two. If
			 *        it is not, round down to the closes power of
			 *        2 and program that size.
			 */
			while (pagecount) {
				int maxpages = pagecount;
				u32 bufsize = pagecount * PAGE_SIZE;

				if (bufsize > MAX_EXPECTED_BUFFER)
					maxpages =
						MAX_EXPECTED_BUFFER >>
						PAGE_SHIFT;
				else if (!is_power_of_2(bufsize))
					maxpages =
						rounddown_pow_of_two(bufsize) >>
						PAGE_SHIFT;

				list[setcount].idx = pageidx;
				list[setcount].count = maxpages;
				pagecount -= maxpages;
				pageidx += maxpages;
				setcount++;
			}
			pageidx = i;
			pagecount = 1;
			pfn = this_pfn;
		} else {
			pagecount++;
		}
	}
	return setcount;
}

/**
 * program_rcvarray() - program an RcvArray group with receive buffers
 * @fp: file pointer
 * @vaddr: starting user virtual address
 * @grp: RcvArray group
 * @sets: array of struct tid_pageset holding information on physically
 *        contiguous chunks from the user buffer
 * @start: starting index into sets array
 * @count: number of struct tid_pageset's to program
 * @pages: an array of struct page * for the user buffer
 * @tidlist: the array of u32 elements when the information about the
 *           programmed RcvArray entries is to be encoded.
 * @tididx: starting offset into tidlist
 * @pmapped: (output parameter) number of pages programmed into the RcvArray
 *           entries.
 *
 * This function will program up to 'count' number of RcvArray entries from the
 * group 'grp'. To make best use of write-combining writes, the function will
 * perform writes to the unused RcvArray entries which will be ignored by the
 * HW. Each RcvArray entry will be programmed with a physically contiguous
 * buffer chunk from the user's virtual buffer.
 *
 * Return:
 * -EINVAL if the requested count is larger than the size of the group,
 * -ENOMEM or -EFAULT on error from set_rcvarray_entry(), or
 * number of RcvArray entries programmed.
 */
static int program_rcvarray(struct file *fp, unsigned long vaddr,
			    struct tid_group *grp,
			    struct tid_pageset *sets,
			    unsigned start, u16 count, struct page **pages,
			    u32 *tidlist, unsigned *tididx, unsigned *pmapped)
{
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	u16 idx;
	u32 tidinfo = 0, rcventry, useidx = 0;
	int mapped = 0;

	/* Count should never be larger than the group size */
	if (count > grp->size)
		return -EINVAL;

	/* Find the first unused entry in the group */
	for (idx = 0; idx < grp->size; idx++) {
		if (!(grp->map & (1 << idx))) {
			useidx = idx;
			break;
		}
		rcv_array_wc_fill(dd, grp->base + idx);
	}

	idx = 0;
	while (idx < count) {
		u16 npages, pageidx, setidx = start + idx;
		int ret = 0;

		/*
		 * If this entry in the group is used, move to the next one.
		 * If we go past the end of the group, exit the loop.
		 */
		if (useidx >= grp->size) {
			break;
		} else if (grp->map & (1 << useidx)) {
			rcv_array_wc_fill(dd, grp->base + useidx);
			useidx++;
			continue;
		}

		rcventry = grp->base + useidx;
		npages = sets[setidx].count;
		pageidx = sets[setidx].idx;

		ret = set_rcvarray_entry(fp, vaddr + (pageidx * PAGE_SIZE),
					 rcventry, grp, pages + pageidx,
					 npages);
		if (ret)
			return ret;
		mapped += npages;

		tidinfo = rcventry2tidinfo(rcventry - uctxt->expected_base) |
			EXP_TID_SET(LEN, npages);
		tidlist[(*tididx)++] = tidinfo;
		grp->used++;
		grp->map |= 1 << useidx++;
		idx++;
	}

	/* Fill the rest of the group with "blank" writes */
	for (; useidx < grp->size; useidx++)
		rcv_array_wc_fill(dd, grp->base + useidx);
	*pmapped = mapped;
	return idx;
}

static int set_rcvarray_entry(struct file *fp, unsigned long vaddr,
			      u32 rcventry, struct tid_group *grp,
			      struct page **pages, unsigned npages)
{
	int ret;
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct tid_rb_node *node;
	struct hfi1_devdata *dd = uctxt->dd;
	dma_addr_t phys;

	/*
	 * Allocate the node first so we can handle a potential
	 * failure before we've programmed anything.
	 */
	node = kzalloc(sizeof(*node) + (sizeof(struct page *) * npages),
		       GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	phys = pci_map_single(dd->pcidev,
			      __va(page_to_phys(pages[0])),
			      npages * PAGE_SIZE, PCI_DMA_FROMDEVICE);
	if (dma_mapping_error(&dd->pcidev->dev, phys)) {
		dd_dev_err(dd, "Failed to DMA map Exp Rcv pages 0x%llx\n",
			   phys);
		kfree(node);
		return -EFAULT;
	}

	node->mmu.addr = vaddr;
	node->mmu.len = npages * PAGE_SIZE;
	node->phys = page_to_phys(pages[0]);
	node->npages = npages;
	node->rcventry = rcventry;
	node->dma_addr = phys;
	node->grp = grp;
	node->freed = false;
	memcpy(node->pages, pages, sizeof(struct page *) * npages);

	if (!fd->handler)
		ret = tid_rb_insert(fd, &node->mmu);
	else
		ret = hfi1_mmu_rb_insert(fd->handler, &node->mmu);

	if (ret) {
		hfi1_cdbg(TID, "Failed to insert RB node %u 0x%lx, 0x%lx %d",
			  node->rcventry, node->mmu.addr, node->phys, ret);
		pci_unmap_single(dd->pcidev, phys, npages * PAGE_SIZE,
				 PCI_DMA_FROMDEVICE);
		kfree(node);
		return -EFAULT;
	}
	hfi1_put_tid(dd, rcventry, PT_EXPECTED, phys, ilog2(npages) + 1);
	trace_hfi1_exp_tid_reg(uctxt->ctxt, fd->subctxt, rcventry, npages,
			       node->mmu.addr, node->phys, phys);
	return 0;
}

static int unprogram_rcvarray(struct file *fp, u32 tidinfo,
			      struct tid_group **grp)
{
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	struct tid_rb_node *node;
	u8 tidctrl = EXP_TID_GET(tidinfo, CTRL);
	u32 tididx = EXP_TID_GET(tidinfo, IDX) << 1, rcventry;

	if (tididx >= uctxt->expected_count) {
		dd_dev_err(dd, "Invalid RcvArray entry (%u) index for ctxt %u\n",
			   tididx, uctxt->ctxt);
		return -EINVAL;
	}

	if (tidctrl == 0x3)
		return -EINVAL;

	rcventry = tididx + (tidctrl - 1);

	node = fd->entry_to_rb[rcventry];
	if (!node || node->rcventry != (uctxt->expected_base + rcventry))
		return -EBADF;

	if (grp)
		*grp = node->grp;

	if (!fd->handler)
		cacheless_tid_rb_remove(fd, node);
	else
		hfi1_mmu_rb_remove(fd->handler, &node->mmu);

	return 0;
}

static void clear_tid_node(struct hfi1_filedata *fd, struct tid_rb_node *node)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;

	trace_hfi1_exp_tid_unreg(uctxt->ctxt, fd->subctxt, node->rcventry,
				 node->npages, node->mmu.addr, node->phys,
				 node->dma_addr);

	hfi1_put_tid(dd, node->rcventry, PT_INVALID, 0, 0);
	/*
	 * Make sure device has seen the write before we unpin the
	 * pages.
	 */
	flush_wc();

	pci_unmap_single(dd->pcidev, node->dma_addr, node->mmu.len,
			 PCI_DMA_FROMDEVICE);
	hfi1_release_user_pages(fd->mm, node->pages, node->npages, true);
	fd->tid_n_pinned -= node->npages;

	node->grp->used--;
	node->grp->map &= ~(1 << (node->rcventry - node->grp->base));

	if (node->grp->used == node->grp->size - 1)
		tid_group_move(node->grp, &uctxt->tid_full_list,
			       &uctxt->tid_used_list);
	else if (!node->grp->used)
		tid_group_move(node->grp, &uctxt->tid_used_list,
			       &uctxt->tid_group_list);
	kfree(node);
}

/*
 * As a simple helper for hfi1_user_exp_rcv_free, this function deals with
 * clearing nodes in the non-cached case.
 */
static void unlock_exp_tids(struct hfi1_ctxtdata *uctxt,
			    struct exp_tid_set *set,
			    struct hfi1_filedata *fd)
{
	struct tid_group *grp, *ptr;
	int i;

	list_for_each_entry_safe(grp, ptr, &set->list, list) {
		list_del_init(&grp->list);

		for (i = 0; i < grp->size; i++) {
			if (grp->map & (1 << i)) {
				u16 rcventry = grp->base + i;
				struct tid_rb_node *node;

				node = fd->entry_to_rb[rcventry -
							  uctxt->expected_base];
				if (!node || node->rcventry != rcventry)
					continue;

				cacheless_tid_rb_remove(fd, node);
			}
		}
	}
}

/*
 * Always return 0 from this function.  A non-zero return indicates that the
 * remove operation will be called and that memory should be unpinned.
 * However, the driver cannot unpin out from under PSM.  Instead, retain the
 * memory (by returning 0) and inform PSM that the memory is going away.  PSM
 * will call back later when it has removed the memory from its list.
 */
static int tid_rb_invalidate(void *arg, struct mmu_rb_node *mnode)
{
	struct hfi1_filedata *fdata = arg;
	struct hfi1_ctxtdata *uctxt = fdata->uctxt;
	struct tid_rb_node *node =
		container_of(mnode, struct tid_rb_node, mmu);

	if (node->freed)
		return 0;

	trace_hfi1_exp_tid_inval(uctxt->ctxt, fdata->subctxt, node->mmu.addr,
				 node->rcventry, node->npages, node->dma_addr);
	node->freed = true;

	spin_lock(&fdata->invalid_lock);
	if (fdata->invalid_tid_idx < uctxt->expected_count) {
		fdata->invalid_tids[fdata->invalid_tid_idx] =
			rcventry2tidinfo(node->rcventry - uctxt->expected_base);
		fdata->invalid_tids[fdata->invalid_tid_idx] |=
			EXP_TID_SET(LEN, node->npages);
		if (!fdata->invalid_tid_idx) {
			unsigned long *ev;

			/*
			 * hfi1_set_uevent_bits() sets a user event flag
			 * for all processes. Because calling into the
			 * driver to process TID cache invalidations is
			 * expensive and TID cache invalidations are
			 * handled on a per-process basis, we can
			 * optimize this to set the flag only for the
			 * process in question.
			 */
			ev = uctxt->dd->events +
				(((uctxt->ctxt - uctxt->dd->first_user_ctxt) *
				  HFI1_MAX_SHARED_CTXTS) + fdata->subctxt);
			set_bit(_HFI1_EVENT_TID_MMU_NOTIFY_BIT, ev);
		}
		fdata->invalid_tid_idx++;
	}
	spin_unlock(&fdata->invalid_lock);
	return 0;
}

static int tid_rb_insert(void *arg, struct mmu_rb_node *node)
{
	struct hfi1_filedata *fdata = arg;
	struct tid_rb_node *tnode =
		container_of(node, struct tid_rb_node, mmu);
	u32 base = fdata->uctxt->expected_base;

	fdata->entry_to_rb[tnode->rcventry - base] = tnode;
	return 0;
}

static void cacheless_tid_rb_remove(struct hfi1_filedata *fdata,
				    struct tid_rb_node *tnode)
{
	u32 base = fdata->uctxt->expected_base;

	fdata->entry_to_rb[tnode->rcventry - base] = NULL;
	clear_tid_node(fdata, tnode);
}

static void tid_rb_remove(void *arg, struct mmu_rb_node *node)
{
	struct hfi1_filedata *fdata = arg;
	struct tid_rb_node *tnode =
		container_of(node, struct tid_rb_node, mmu);

	cacheless_tid_rb_remove(fdata, tnode);
}
