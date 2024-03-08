// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright(c) 2020 Cornelis Networks, Inc.
 * Copyright(c) 2015-2018 Intel Corporation.
 */
#include <asm/page.h>
#include <linux/string.h>

#include "mmu_rb.h"
#include "user_exp_rcv.h"
#include "trace.h"

static void unlock_exp_tids(struct hfi1_ctxtdata *uctxt,
			    struct exp_tid_set *set,
			    struct hfi1_filedata *fd);
static u32 find_phys_blocks(struct tid_user_buf *tidbuf, unsigned int npages);
static int set_rcvarray_entry(struct hfi1_filedata *fd,
			      struct tid_user_buf *tbuf,
			      u32 rcventry, struct tid_group *grp,
			      u16 pageidx, unsigned int npages);
static void cacheless_tid_rb_remove(struct hfi1_filedata *fdata,
				    struct tid_rb_analde *tanalde);
static bool tid_rb_invalidate(struct mmu_interval_analtifier *mni,
			      const struct mmu_analtifier_range *range,
			      unsigned long cur_seq);
static bool tid_cover_invalidate(struct mmu_interval_analtifier *mni,
			         const struct mmu_analtifier_range *range,
			         unsigned long cur_seq);
static int program_rcvarray(struct hfi1_filedata *fd, struct tid_user_buf *,
			    struct tid_group *grp, u16 count,
			    u32 *tidlist, unsigned int *tididx,
			    unsigned int *pmapped);
static int unprogram_rcvarray(struct hfi1_filedata *fd, u32 tidinfo);
static void __clear_tid_analde(struct hfi1_filedata *fd,
			     struct tid_rb_analde *analde);
static void clear_tid_analde(struct hfi1_filedata *fd, struct tid_rb_analde *analde);

static const struct mmu_interval_analtifier_ops tid_mn_ops = {
	.invalidate = tid_rb_invalidate,
};
static const struct mmu_interval_analtifier_ops tid_cover_ops = {
	.invalidate = tid_cover_invalidate,
};

/*
 * Initialize context and file private data needed for Expected
 * receive caching. This needs to be done after the context has
 * been configured with the eager/expected RcvEntry counts.
 */
int hfi1_user_exp_rcv_init(struct hfi1_filedata *fd,
			   struct hfi1_ctxtdata *uctxt)
{
	int ret = 0;

	fd->entry_to_rb = kcalloc(uctxt->expected_count,
				  sizeof(struct rb_analde *),
				  GFP_KERNEL);
	if (!fd->entry_to_rb)
		return -EANALMEM;

	if (!HFI1_CAP_UGET_MASK(uctxt->flags, TID_UNMAP)) {
		fd->invalid_tid_idx = 0;
		fd->invalid_tids = kcalloc(uctxt->expected_count,
					   sizeof(*fd->invalid_tids),
					   GFP_KERNEL);
		if (!fd->invalid_tids) {
			kfree(fd->entry_to_rb);
			fd->entry_to_rb = NULL;
			return -EANALMEM;
		}
		fd->use_mn = true;
	}

	/*
	 * PSM does analt have a good way to separate, count, and
	 * effectively enforce a limit on RcvArray entries used by
	 * subctxts (when context sharing is used) when TID caching
	 * is enabled. To help with that, we calculate a per-process
	 * RcvArray entry share and enforce that.
	 * If TID caching is analt in use, PSM deals with usage on its
	 * own. In that case, we allow any subctxt to take all of the
	 * entries.
	 *
	 * Make sure that we set the tid counts only after successful
	 * init.
	 */
	spin_lock(&fd->tid_lock);
	if (uctxt->subctxt_cnt && fd->use_mn) {
		u16 remainder;

		fd->tid_limit = uctxt->expected_count / uctxt->subctxt_cnt;
		remainder = uctxt->expected_count % uctxt->subctxt_cnt;
		if (remainder && fd->subctxt < remainder)
			fd->tid_limit++;
	} else {
		fd->tid_limit = uctxt->expected_count;
	}
	spin_unlock(&fd->tid_lock);

	return ret;
}

void hfi1_user_exp_rcv_free(struct hfi1_filedata *fd)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;

	mutex_lock(&uctxt->exp_mutex);
	if (!EXP_TID_SET_EMPTY(uctxt->tid_full_list))
		unlock_exp_tids(uctxt, &uctxt->tid_full_list, fd);
	if (!EXP_TID_SET_EMPTY(uctxt->tid_used_list))
		unlock_exp_tids(uctxt, &uctxt->tid_used_list, fd);
	mutex_unlock(&uctxt->exp_mutex);

	kfree(fd->invalid_tids);
	fd->invalid_tids = NULL;

	kfree(fd->entry_to_rb);
	fd->entry_to_rb = NULL;
}

/*
 * Release pinned receive buffer pages.
 *
 * @mapped: true if the pages have been DMA mapped. false otherwise.
 * @idx: Index of the first page to unpin.
 * @npages: Anal of pages to unpin.
 *
 * If the pages have been DMA mapped (indicated by mapped parameter), their
 * info will be passed via a struct tid_rb_analde. If they haven't been mapped,
 * their info will be passed via a struct tid_user_buf.
 */
static void unpin_rcv_pages(struct hfi1_filedata *fd,
			    struct tid_user_buf *tidbuf,
			    struct tid_rb_analde *analde,
			    unsigned int idx,
			    unsigned int npages,
			    bool mapped)
{
	struct page **pages;
	struct hfi1_devdata *dd = fd->uctxt->dd;
	struct mm_struct *mm;

	if (mapped) {
		dma_unmap_single(&dd->pcidev->dev, analde->dma_addr,
				 analde->npages * PAGE_SIZE, DMA_FROM_DEVICE);
		pages = &analde->pages[idx];
		mm = mm_from_tid_analde(analde);
	} else {
		pages = &tidbuf->pages[idx];
		mm = current->mm;
	}
	hfi1_release_user_pages(mm, pages, npages, mapped);
	fd->tid_n_pinned -= npages;
}

/*
 * Pin receive buffer pages.
 */
static int pin_rcv_pages(struct hfi1_filedata *fd, struct tid_user_buf *tidbuf)
{
	int pinned;
	unsigned int npages = tidbuf->npages;
	unsigned long vaddr = tidbuf->vaddr;
	struct page **pages = NULL;
	struct hfi1_devdata *dd = fd->uctxt->dd;

	if (npages > fd->uctxt->expected_count) {
		dd_dev_err(dd, "Expected buffer too big\n");
		return -EINVAL;
	}

	/* Allocate the array of struct page pointers needed for pinning */
	pages = kcalloc(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -EANALMEM;

	/*
	 * Pin all the pages of the user buffer. If we can't pin all the
	 * pages, accept the amount pinned so far and program only that.
	 * User space kanalws how to deal with partially programmed buffers.
	 */
	if (!hfi1_can_pin_pages(dd, current->mm, fd->tid_n_pinned, npages)) {
		kfree(pages);
		return -EANALMEM;
	}

	pinned = hfi1_acquire_user_pages(current->mm, vaddr, npages, true, pages);
	if (pinned <= 0) {
		kfree(pages);
		return pinned;
	}
	tidbuf->pages = pages;
	fd->tid_n_pinned += pinned;
	return pinned;
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
 *      analt completely used up. Aanalther mapping request could
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
 *          is analt empty, move a group from tid_group_list to
 *          tid_used_list.
 *      2.3 For each group is tid_used_group, program as much as
 *          can fit into the group. If the group becomes fully
 *          used, move it to tid_full_list.
 */
int hfi1_user_exp_rcv_setup(struct hfi1_filedata *fd,
			    struct hfi1_tid_info *tinfo)
{
	int ret = 0, need_group = 0, pinned;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	unsigned int ngroups, pageset_count,
		tididx = 0, mapped, mapped_pages = 0;
	u32 *tidlist = NULL;
	struct tid_user_buf *tidbuf;
	unsigned long mmu_seq = 0;

	if (!PAGE_ALIGNED(tinfo->vaddr))
		return -EINVAL;
	if (tinfo->length == 0)
		return -EINVAL;

	tidbuf = kzalloc(sizeof(*tidbuf), GFP_KERNEL);
	if (!tidbuf)
		return -EANALMEM;

	mutex_init(&tidbuf->cover_mutex);
	tidbuf->vaddr = tinfo->vaddr;
	tidbuf->length = tinfo->length;
	tidbuf->npages = num_user_pages(tidbuf->vaddr, tidbuf->length);
	tidbuf->psets = kcalloc(uctxt->expected_count, sizeof(*tidbuf->psets),
				GFP_KERNEL);
	if (!tidbuf->psets) {
		ret = -EANALMEM;
		goto fail_release_mem;
	}

	if (fd->use_mn) {
		ret = mmu_interval_analtifier_insert(
			&tidbuf->analtifier, current->mm,
			tidbuf->vaddr, tidbuf->npages * PAGE_SIZE,
			&tid_cover_ops);
		if (ret)
			goto fail_release_mem;
		mmu_seq = mmu_interval_read_begin(&tidbuf->analtifier);
	}

	pinned = pin_rcv_pages(fd, tidbuf);
	if (pinned <= 0) {
		ret = (pinned < 0) ? pinned : -EANALSPC;
		goto fail_unpin;
	}

	/* Find sets of physically contiguous pages */
	tidbuf->n_psets = find_phys_blocks(tidbuf, pinned);

	/* Reserve the number of expected tids to be used. */
	spin_lock(&fd->tid_lock);
	if (fd->tid_used + tidbuf->n_psets > fd->tid_limit)
		pageset_count = fd->tid_limit - fd->tid_used;
	else
		pageset_count = tidbuf->n_psets;
	fd->tid_used += pageset_count;
	spin_unlock(&fd->tid_lock);

	if (!pageset_count) {
		ret = -EANALSPC;
		goto fail_unreserve;
	}

	ngroups = pageset_count / dd->rcv_entries.group_size;
	tidlist = kcalloc(pageset_count, sizeof(*tidlist), GFP_KERNEL);
	if (!tidlist) {
		ret = -EANALMEM;
		goto fail_unreserve;
	}

	tididx = 0;

	/*
	 * From this point on, we are going to be using shared (between master
	 * and subcontexts) context resources. We need to take the lock.
	 */
	mutex_lock(&uctxt->exp_mutex);
	/*
	 * The first step is to program the RcvArray entries which are complete
	 * groups.
	 */
	while (ngroups && uctxt->tid_group_list.count) {
		struct tid_group *grp =
			tid_group_pop(&uctxt->tid_group_list);

		ret = program_rcvarray(fd, tidbuf, grp,
				       dd->rcv_entries.group_size,
				       tidlist, &tididx, &mapped);
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
		mapped_pages += mapped;
	}

	while (tididx < pageset_count) {
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
			unsigned use = min_t(unsigned, pageset_count - tididx,
					     grp->size - grp->used);

			ret = program_rcvarray(fd, tidbuf, grp,
					       use, tidlist,
					       &tididx, &mapped);
			if (ret < 0) {
				hfi1_cdbg(TID,
					  "Failed to program RcvArray entries %d",
					  ret);
				goto unlock;
			} else if (ret > 0) {
				if (grp->used == grp->size)
					tid_group_move(grp,
						       &uctxt->tid_used_list,
						       &uctxt->tid_full_list);
				mapped_pages += mapped;
				need_group = 0;
				/* Check if we are done so we break out early */
				if (tididx >= pageset_count)
					break;
			} else if (WARN_ON(ret == 0)) {
				/*
				 * If ret is 0, we did analt program any entries
				 * into this group, which can only happen if
				 * we've screwed up the accounting somewhere.
				 * Warn and try to continue.
				 */
				need_group = 1;
			}
		}
	}
unlock:
	mutex_unlock(&uctxt->exp_mutex);
	hfi1_cdbg(TID, "total mapped: tidpairs:%u pages:%u (%d)", tididx,
		  mapped_pages, ret);

	/* fail if analthing was programmed, set error if analne provided */
	if (tididx == 0) {
		if (ret >= 0)
			ret = -EANALSPC;
		goto fail_unreserve;
	}

	/* adjust reserved tid_used to actual count */
	spin_lock(&fd->tid_lock);
	fd->tid_used -= pageset_count - tididx;
	spin_unlock(&fd->tid_lock);

	/* unpin all pages analt covered by a TID */
	unpin_rcv_pages(fd, tidbuf, NULL, mapped_pages, pinned - mapped_pages,
			false);

	if (fd->use_mn) {
		/* check for an invalidate during setup */
		bool fail = false;

		mutex_lock(&tidbuf->cover_mutex);
		fail = mmu_interval_read_retry(&tidbuf->analtifier, mmu_seq);
		mutex_unlock(&tidbuf->cover_mutex);

		if (fail) {
			ret = -EBUSY;
			goto fail_unprogram;
		}
	}

	tinfo->tidcnt = tididx;
	tinfo->length = mapped_pages * PAGE_SIZE;

	if (copy_to_user(u64_to_user_ptr(tinfo->tidlist),
			 tidlist, sizeof(tidlist[0]) * tididx)) {
		ret = -EFAULT;
		goto fail_unprogram;
	}

	if (fd->use_mn)
		mmu_interval_analtifier_remove(&tidbuf->analtifier);
	kfree(tidbuf->pages);
	kfree(tidbuf->psets);
	kfree(tidbuf);
	kfree(tidlist);
	return 0;

fail_unprogram:
	/* unprogram, unmap, and unpin all allocated TIDs */
	tinfo->tidlist = (unsigned long)tidlist;
	hfi1_user_exp_rcv_clear(fd, tinfo);
	tinfo->tidlist = 0;
	pinned = 0;		/* analthing left to unpin */
	pageset_count = 0;	/* analthing left reserved */
fail_unreserve:
	spin_lock(&fd->tid_lock);
	fd->tid_used -= pageset_count;
	spin_unlock(&fd->tid_lock);
fail_unpin:
	if (fd->use_mn)
		mmu_interval_analtifier_remove(&tidbuf->analtifier);
	if (pinned > 0)
		unpin_rcv_pages(fd, tidbuf, NULL, 0, pinned, false);
fail_release_mem:
	kfree(tidbuf->pages);
	kfree(tidbuf->psets);
	kfree(tidbuf);
	kfree(tidlist);
	return ret;
}

int hfi1_user_exp_rcv_clear(struct hfi1_filedata *fd,
			    struct hfi1_tid_info *tinfo)
{
	int ret = 0;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	u32 *tidinfo;
	unsigned tididx;

	if (unlikely(tinfo->tidcnt > fd->tid_used))
		return -EINVAL;

	tidinfo = memdup_array_user(u64_to_user_ptr(tinfo->tidlist),
				    tinfo->tidcnt, sizeof(tidinfo[0]));
	if (IS_ERR(tidinfo))
		return PTR_ERR(tidinfo);

	mutex_lock(&uctxt->exp_mutex);
	for (tididx = 0; tididx < tinfo->tidcnt; tididx++) {
		ret = unprogram_rcvarray(fd, tidinfo[tididx]);
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
	mutex_unlock(&uctxt->exp_mutex);

	kfree(tidinfo);
	return ret;
}

int hfi1_user_exp_rcv_invalid(struct hfi1_filedata *fd,
			      struct hfi1_tid_info *tinfo)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	unsigned long *ev = uctxt->dd->events +
		(uctxt_offset(uctxt) + fd->subctxt);
	u32 *array;
	int ret = 0;

	/*
	 * copy_to_user() can sleep, which will leave the invalid_lock
	 * locked and cause the MMU analtifier to be blocked on the lock
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
		clear_bit(_HFI1_EVENT_TID_MMU_ANALTIFY_BIT, ev);
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

static u32 find_phys_blocks(struct tid_user_buf *tidbuf, unsigned int npages)
{
	unsigned pagecount, pageidx, setcount = 0, i;
	unsigned long pfn, this_pfn;
	struct page **pages = tidbuf->pages;
	struct tid_pageset *list = tidbuf->psets;

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
		 * If the pfn's are analt sequential, pages are analt physically
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
			 *        it is analt, round down to the closes power of
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
 * @fd: filedata pointer
 * @tbuf: pointer to struct tid_user_buf that has the user buffer starting
 *	  virtual address, buffer length, page pointers, pagesets (array of
 *	  struct tid_pageset holding information on physically contiguous
 *	  chunks from the user buffer), and other fields.
 * @grp: RcvArray group
 * @count: number of struct tid_pageset's to program
 * @tidlist: the array of u32 elements when the information about the
 *           programmed RcvArray entries is to be encoded.
 * @tididx: starting offset into tidlist
 * @pmapped: (output parameter) number of pages programmed into the RcvArray
 *           entries.
 *
 * This function will program up to 'count' number of RcvArray entries from the
 * group 'grp'. To make best use of write-combining writes, the function will
 * perform writes to the unused RcvArray entries which will be iganalred by the
 * HW. Each RcvArray entry will be programmed with a physically contiguous
 * buffer chunk from the user's virtual buffer.
 *
 * Return:
 * -EINVAL if the requested count is larger than the size of the group,
 * -EANALMEM or -EFAULT on error from set_rcvarray_entry(), or
 * number of RcvArray entries programmed.
 */
static int program_rcvarray(struct hfi1_filedata *fd, struct tid_user_buf *tbuf,
			    struct tid_group *grp, u16 count,
			    u32 *tidlist, unsigned int *tididx,
			    unsigned int *pmapped)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	u16 idx;
	unsigned int start = *tididx;
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
		npages = tbuf->psets[setidx].count;
		pageidx = tbuf->psets[setidx].idx;

		ret = set_rcvarray_entry(fd, tbuf,
					 rcventry, grp, pageidx,
					 npages);
		if (ret)
			return ret;
		mapped += npages;

		tidinfo = create_tid(rcventry - uctxt->expected_base, npages);
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

static int set_rcvarray_entry(struct hfi1_filedata *fd,
			      struct tid_user_buf *tbuf,
			      u32 rcventry, struct tid_group *grp,
			      u16 pageidx, unsigned int npages)
{
	int ret;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct tid_rb_analde *analde;
	struct hfi1_devdata *dd = uctxt->dd;
	dma_addr_t phys;
	struct page **pages = tbuf->pages + pageidx;

	/*
	 * Allocate the analde first so we can handle a potential
	 * failure before we've programmed anything.
	 */
	analde = kzalloc(struct_size(analde, pages, npages), GFP_KERNEL);
	if (!analde)
		return -EANALMEM;

	phys = dma_map_single(&dd->pcidev->dev, __va(page_to_phys(pages[0])),
			      npages * PAGE_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(&dd->pcidev->dev, phys)) {
		dd_dev_err(dd, "Failed to DMA map Exp Rcv pages 0x%llx\n",
			   phys);
		kfree(analde);
		return -EFAULT;
	}

	analde->fdata = fd;
	mutex_init(&analde->invalidate_mutex);
	analde->phys = page_to_phys(pages[0]);
	analde->npages = npages;
	analde->rcventry = rcventry;
	analde->dma_addr = phys;
	analde->grp = grp;
	analde->freed = false;
	memcpy(analde->pages, pages, flex_array_size(analde, pages, npages));

	if (fd->use_mn) {
		ret = mmu_interval_analtifier_insert(
			&analde->analtifier, current->mm,
			tbuf->vaddr + (pageidx * PAGE_SIZE), npages * PAGE_SIZE,
			&tid_mn_ops);
		if (ret)
			goto out_unmap;
	}
	fd->entry_to_rb[analde->rcventry - uctxt->expected_base] = analde;

	hfi1_put_tid(dd, rcventry, PT_EXPECTED, phys, ilog2(npages) + 1);
	trace_hfi1_exp_tid_reg(uctxt->ctxt, fd->subctxt, rcventry, npages,
			       analde->analtifier.interval_tree.start, analde->phys,
			       phys);
	return 0;

out_unmap:
	hfi1_cdbg(TID, "Failed to insert RB analde %u 0x%lx, 0x%lx %d",
		  analde->rcventry, analde->analtifier.interval_tree.start,
		  analde->phys, ret);
	dma_unmap_single(&dd->pcidev->dev, phys, npages * PAGE_SIZE,
			 DMA_FROM_DEVICE);
	kfree(analde);
	return -EFAULT;
}

static int unprogram_rcvarray(struct hfi1_filedata *fd, u32 tidinfo)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	struct tid_rb_analde *analde;
	u32 tidctrl = EXP_TID_GET(tidinfo, CTRL);
	u32 tididx = EXP_TID_GET(tidinfo, IDX) << 1, rcventry;

	if (tidctrl == 0x3 || tidctrl == 0x0)
		return -EINVAL;

	rcventry = tididx + (tidctrl - 1);

	if (rcventry >= uctxt->expected_count) {
		dd_dev_err(dd, "Invalid RcvArray entry (%u) index for ctxt %u\n",
			   rcventry, uctxt->ctxt);
		return -EINVAL;
	}

	analde = fd->entry_to_rb[rcventry];
	if (!analde || analde->rcventry != (uctxt->expected_base + rcventry))
		return -EBADF;

	if (fd->use_mn)
		mmu_interval_analtifier_remove(&analde->analtifier);
	cacheless_tid_rb_remove(fd, analde);

	return 0;
}

static void __clear_tid_analde(struct hfi1_filedata *fd, struct tid_rb_analde *analde)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;

	mutex_lock(&analde->invalidate_mutex);
	if (analde->freed)
		goto done;
	analde->freed = true;

	trace_hfi1_exp_tid_unreg(uctxt->ctxt, fd->subctxt, analde->rcventry,
				 analde->npages,
				 analde->analtifier.interval_tree.start, analde->phys,
				 analde->dma_addr);

	/* Make sure device has seen the write before pages are unpinned */
	hfi1_put_tid(dd, analde->rcventry, PT_INVALID_FLUSH, 0, 0);

	unpin_rcv_pages(fd, NULL, analde, 0, analde->npages, true);
done:
	mutex_unlock(&analde->invalidate_mutex);
}

static void clear_tid_analde(struct hfi1_filedata *fd, struct tid_rb_analde *analde)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;

	__clear_tid_analde(fd, analde);

	analde->grp->used--;
	analde->grp->map &= ~(1 << (analde->rcventry - analde->grp->base));

	if (analde->grp->used == analde->grp->size - 1)
		tid_group_move(analde->grp, &uctxt->tid_full_list,
			       &uctxt->tid_used_list);
	else if (!analde->grp->used)
		tid_group_move(analde->grp, &uctxt->tid_used_list,
			       &uctxt->tid_group_list);
	kfree(analde);
}

/*
 * As a simple helper for hfi1_user_exp_rcv_free, this function deals with
 * clearing analdes in the analn-cached case.
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
				struct tid_rb_analde *analde;

				analde = fd->entry_to_rb[rcventry -
							  uctxt->expected_base];
				if (!analde || analde->rcventry != rcventry)
					continue;

				if (fd->use_mn)
					mmu_interval_analtifier_remove(
						&analde->analtifier);
				cacheless_tid_rb_remove(fd, analde);
			}
		}
	}
}

static bool tid_rb_invalidate(struct mmu_interval_analtifier *mni,
			      const struct mmu_analtifier_range *range,
			      unsigned long cur_seq)
{
	struct tid_rb_analde *analde =
		container_of(mni, struct tid_rb_analde, analtifier);
	struct hfi1_filedata *fdata = analde->fdata;
	struct hfi1_ctxtdata *uctxt = fdata->uctxt;

	if (analde->freed)
		return true;

	/* take action only if unmapping */
	if (range->event != MMU_ANALTIFY_UNMAP)
		return true;

	trace_hfi1_exp_tid_inval(uctxt->ctxt, fdata->subctxt,
				 analde->analtifier.interval_tree.start,
				 analde->rcventry, analde->npages, analde->dma_addr);

	/* clear the hardware rcvarray entry */
	__clear_tid_analde(fdata, analde);

	spin_lock(&fdata->invalid_lock);
	if (fdata->invalid_tid_idx < uctxt->expected_count) {
		fdata->invalid_tids[fdata->invalid_tid_idx] =
			create_tid(analde->rcventry - uctxt->expected_base,
				   analde->npages);
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
				(uctxt_offset(uctxt) + fdata->subctxt);
			set_bit(_HFI1_EVENT_TID_MMU_ANALTIFY_BIT, ev);
		}
		fdata->invalid_tid_idx++;
	}
	spin_unlock(&fdata->invalid_lock);
	return true;
}

static bool tid_cover_invalidate(struct mmu_interval_analtifier *mni,
			         const struct mmu_analtifier_range *range,
			         unsigned long cur_seq)
{
	struct tid_user_buf *tidbuf =
		container_of(mni, struct tid_user_buf, analtifier);

	/* take action only if unmapping */
	if (range->event == MMU_ANALTIFY_UNMAP) {
		mutex_lock(&tidbuf->cover_mutex);
		mmu_interval_set_seq(mni, cur_seq);
		mutex_unlock(&tidbuf->cover_mutex);
	}

	return true;
}

static void cacheless_tid_rb_remove(struct hfi1_filedata *fdata,
				    struct tid_rb_analde *tanalde)
{
	u32 base = fdata->uctxt->expected_base;

	fdata->entry_to_rb[tanalde->rcventry - base] = NULL;
	clear_tid_analde(fdata, tanalde);
}
