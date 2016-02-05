/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
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
 * Copyright(c) 2015 Intel Corporation.
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

struct tid_group {
	struct list_head list;
	unsigned base;
	u8 size;
	u8 used;
	u8 map;
};

struct mmu_rb_node {
	struct rb_node rbnode;
	unsigned long virt;
	unsigned long phys;
	unsigned long len;
	struct tid_group *grp;
	u32 rcventry;
	dma_addr_t dma_addr;
	bool freed;
	unsigned npages;
	struct page *pages[0];
};

enum mmu_call_types {
	MMU_INVALIDATE_PAGE = 0,
	MMU_INVALIDATE_RANGE = 1
};

static const char * const mmu_types[] = {
	"PAGE",
	"RANGE"
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
			    struct rb_root *);
static u32 find_phys_blocks(struct page **, unsigned,
			    struct tid_pageset *) __maybe_unused;
static int set_rcvarray_entry(struct file *, unsigned long, u32,
			      struct tid_group *, struct page **, unsigned);
static inline int mmu_addr_cmp(struct mmu_rb_node *, unsigned long,
			       unsigned long);
static struct mmu_rb_node *mmu_rb_search_by_addr(struct rb_root *,
						 unsigned long);
static inline struct mmu_rb_node *mmu_rb_search_by_entry(struct rb_root *,
							 u32);
static int mmu_rb_insert_by_addr(struct rb_root *, struct mmu_rb_node *);
static int mmu_rb_insert_by_entry(struct rb_root *, struct mmu_rb_node *);
static void mmu_notifier_mem_invalidate(struct mmu_notifier *,
					unsigned long, unsigned long,
					enum mmu_call_types);
static inline void mmu_notifier_page(struct mmu_notifier *, struct mm_struct *,
				     unsigned long);
static inline void mmu_notifier_range_start(struct mmu_notifier *,
					    struct mm_struct *,
					    unsigned long, unsigned long);
static int program_rcvarray(struct file *, unsigned long, struct tid_group *,
			    struct tid_pageset *, unsigned, u16, struct page **,
			    u32 *, unsigned *, unsigned *) __maybe_unused;
static int unprogram_rcvarray(struct file *, u32, struct tid_group **);
static void clear_tid_node(struct hfi1_filedata *, u16, struct mmu_rb_node *);

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

static struct mmu_notifier_ops mn_opts = {
	.invalidate_page = mmu_notifier_page,
	.invalidate_range_start = mmu_notifier_range_start,
};

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

	INIT_HLIST_NODE(&fd->mn.hlist);
	spin_lock_init(&fd->rb_lock);
	spin_lock_init(&fd->tid_lock);
	spin_lock_init(&fd->invalid_lock);
	fd->mn.ops = &mn_opts;
	fd->tid_rb_root = RB_ROOT;

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

	if (!HFI1_CAP_IS_USET(TID_UNMAP)) {
		fd->invalid_tid_idx = 0;
		fd->invalid_tids = kzalloc(uctxt->expected_count *
					   sizeof(u32), GFP_KERNEL);
		if (!fd->invalid_tids) {
			ret = -ENOMEM;
			goto done;
		} else {
			/*
			 * Register MMU notifier callbacks. If the registration
			 * fails, continue but turn off the TID caching for
			 * all user contexts.
			 */
			ret = mmu_notifier_register(&fd->mn, current->mm);
			if (ret) {
				dd_dev_info(dd,
					    "Failed MMU notifier registration %d\n",
					    ret);
				HFI1_CAP_USET(TID_UNMAP);
				ret = 0;
			}
		}
	}

	if (HFI1_CAP_IS_USET(TID_UNMAP))
		fd->mmu_rb_insert = mmu_rb_insert_by_entry;
	else
		fd->mmu_rb_insert = mmu_rb_insert_by_addr;

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
	if (uctxt->subctxt_cnt && !HFI1_CAP_IS_USET(TID_UNMAP)) {
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

int hfi1_user_exp_rcv_free(struct hfi1_filedata *fd)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct tid_group *grp, *gptr;

	/*
	 * The notifier would have been removed when the process'es mm
	 * was freed.
	 */
	if (current->mm && !HFI1_CAP_IS_USET(TID_UNMAP))
		mmu_notifier_unregister(&fd->mn, current->mm);

	kfree(fd->invalid_tids);

	if (!uctxt->cnt) {
		if (!EXP_TID_SET_EMPTY(uctxt->tid_full_list))
			unlock_exp_tids(uctxt, &uctxt->tid_full_list,
					&fd->tid_rb_root);
		if (!EXP_TID_SET_EMPTY(uctxt->tid_used_list))
			unlock_exp_tids(uctxt, &uctxt->tid_used_list,
					&fd->tid_rb_root);
		list_for_each_entry_safe(grp, gptr, &uctxt->tid_group_list.list,
					 list) {
			list_del_init(&grp->list);
			kfree(grp);
		}
		spin_lock(&fd->rb_lock);
		if (!RB_EMPTY_ROOT(&fd->tid_rb_root)) {
			struct rb_node *node;
			struct mmu_rb_node *rbnode;

			while ((node = rb_first(&fd->tid_rb_root))) {
				rbnode = rb_entry(node, struct mmu_rb_node,
						  rbnode);
				rb_erase(&rbnode->rbnode, &fd->tid_rb_root);
				kfree(rbnode);
			}
		}
		spin_unlock(&fd->rb_lock);
		hfi1_clear_tids(uctxt);
	}
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

int hfi1_user_exp_rcv_setup(struct file *fp, struct hfi1_tid_info *tinfo)
{
	return -EINVAL;
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
	struct mmu_rb_node *node;
	struct hfi1_devdata *dd = uctxt->dd;
	struct rb_root *root = &fd->tid_rb_root;
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

	node->virt = vaddr;
	node->phys = page_to_phys(pages[0]);
	node->len = npages * PAGE_SIZE;
	node->npages = npages;
	node->rcventry = rcventry;
	node->dma_addr = phys;
	node->grp = grp;
	node->freed = false;
	memcpy(node->pages, pages, sizeof(struct page *) * npages);

	spin_lock(&fd->rb_lock);
	ret = fd->mmu_rb_insert(root, node);
	spin_unlock(&fd->rb_lock);

	if (ret) {
		hfi1_cdbg(TID, "Failed to insert RB node %u 0x%lx, 0x%lx %d",
			  node->rcventry, node->virt, node->phys, ret);
		pci_unmap_single(dd->pcidev, phys, npages * PAGE_SIZE,
				 PCI_DMA_FROMDEVICE);
		kfree(node);
		return -EFAULT;
	}
	hfi1_put_tid(dd, rcventry, PT_EXPECTED, phys, ilog2(npages) + 1);
	return 0;
}

static int unprogram_rcvarray(struct file *fp, u32 tidinfo,
			      struct tid_group **grp)
{
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	struct mmu_rb_node *node;
	u8 tidctrl = EXP_TID_GET(tidinfo, CTRL);
	u32 tidbase = uctxt->expected_base,
		tididx = EXP_TID_GET(tidinfo, IDX) << 1, rcventry;

	if (tididx >= uctxt->expected_count) {
		dd_dev_err(dd, "Invalid RcvArray entry (%u) index for ctxt %u\n",
			   tididx, uctxt->ctxt);
		return -EINVAL;
	}

	if (tidctrl == 0x3)
		return -EINVAL;

	rcventry = tidbase + tididx + (tidctrl - 1);

	spin_lock(&fd->rb_lock);
	node = mmu_rb_search_by_entry(&fd->tid_rb_root, rcventry);
	if (!node) {
		spin_unlock(&fd->rb_lock);
		return -EBADF;
	}
	rb_erase(&node->rbnode, &fd->tid_rb_root);
	spin_unlock(&fd->rb_lock);
	if (grp)
		*grp = node->grp;
	clear_tid_node(fd, fd->subctxt, node);
	return 0;
}

static void clear_tid_node(struct hfi1_filedata *fd, u16 subctxt,
			   struct mmu_rb_node *node)
{
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;

	hfi1_put_tid(dd, node->rcventry, PT_INVALID, 0, 0);
	/*
	 * Make sure device has seen the write before we unpin the
	 * pages.
	 */
	flush_wc();

	pci_unmap_single(dd->pcidev, node->dma_addr, node->len,
			 PCI_DMA_FROMDEVICE);
	hfi1_release_user_pages(node->pages, node->npages, true);

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

static void unlock_exp_tids(struct hfi1_ctxtdata *uctxt,
			    struct exp_tid_set *set, struct rb_root *root)
{
	struct tid_group *grp, *ptr;
	struct hfi1_filedata *fd = container_of(root, struct hfi1_filedata,
						tid_rb_root);
	int i;

	list_for_each_entry_safe(grp, ptr, &set->list, list) {
		list_del_init(&grp->list);

		spin_lock(&fd->rb_lock);
		for (i = 0; i < grp->size; i++) {
			if (grp->map & (1 << i)) {
				u16 rcventry = grp->base + i;
				struct mmu_rb_node *node;

				node = mmu_rb_search_by_entry(root, rcventry);
				if (!node)
					continue;
				rb_erase(&node->rbnode, root);
				clear_tid_node(fd, -1, node);
			}
		}
		spin_unlock(&fd->rb_lock);
	}
}

static inline void mmu_notifier_page(struct mmu_notifier *mn,
				     struct mm_struct *mm, unsigned long addr)
{
	mmu_notifier_mem_invalidate(mn, addr, addr + PAGE_SIZE,
				    MMU_INVALIDATE_PAGE);
}

static inline void mmu_notifier_range_start(struct mmu_notifier *mn,
					    struct mm_struct *mm,
					    unsigned long start,
					    unsigned long end)
{
	mmu_notifier_mem_invalidate(mn, start, end, MMU_INVALIDATE_RANGE);
}

static void mmu_notifier_mem_invalidate(struct mmu_notifier *mn,
					unsigned long start, unsigned long end,
					enum mmu_call_types type)
{
	struct hfi1_filedata *fd = container_of(mn, struct hfi1_filedata, mn);
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct rb_root *root = &fd->tid_rb_root;
	struct mmu_rb_node *node;
	unsigned long addr = start;

	spin_lock(&fd->rb_lock);
	while (addr < end) {
		node = mmu_rb_search_by_addr(root, addr);

		if (!node) {
			/*
			 * Didn't find a node at this address. However, the
			 * range could be bigger than what we have registered
			 * so we have to keep looking.
			 */
			addr += PAGE_SIZE;
			continue;
		}

		/*
		 * The next address to be looked up is computed based
		 * on the node's starting address. This is due to the
		 * fact that the range where we start might be in the
		 * middle of the node's buffer so simply incrementing
		 * the address by the node's size would result is a
		 * bad address.
		 */
		addr = node->virt + (node->npages * PAGE_SIZE);
		if (node->freed)
			continue;

		node->freed = true;

		spin_lock(&fd->invalid_lock);
		if (fd->invalid_tid_idx < uctxt->expected_count) {
			fd->invalid_tids[fd->invalid_tid_idx] =
				rcventry2tidinfo(node->rcventry -
						 uctxt->expected_base);
			fd->invalid_tids[fd->invalid_tid_idx] |=
				EXP_TID_SET(LEN, node->npages);
			if (!fd->invalid_tid_idx) {
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
					(((uctxt->ctxt -
					   uctxt->dd->first_user_ctxt) *
					  HFI1_MAX_SHARED_CTXTS) + fd->subctxt);
				set_bit(_HFI1_EVENT_TID_MMU_NOTIFY_BIT, ev);
			}
			fd->invalid_tid_idx++;
		}
		spin_unlock(&fd->invalid_lock);
	}
	spin_unlock(&fd->rb_lock);
}

static inline int mmu_addr_cmp(struct mmu_rb_node *node, unsigned long addr,
			       unsigned long len)
{
	if ((addr + len) <= node->virt)
		return -1;
	else if (addr >= node->virt && addr < (node->virt + node->len))
		return 0;
	else
		return 1;
}

static inline int mmu_entry_cmp(struct mmu_rb_node *node, u32 entry)
{
	if (entry < node->rcventry)
		return -1;
	else if (entry > node->rcventry)
		return 1;
	else
		return 0;
}

static struct mmu_rb_node *mmu_rb_search_by_addr(struct rb_root *root,
						 unsigned long addr)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct mmu_rb_node *mnode =
			container_of(node, struct mmu_rb_node, rbnode);
		/*
		 * When searching, use at least one page length for size. The
		 * MMU notifier will not give us anything less than that. We
		 * also don't need anything more than a page because we are
		 * guaranteed to have non-overlapping buffers in the tree.
		 */
		int result = mmu_addr_cmp(mnode, addr, PAGE_SIZE);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return mnode;
	}
	return NULL;
}

static inline struct mmu_rb_node *mmu_rb_search_by_entry(struct rb_root *root,
							 u32 index)
{
	struct mmu_rb_node *rbnode;
	struct rb_node *node;

	if (root && !RB_EMPTY_ROOT(root))
		for (node = rb_first(root); node; node = rb_next(node)) {
			rbnode = rb_entry(node, struct mmu_rb_node, rbnode);
			if (rbnode->rcventry == index)
				return rbnode;
		}
	return NULL;
}

static int mmu_rb_insert_by_entry(struct rb_root *root,
				  struct mmu_rb_node *node)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;

	while (*new) {
		struct mmu_rb_node *this =
			container_of(*new, struct mmu_rb_node, rbnode);
		int result = mmu_entry_cmp(this, node->rcventry);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return 1;
	}

	rb_link_node(&node->rbnode, parent, new);
	rb_insert_color(&node->rbnode, root);
	return 0;
}

static int mmu_rb_insert_by_addr(struct rb_root *root, struct mmu_rb_node *node)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct mmu_rb_node *this =
			container_of(*new, struct mmu_rb_node, rbnode);
		int result = mmu_addr_cmp(this, node->virt, node->len);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return 1;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&node->rbnode, parent, new);
	rb_insert_color(&node->rbnode, root);

	return 0;
}
