/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2013 Cisco Systems.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/hugetlb.h>
#include <linux/iommu.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <rdma/ib_verbs.h>

#include "usnic_log.h"
#include "usnic_uiom.h"
#include "usnic_uiom_interval_tree.h"

#define USNIC_UIOM_PAGE_CHUNK						\
	((PAGE_SIZE - offsetof(struct usnic_uiom_chunk, page_list))	/\
	((void *) &((struct usnic_uiom_chunk *) 0)->page_list[1] -	\
	(void *) &((struct usnic_uiom_chunk *) 0)->page_list[0]))

static int usnic_uiom_dma_fault(struct iommu_domain *domain,
				struct device *dev,
				unsigned long iova, int flags,
				void *token)
{
	usnic_err("Device %s iommu fault domain 0x%pK va 0x%lx flags 0x%x\n",
		dev_name(dev),
		domain, iova, flags);
	return -ENOSYS;
}

static void usnic_uiom_put_pages(struct list_head *chunk_list, int dirty)
{
	struct usnic_uiom_chunk *chunk, *tmp;
	struct page *page;
	struct scatterlist *sg;
	int i;
	dma_addr_t pa;

	list_for_each_entry_safe(chunk, tmp, chunk_list, list) {
		for_each_sg(chunk->page_list, sg, chunk->nents, i) {
			page = sg_page(sg);
			pa = sg_phys(sg);
			unpin_user_pages_dirty_lock(&page, 1, dirty);
			usnic_dbg("pa: %pa\n", &pa);
		}
		kfree(chunk);
	}
}

static int usnic_uiom_get_pages(unsigned long addr, size_t size, int writable,
				int dmasync, struct usnic_uiom_reg *uiomr)
{
	struct list_head *chunk_list = &uiomr->chunk_list;
	unsigned int gup_flags = FOLL_LONGTERM;
	struct page **page_list;
	struct scatterlist *sg;
	struct usnic_uiom_chunk *chunk;
	unsigned long locked;
	unsigned long lock_limit;
	unsigned long cur_base;
	unsigned long npages;
	int ret;
	int off;
	int i;
	dma_addr_t pa;
	struct mm_struct *mm;

	/*
	 * If the combination of the addr and size requested for this memory
	 * region causes an integer overflow, return error.
	 */
	if (((addr + size) < addr) || PAGE_ALIGN(addr + size) < (addr + size))
		return -EINVAL;

	if (!size)
		return -EINVAL;

	if (!can_do_mlock())
		return -EPERM;

	INIT_LIST_HEAD(chunk_list);

	page_list = (struct page **) __get_free_page(GFP_KERNEL);
	if (!page_list)
		return -ENOMEM;

	npages = PAGE_ALIGN(size + (addr & ~PAGE_MASK)) >> PAGE_SHIFT;

	uiomr->owning_mm = mm = current->mm;
	mmap_read_lock(mm);

	locked = atomic64_add_return(npages, &current->mm->pinned_vm);
	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	if ((locked > lock_limit) && !capable(CAP_IPC_LOCK)) {
		ret = -ENOMEM;
		goto out;
	}

	if (writable)
		gup_flags |= FOLL_WRITE;
	cur_base = addr & PAGE_MASK;
	ret = 0;

	while (npages) {
		ret = pin_user_pages(cur_base,
				     min_t(unsigned long, npages,
				     PAGE_SIZE / sizeof(struct page *)),
				     gup_flags, page_list, NULL);

		if (ret < 0)
			goto out;

		npages -= ret;
		off = 0;

		while (ret) {
			chunk = kmalloc(struct_size(chunk, page_list,
					min_t(int, ret, USNIC_UIOM_PAGE_CHUNK)),
					GFP_KERNEL);
			if (!chunk) {
				ret = -ENOMEM;
				goto out;
			}

			chunk->nents = min_t(int, ret, USNIC_UIOM_PAGE_CHUNK);
			sg_init_table(chunk->page_list, chunk->nents);
			for_each_sg(chunk->page_list, sg, chunk->nents, i) {
				sg_set_page(sg, page_list[i + off],
						PAGE_SIZE, 0);
				pa = sg_phys(sg);
				usnic_dbg("va: 0x%lx pa: %pa\n",
						cur_base + i*PAGE_SIZE, &pa);
			}
			cur_base += chunk->nents * PAGE_SIZE;
			ret -= chunk->nents;
			off += chunk->nents;
			list_add_tail(&chunk->list, chunk_list);
		}

		ret = 0;
	}

out:
	if (ret < 0) {
		usnic_uiom_put_pages(chunk_list, 0);
		atomic64_sub(npages, &current->mm->pinned_vm);
	} else
		mmgrab(uiomr->owning_mm);

	mmap_read_unlock(mm);
	free_page((unsigned long) page_list);
	return ret;
}

static void usnic_uiom_unmap_sorted_intervals(struct list_head *intervals,
						struct usnic_uiom_pd *pd)
{
	struct usnic_uiom_interval_node *interval, *tmp;
	long unsigned va, size;

	list_for_each_entry_safe(interval, tmp, intervals, link) {
		va = interval->start << PAGE_SHIFT;
		size = ((interval->last - interval->start) + 1) << PAGE_SHIFT;
		while (size > 0) {
			/* Workaround for RH 970401 */
			usnic_dbg("va 0x%lx size 0x%lx", va, PAGE_SIZE);
			iommu_unmap(pd->domain, va, PAGE_SIZE);
			va += PAGE_SIZE;
			size -= PAGE_SIZE;
		}
	}
}

static void __usnic_uiom_reg_release(struct usnic_uiom_pd *pd,
					struct usnic_uiom_reg *uiomr,
					int dirty)
{
	int npages;
	unsigned long vpn_start, vpn_last;
	struct usnic_uiom_interval_node *interval, *tmp;
	int writable = 0;
	LIST_HEAD(rm_intervals);

	npages = PAGE_ALIGN(uiomr->length + uiomr->offset) >> PAGE_SHIFT;
	vpn_start = (uiomr->va & PAGE_MASK) >> PAGE_SHIFT;
	vpn_last = vpn_start + npages - 1;

	spin_lock(&pd->lock);
	usnic_uiom_remove_interval(&pd->root, vpn_start,
					vpn_last, &rm_intervals);
	usnic_uiom_unmap_sorted_intervals(&rm_intervals, pd);

	list_for_each_entry_safe(interval, tmp, &rm_intervals, link) {
		if (interval->flags & IOMMU_WRITE)
			writable = 1;
		list_del(&interval->link);
		kfree(interval);
	}

	usnic_uiom_put_pages(&uiomr->chunk_list, dirty & writable);
	spin_unlock(&pd->lock);
}

static int usnic_uiom_map_sorted_intervals(struct list_head *intervals,
						struct usnic_uiom_reg *uiomr)
{
	int i, err;
	size_t size;
	struct usnic_uiom_chunk *chunk;
	struct usnic_uiom_interval_node *interval_node;
	dma_addr_t pa;
	dma_addr_t pa_start = 0;
	dma_addr_t pa_end = 0;
	long int va_start = -EINVAL;
	struct usnic_uiom_pd *pd = uiomr->pd;
	long int va = uiomr->va & PAGE_MASK;
	int flags = IOMMU_READ | IOMMU_CACHE;

	flags |= (uiomr->writable) ? IOMMU_WRITE : 0;
	chunk = list_first_entry(&uiomr->chunk_list, struct usnic_uiom_chunk,
									list);
	list_for_each_entry(interval_node, intervals, link) {
iter_chunk:
		for (i = 0; i < chunk->nents; i++, va += PAGE_SIZE) {
			pa = sg_phys(&chunk->page_list[i]);
			if ((va >> PAGE_SHIFT) < interval_node->start)
				continue;

			if ((va >> PAGE_SHIFT) == interval_node->start) {
				/* First page of the interval */
				va_start = va;
				pa_start = pa;
				pa_end = pa;
			}

			WARN_ON(va_start == -EINVAL);

			if ((pa_end + PAGE_SIZE != pa) &&
					(pa != pa_start)) {
				/* PAs are not contiguous */
				size = pa_end - pa_start + PAGE_SIZE;
				usnic_dbg("va 0x%lx pa %pa size 0x%zx flags 0x%x",
					va_start, &pa_start, size, flags);
				err = iommu_map(pd->domain, va_start, pa_start,
						size, flags, GFP_ATOMIC);
				if (err) {
					usnic_err("Failed to map va 0x%lx pa %pa size 0x%zx with err %d\n",
						va_start, &pa_start, size, err);
					goto err_out;
				}
				va_start = va;
				pa_start = pa;
				pa_end = pa;
			}

			if ((va >> PAGE_SHIFT) == interval_node->last) {
				/* Last page of the interval */
				size = pa - pa_start + PAGE_SIZE;
				usnic_dbg("va 0x%lx pa %pa size 0x%zx flags 0x%x\n",
					va_start, &pa_start, size, flags);
				err = iommu_map(pd->domain, va_start, pa_start,
						size, flags, GFP_ATOMIC);
				if (err) {
					usnic_err("Failed to map va 0x%lx pa %pa size 0x%zx with err %d\n",
						va_start, &pa_start, size, err);
					goto err_out;
				}
				break;
			}

			if (pa != pa_start)
				pa_end += PAGE_SIZE;
		}

		if (i == chunk->nents) {
			/*
			 * Hit last entry of the chunk,
			 * hence advance to next chunk
			 */
			chunk = list_first_entry(&chunk->list,
							struct usnic_uiom_chunk,
							list);
			goto iter_chunk;
		}
	}

	return 0;

err_out:
	usnic_uiom_unmap_sorted_intervals(intervals, pd);
	return err;
}

struct usnic_uiom_reg *usnic_uiom_reg_get(struct usnic_uiom_pd *pd,
						unsigned long addr, size_t size,
						int writable, int dmasync)
{
	struct usnic_uiom_reg *uiomr;
	unsigned long va_base, vpn_start, vpn_last;
	unsigned long npages;
	int offset, err;
	LIST_HEAD(sorted_diff_intervals);

	/*
	 * Intel IOMMU map throws an error if a translation entry is
	 * changed from read to write.  This module may not unmap
	 * and then remap the entry after fixing the permission
	 * b/c this open up a small windows where hw DMA may page fault
	 * Hence, make all entries to be writable.
	 */
	writable = 1;

	va_base = addr & PAGE_MASK;
	offset = addr & ~PAGE_MASK;
	npages = PAGE_ALIGN(size + offset) >> PAGE_SHIFT;
	vpn_start = (addr & PAGE_MASK) >> PAGE_SHIFT;
	vpn_last = vpn_start + npages - 1;

	uiomr = kmalloc(sizeof(*uiomr), GFP_KERNEL);
	if (!uiomr)
		return ERR_PTR(-ENOMEM);

	uiomr->va = va_base;
	uiomr->offset = offset;
	uiomr->length = size;
	uiomr->writable = writable;
	uiomr->pd = pd;

	err = usnic_uiom_get_pages(addr, size, writable, dmasync,
				   uiomr);
	if (err) {
		usnic_err("Failed get_pages vpn [0x%lx,0x%lx] err %d\n",
				vpn_start, vpn_last, err);
		goto out_free_uiomr;
	}

	spin_lock(&pd->lock);
	err = usnic_uiom_get_intervals_diff(vpn_start, vpn_last,
						(writable) ? IOMMU_WRITE : 0,
						IOMMU_WRITE,
						&pd->root,
						&sorted_diff_intervals);
	if (err) {
		usnic_err("Failed disjoint interval vpn [0x%lx,0x%lx] err %d\n",
						vpn_start, vpn_last, err);
		goto out_put_pages;
	}

	err = usnic_uiom_map_sorted_intervals(&sorted_diff_intervals, uiomr);
	if (err) {
		usnic_err("Failed map interval vpn [0x%lx,0x%lx] err %d\n",
						vpn_start, vpn_last, err);
		goto out_put_intervals;

	}

	err = usnic_uiom_insert_interval(&pd->root, vpn_start, vpn_last,
					(writable) ? IOMMU_WRITE : 0);
	if (err) {
		usnic_err("Failed insert interval vpn [0x%lx,0x%lx] err %d\n",
						vpn_start, vpn_last, err);
		goto out_unmap_intervals;
	}

	usnic_uiom_put_interval_set(&sorted_diff_intervals);
	spin_unlock(&pd->lock);

	return uiomr;

out_unmap_intervals:
	usnic_uiom_unmap_sorted_intervals(&sorted_diff_intervals, pd);
out_put_intervals:
	usnic_uiom_put_interval_set(&sorted_diff_intervals);
out_put_pages:
	usnic_uiom_put_pages(&uiomr->chunk_list, 0);
	spin_unlock(&pd->lock);
	mmdrop(uiomr->owning_mm);
out_free_uiomr:
	kfree(uiomr);
	return ERR_PTR(err);
}

static void __usnic_uiom_release_tail(struct usnic_uiom_reg *uiomr)
{
	mmdrop(uiomr->owning_mm);
	kfree(uiomr);
}

static inline size_t usnic_uiom_num_pages(struct usnic_uiom_reg *uiomr)
{
	return PAGE_ALIGN(uiomr->length + uiomr->offset) >> PAGE_SHIFT;
}

void usnic_uiom_reg_release(struct usnic_uiom_reg *uiomr)
{
	__usnic_uiom_reg_release(uiomr->pd, uiomr, 1);

	atomic64_sub(usnic_uiom_num_pages(uiomr), &uiomr->owning_mm->pinned_vm);
	__usnic_uiom_release_tail(uiomr);
}

struct usnic_uiom_pd *usnic_uiom_alloc_pd(struct device *dev)
{
	struct usnic_uiom_pd *pd;
	void *domain;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	pd->domain = domain = iommu_domain_alloc(dev->bus);
	if (!domain) {
		usnic_err("Failed to allocate IOMMU domain");
		kfree(pd);
		return ERR_PTR(-ENOMEM);
	}

	iommu_set_fault_handler(pd->domain, usnic_uiom_dma_fault, NULL);

	spin_lock_init(&pd->lock);
	INIT_LIST_HEAD(&pd->devs);

	return pd;
}

void usnic_uiom_dealloc_pd(struct usnic_uiom_pd *pd)
{
	iommu_domain_free(pd->domain);
	kfree(pd);
}

int usnic_uiom_attach_dev_to_pd(struct usnic_uiom_pd *pd, struct device *dev)
{
	struct usnic_uiom_dev *uiom_dev;
	int err;

	uiom_dev = kzalloc(sizeof(*uiom_dev), GFP_ATOMIC);
	if (!uiom_dev)
		return -ENOMEM;
	uiom_dev->dev = dev;

	err = iommu_attach_device(pd->domain, dev);
	if (err)
		goto out_free_dev;

	if (!device_iommu_capable(dev, IOMMU_CAP_CACHE_COHERENCY)) {
		usnic_err("IOMMU of %s does not support cache coherency\n",
				dev_name(dev));
		err = -EINVAL;
		goto out_detach_device;
	}

	spin_lock(&pd->lock);
	list_add_tail(&uiom_dev->link, &pd->devs);
	pd->dev_cnt++;
	spin_unlock(&pd->lock);

	return 0;

out_detach_device:
	iommu_detach_device(pd->domain, dev);
out_free_dev:
	kfree(uiom_dev);
	return err;
}

void usnic_uiom_detach_dev_from_pd(struct usnic_uiom_pd *pd, struct device *dev)
{
	struct usnic_uiom_dev *uiom_dev;
	int found = 0;

	spin_lock(&pd->lock);
	list_for_each_entry(uiom_dev, &pd->devs, link) {
		if (uiom_dev->dev == dev) {
			found = 1;
			break;
		}
	}

	if (!found) {
		usnic_err("Unable to free dev %s - not found\n",
				dev_name(dev));
		spin_unlock(&pd->lock);
		return;
	}

	list_del(&uiom_dev->link);
	pd->dev_cnt--;
	spin_unlock(&pd->lock);

	return iommu_detach_device(pd->domain, dev);
}

struct device **usnic_uiom_get_dev_list(struct usnic_uiom_pd *pd)
{
	struct usnic_uiom_dev *uiom_dev;
	struct device **devs;
	int i = 0;

	spin_lock(&pd->lock);
	devs = kcalloc(pd->dev_cnt + 1, sizeof(*devs), GFP_ATOMIC);
	if (!devs) {
		devs = ERR_PTR(-ENOMEM);
		goto out;
	}

	list_for_each_entry(uiom_dev, &pd->devs, link) {
		devs[i++] = uiom_dev->dev;
	}
out:
	spin_unlock(&pd->lock);
	return devs;
}

void usnic_uiom_free_dev_list(struct device **devs)
{
	kfree(devs);
}
