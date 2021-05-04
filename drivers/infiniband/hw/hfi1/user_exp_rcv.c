/*
 * Copyright(c) 2020 Cornelis Networks, Inc.
 * Copyright(c) 2015-2018 Intel Corporation.
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
				    struct tid_rb_node *tnode);
static bool tid_rb_invalidate(struct mmu_interval_notifier *mni,
			      const struct mmu_notifier_range *range,
			      unsigned long cur_seq);
static int program_rcvarray(struct hfi1_filedata *fd, struct tid_user_buf *,
			    struct tid_group *grp,
			    unsigned int start, u16 count,
			    u32 *tidlist, unsigned int *tididx,
			    unsigned int *pmapped);
static int unprogram_rcvarray(struct hfi1_filedata *fd, u32 tidinfo,
			      struct tid_group **grp);
static void clear_tid_node(struct hfi1_filedata *fd, struct tid_rb_node *node);

static const struct mmu_interval_notifier_ops tid_mn_ops = {
	.invalidate = tid_rb_invalidate,
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
				  sizeof(struct rb_node *),
				  GFP_KERNEL);
	if (!fd->entry_to_rb)
		return -ENOMEM;

	if (!HFI1_CAP_UGET_MASK(uctxt->flags, TID_UNMAP)) {
		fd->invalid_tid_idx = 0;
		fd->invalid_tids = kcalloc(uctxt->expected_count,
					   sizeof(*fd->invalid_tids),
					   GFP_KERNEL);
		if (!fd->invalid_tids) {
			kfree(fd->entry_to_rb);
			fd->entry_to_rb = NULL;
			return -ENOMEM;
		}
		fd->use_mn = true;
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
 * @npages: No of pages to unpin.
 *
 * If the pages have been DMA mapped (indicated by mapped parameter), their
 * info will be passed via a struct tid_rb_node. If they haven't been mapped,
 * their info will be passed via a struct tid_user_buf.
 */
static void unpin_rcv_pages(struct hfi1_filedata *fd,
			    struct tid_user_buf *tidbuf,
			    struct tid_rb_node *node,
			    unsigned int idx,
			    unsigned int npages,
			    bool mapped)
{
	struct page **pages;
	struct hfi1_devdata *dd = fd->uctxt->dd;
	struct mm_struct *mm;

	if (mapped) {
		pci_unmap_single(dd->pcidev, node->dma_addr,
				 node->npages * PAGE_SIZE, PCI_DMA_FROMDEVICE);
		pages = &node->pages[idx];
		mm = mm_from_tid_node(node);
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
	unsigned int npages;
	unsigned long vaddr = tidbuf->vaddr;
	struct page **pages = NULL;
	struct hfi1_devdata *dd = fd->uctxt->dd;

	/* Get the number of pages the user buffer spans */
	npages = num_user_pages(vaddr, tidbuf->length);
	if (!npages)
		return -EINVAL;

	if (npages > fd->uctxt->expected_count) {
		dd_dev_err(dd, "Expected buffer too big\n");
		return -EINVAL;
	}

	/* Allocate the array of struct page pointers needed for pinning */
	pages = kcalloc(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	/*
	 * Pin all the pages of the user buffer. If we can't pin all the
	 * pages, accept the amount pinned so far and program only that.
	 * User space knows how to deal with partially programmed buffers.
	 */
	if (!hfi1_can_pin_pages(dd, current->mm, fd->tid_n_pinned, npages)) {
		kfree(pages);
		return -ENOMEM;
	}

	pinned = hfi1_acquire_user_pages(current->mm, vaddr, npages, true, pages);
	if (pinned <= 0) {
		kfree(pages);
		return pinned;
	}
	tidbuf->pages = pages;
	tidbuf->npages = npages;
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
int hfi1_user_exp_rcv_setup(struct hfi1_filedata *fd,
			    struct hfi1_tid_info *tinfo)
{
	int ret = 0, need_group = 0, pinned;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	unsigned int ngroups, pageidx = 0, pageset_count,
		tididx = 0, mapped, mapped_pages = 0;
	u32 *tidlist = NULL;
	struct tid_user_buf *tidbuf;

	if (!PAGE_ALIGNED(tinfo->vaddr))
		return -EINVAL;

	tidbuf = kzalloc(sizeof(*tidbuf), GFP_KERNEL);
	if (!tidbuf)
		return -ENOMEM;

	tidbuf->vaddr = tinfo->vaddr;
	tidbuf->length = tinfo->length;
	tidbuf->psets = kcalloc(uctxt->expected_count, sizeof(*tidbuf->psets),
				GFP_KERNEL);
	if (!tidbuf->psets) {
		kfree(tidbuf);
		return -ENOMEM;
	}

	pinned = pin_rcv_pages(fd, tidbuf);
	if (pinned <= 0) {
		kfree(tidbuf->psets);
		kfree(tidbuf);
		return pinned;
	}

	/* Find sets of physically contiguous pages */
	tidbuf->n_psets = find_phys_blocks(tidbuf, pinned);

	/*
	 * We don't need to access this under a lock since tid_used is per
	 * process and the same process cannot be in hfi1_user_exp_rcv_clear()
	 * and hfi1_user_exp_rcv_setup() at the same time.
	 */
	spin_lock(&fd->tid_lock);
	if (fd->tid_used + tidbuf->n_psets > fd->tid_limit)
		pageset_count = fd->tid_limit - fd->tid_used;
	else
		pageset_count = tidbuf->n_psets;
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
	mutex_lock(&uctxt->exp_mutex);
	/*
	 * The first step is to program the RcvArray entries which are complete
	 * groups.
	 */
	while (ngroups && uctxt->tid_group_list.count) {
		struct tid_group *grp =
			tid_group_pop(&uctxt->tid_group_list);

		ret = program_rcvarray(fd, tidbuf, grp,
				       pageidx, dd->rcv_entries.group_size,
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

			ret = program_rcvarray(fd, tidbuf, grp,
					       pageidx, use, tidlist,
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
	mutex_unlock(&uctxt->exp_mutex);
nomem:
	hfi1_cdbg(TID, "total mapped: tidpairs:%u pages:%u (%d)", tididx,
		  mapped_pages, ret);
	if (tididx) {
		spin_lock(&fd->tid_lock);
		fd->tid_used += tididx;
		spin_unlock(&fd->tid_lock);
		tinfo->tidcnt = tididx;
		tinfo->length = mapped_pages * PAGE_SIZE;

		if (copy_to_user(u64_to_user_ptr(tinfo->tidlist),
				 tidlist, sizeof(tidlist[0]) * tididx)) {
			/*
			 * On failure to copy to the user level, we need to undo
			 * everything done so far so we don't leak resources.
			 */
			tinfo->tidlist = (unsigned long)&tidlist;
			hfi1_user_exp_rcv_clear(fd, tinfo);
			tinfo->tidlist = 0;
			ret = -EFAULT;
			goto bail;
		}
	}

	/*
	 * If not everything was mapped (due to insufficient RcvArray entries,
	 * for example), unpin all unmapped pages so we can pin them nex time.
	 */
	if (mapped_pages != pinned)
		unpin_rcv_pages(fd, tidbuf, NULL, mapped_pages,
				(pinned - mapped_pages), false);
bail:
	kfree(tidbuf->psets);
	kfree(tidlist);
	kfree(tidbuf->pages);
	kfree(tidbuf);
	return ret > 0 ? 0 : ret;
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

	tidinfo = memdup_user(u64_to_user_ptr(tinfo->tidlist),
			      sizeof(tidinfo[0]) * tinfo->tidcnt);
	if (IS_ERR(tidinfo))
		return PTR_ERR(tidinfo);

	mutex_lock(&uctxt->exp_mutex);
	for (tididx = 0; tididx < tinfo->tidcnt; tididx++) {
		ret = unprogram_rcvarray(fd, tidinfo[tididx], NULL);
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
 * @fd: filedata pointer
 * @tbuf: pointer to struct tid_user_buf that has the user buffer starting
 *	  virtual address, buffer length, page pointers, pagesets (array of
 *	  struct tid_pageset holding information on physically contiguous
 *	  chunks from the user buffer), and other fields.
 * @grp: RcvArray group
 * @start: starting index into sets array
 * @count: number of struct tid_pageset's to program
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
static int program_rcvarray(struct hfi1_filedata *fd, struct tid_user_buf *tbuf,
			    struct tid_group *grp,
			    unsigned int start, u16 count,
			    u32 *tidlist, unsigned int *tididx,
			    unsigned int *pmapped)
{
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
		npages = tbuf->psets[setidx].count;
		pageidx = tbuf->psets[setidx].idx;

		ret = set_rcvarray_entry(fd, tbuf,
					 rcventry, grp, pageidx,
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

static int set_rcvarray_entry(struct hfi1_filedata *fd,
			      struct tid_user_buf *tbuf,
			      u32 rcventry, struct tid_group *grp,
			      u16 pageidx, unsigned int npages)
{
	int ret;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct tid_rb_node *node;
	struct hfi1_devdata *dd = uctxt->dd;
	dma_addr_t phys;
	struct page **pages = tbuf->pages + pageidx;

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

	node->fdata = fd;
	node->phys = page_to_phys(pages[0]);
	node->npages = npages;
	node->rcventry = rcventry;
	node->dma_addr = phys;
	node->grp = grp;
	node->freed = false;
	memcpy(node->pages, pages, sizeof(struct page *) * npages);

	if (fd->use_mn) {
		ret = mmu_interval_notifier_insert(
			&node->notifier, current->mm,
			tbuf->vaddr + (pageidx * PAGE_SIZE), npages * PAGE_SIZE,
			&tid_mn_ops);
		if (ret)
			goto out_unmap;
		/*
		 * FIXME: This is in the wrong order, the notifier should be
		 * established before the pages are pinned by pin_rcv_pages.
		 */
		mmu_interval_read_begin(&node->notifier);
	}
	fd->entry_to_rb[node->rcventry - uctxt->expected_base] = node;

	hfi1_put_tid(dd, rcventry, PT_EXPECTED, phys, ilog2(npages) + 1);
	trace_hfi1_exp_tid_reg(uctxt->ctxt, fd->subctxt, rcventry, npages,
			       node->notifier.interval_tree.start, node->phys,
			       phys);
	return 0;

out_unmap:
	hfi1_cdbg(TID, "Failed to insert RB node %u 0x%lx, 0x%lx %d",
		  node->rcventry, node->notifier.interval_tree.start,
		  node->phys, ret);
	pci_unmap_single(dd->pcidev, phys, npages * PAGE_SIZE,
			 PCI_DMA_FROMDEVICE);
	kfree(node);
	return -EFAULT;
}

static int unprogram_rcvarray(struct hfi1_filedata *fd, u32 tidinfo,
			      struct tid_group **grp)
{
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

	if (fd->use_mn)
		mmu_interval_notifier_remove(&node->notifier);
	cacheless_tid_rb_remove(fd, node);

	return 0;
}

static void clear_tid_node(struct hfi1_filedata *fd, struct tid_rb_node *node)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;

	trace_hfi1_exp_tid_unreg(uctxt->ctxt, fd->subctxt, node->rcventry,
				 node->npages,
				 node->notifier.interval_tree.start, node->phys,
				 node->dma_addr);

	/*
	 * Make sure device has seen the write before we unpin the
	 * pages.
	 */
	hfi1_put_tid(dd, node->rcventry, PT_INVALID_FLUSH, 0, 0);

	unpin_rcv_pages(fd, NULL, node, 0, node->npages, true);

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

				if (fd->use_mn)
					mmu_interval_notifier_remove(
						&node->notifier);
				cacheless_tid_rb_remove(fd, node);
			}
		}
	}
}

static bool tid_rb_invalidate(struct mmu_interval_notifier *mni,
			      const struct mmu_notifier_range *range,
			      unsigned long cur_seq)
{
	struct tid_rb_node *node =
		container_of(mni, struct tid_rb_node, notifier);
	struct hfi1_filedata *fdata = node->fdata;
	struct hfi1_ctxtdata *uctxt = fdata->uctxt;

	if (node->freed)
		return true;

	trace_hfi1_exp_tid_inval(uctxt->ctxt, fdata->subctxt,
				 node->notifier.interval_tree.start,
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
				(uctxt_offset(uctxt) + fdata->subctxt);
			set_bit(_HFI1_EVENT_TID_MMU_NOTIFY_BIT, ev);
		}
		fdata->invalid_tid_idx++;
	}
	spin_unlock(&fdata->invalid_lock);
	return true;
}

static void cacheless_tid_rb_remove(struct hfi1_filedata *fdata,
				    struct tid_rb_node *tnode)
{
	u32 base = fdata->uctxt->expected_base;

	fdata->entry_to_rb[tnode->rcventry - base] = NULL;
	clear_tid_node(fdata, tnode);
}
