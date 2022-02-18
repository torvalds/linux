// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Cerf Yu <cerf.yu@rock-chips.com>
 */

#define pr_fmt(fmt) "rga_mm: " fmt

#include "rga.h"
#include "rga_job.h"
#include "rga_mm.h"
#include "rga_dma_buf.h"
#include "rga_common.h"

static void rga_current_mm_read_lock(struct mm_struct *mm)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	mmap_read_lock(mm);
#else
	down_read(&mm->mmap_sem);
#endif
}

static void rga_current_mm_read_unlock(struct mm_struct *mm)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	mmap_read_unlock(mm);
#else
	up_read(&mm->mmap_sem);
#endif
}

static int rga_get_user_pages_from_vma(struct page **pages, unsigned long Memory,
				       uint32_t pageCount, struct mm_struct *current_mm)
{
	int ret = 0;
	int i;
	struct vm_area_struct *vma;
	spinlock_t *ptl;
	pte_t *pte;
	pgd_t *pgd;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	p4d_t *p4d;
#endif
	pud_t *pud;
	pmd_t *pmd;
	unsigned long pfn;

	for (i = 0; i < pageCount; i++) {
		vma = find_vma(current_mm, (Memory + i) << PAGE_SHIFT);
		if (!vma) {
			pr_err("failed to get vma\n");
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}

		pgd = pgd_offset(current_mm, (Memory + i) << PAGE_SHIFT);
		if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd))) {
			pr_err("failed to get pgd\n");
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
		/*
		 * In the four-level page table,
		 * it will do nothing and return pgd.
		 */
		p4d = p4d_offset(pgd, (Memory + i) << PAGE_SHIFT);
		if (p4d_none(*p4d) || unlikely(p4d_bad(*p4d))) {
			pr_err("failed to get p4d\n");
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}

		pud = pud_offset(p4d, (Memory + i) << PAGE_SHIFT);
#else
		pud = pud_offset(pgd, (Memory + i) << PAGE_SHIFT);
#endif

		if (pud_none(*pud) || unlikely(pud_bad(*pud))) {
			pr_err("failed to get pud\n");
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}
		pmd = pmd_offset(pud, (Memory + i) << PAGE_SHIFT);
		if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd))) {
			pr_err("failed to get pmd\n");
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}
		pte = pte_offset_map_lock(current_mm, pmd,
					  (Memory + i) << PAGE_SHIFT, &ptl);
		if (pte_none(*pte)) {
			pr_err("failed to get pte\n");
			pte_unmap_unlock(pte, ptl);
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}

		pfn = pte_pfn(*pte);
		pages[i] = pfn_to_page(pfn);
		pte_unmap_unlock(pte, ptl);
	}

	return ret;
}

static int rga_get_user_pages(struct page **pages, unsigned long Memory,
			      uint32_t pageCount, int writeFlag,
			      struct mm_struct *current_mm)
{
	uint32_t i;
	int32_t ret = 0;
	int32_t result;

	rga_current_mm_read_lock(current_mm);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 168) && \
    LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
	result = get_user_pages(current, current_mm, Memory << PAGE_SHIFT,
				pageCount, writeFlag ? FOLL_WRITE : 0,
				pages, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	result = get_user_pages(current, current_mm, Memory << PAGE_SHIFT,
				pageCount, writeFlag, 0, pages, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	result = get_user_pages_remote(current, current_mm,
				       Memory << PAGE_SHIFT,
				       pageCount, writeFlag, pages, NULL, NULL);
#else
	result = get_user_pages_remote(current_mm, Memory << PAGE_SHIFT,
				       pageCount, writeFlag, pages, NULL, NULL);
#endif

	if (result > 0 && result >= pageCount) {
		ret = result;
	} else {
		if (result > 0)
			for (i = 0; i < result; i++)
				put_page(pages[i]);

		ret = rga_get_user_pages_from_vma(pages, Memory, pageCount, current_mm);
		if (ret < 0) {
			pr_err("Can not get user pages from vma, result = %d, pagecount = %d\n",
			       result, pageCount);
		}
	}

	rga_current_mm_read_unlock(current_mm);

	return ret;
}

static void rga_free_sgt(struct rga_dma_buffer *virt_dma_buf)
{
	if (virt_dma_buf->sgt == NULL)
		return;

	sg_free_table(virt_dma_buf->sgt);
	kfree(virt_dma_buf->sgt);
	virt_dma_buf->sgt = NULL;
}

static int rga_alloc_sgt(struct rga_virt_addr *virt_addr,
			 struct rga_dma_buffer *virt_dma_buf)
{
	int ret;
	struct sg_table *sgt = NULL;

	sgt = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (sgt == NULL) {
		pr_err("%s alloc sgt error!\n", __func__);
		return -ENOMEM;
	}

	/* get sg form pages. */
	if (sg_alloc_table_from_pages(sgt, virt_addr->pages,
				      virt_addr->page_count, 0,
				      virt_addr->size, GFP_KERNEL)) {
		pr_err("sg_alloc_table_from_pages failed");
		ret = -ENOMEM;
		goto out_free_sgt;
	}

	virt_dma_buf->sgt = sgt;
	virt_dma_buf->size = virt_addr->size;
	virt_dma_buf->offset = virt_addr->offset;

	return 0;

out_free_sgt:
	kfree(sgt);

	return ret;
}

static void rga_free_virt_addr(struct rga_virt_addr **virt_addr_p)
{
	int i;
	struct rga_virt_addr *virt_addr = NULL;

	if (virt_addr_p == NULL)
		return;

	virt_addr = *virt_addr_p;
	if (virt_addr == NULL)
		return;

	for (i = 0; i < virt_addr->result; i++)
		put_page(virt_addr->pages[i]);

	free_pages((unsigned long)virt_addr->pages, virt_addr->pages_order);
	kfree(virt_addr);
	*virt_addr_p = NULL;
}

static int rga_alloc_virt_addr(struct rga_virt_addr **virt_addr_p,
			       uint64_t viraddr,
			       struct rga_memory_parm *memory_parm,
			       int writeFlag,
			       struct mm_struct *mm)
{
	int i;
	int ret;
	int result = 0;
	int order;
	unsigned int count;
	unsigned long start_addr;
	unsigned long size;
	struct page **pages = NULL;
	struct rga_virt_addr *virt_addr = NULL;

	/* Calculate page size. */
	count = rga_buf_size_cal(viraddr, viraddr, viraddr, memory_parm->format,
				 memory_parm->width, memory_parm->height,
				 &start_addr, NULL);
	size = count * PAGE_SIZE;

	/* alloc pages and page_table */
	order = get_order(count * sizeof(struct page *));
	pages = (struct page **)__get_free_pages(GFP_KERNEL, order);
	if (pages == NULL) {
		pr_err("%s can not alloc pages for pages\n", __func__);
		return -ENOMEM;
	}

	/* get pages from virtual address. */
	ret = rga_get_user_pages(pages, start_addr, count, writeFlag, mm);
	if (ret < 0) {
		pr_err("failed to get pages");
		ret = -EINVAL;
		goto out_free_pages;
	} else if (ret > 0) {
		/* For put pages */
		result = ret;
	}

	*virt_addr_p = kzalloc(sizeof(struct rga_virt_addr), GFP_KERNEL);
	if (*virt_addr_p == NULL) {
		pr_err("%s alloc virt_addr error!\n", __func__);
		ret = -ENOMEM;
		goto out_put_and_free_pages;
	}
	virt_addr = *virt_addr_p;

	virt_addr->addr = viraddr;
	virt_addr->pages = pages;
	virt_addr->pages_order = order;
	virt_addr->page_count = count;
	virt_addr->size = size;
	virt_addr->offset = viraddr & (~PAGE_MASK);
	virt_addr->result = result;

	return 0;

out_put_and_free_pages:
	for (i = 0; i < result; i++)
		put_page(pages[i]);
out_free_pages:
	free_pages((unsigned long)pages, order);

	return ret;
}

/* If it is within 0~4G, return 1 (true). */
static int rga_mm_check_range_sgt(struct sg_table *sgt)
{
	int i;
	struct scatterlist *sg;
	phys_addr_t s_phys = 0;

	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i) {
		s_phys = sg_phys(sg);
		if ((s_phys > 0xffffffff) || (s_phys + sg->length > 0xffffffff))
			return 0;
	}

	return 1;
}

static inline bool rga_mm_check_contiguous_sgt(struct sg_table *sgt)
{
	if (sgt->orig_nents == 1)
		return true;

	return false;
}

static void rga_mm_unmap_dma_buffer(struct rga_internal_buffer *internal_buffer)
{
	int i;

	for (i = 0; i < internal_buffer->dma_buffer_size; i++) {
		rga_dma_unmap_fd(&internal_buffer->dma_buffer[i]);

		if (i == 0 &&
		    internal_buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS &&
		    internal_buffer->phys_addr > 0) {
			internal_buffer->phys_addr = 0;
			break;
		}
	}

	kfree(internal_buffer->dma_buffer);
	internal_buffer->dma_buffer = NULL;
}

static int rga_mm_map_dma_buffer(struct rga_external_buffer *external_buffer,
				 struct rga_internal_buffer *internal_buffer)
{
	int ret, i;

	internal_buffer->dma_buffer_size = rga_drvdata->num_of_scheduler;
	internal_buffer->dma_buffer = kcalloc(internal_buffer->dma_buffer_size,
					      sizeof(struct rga_dma_buffer), GFP_KERNEL);
	if (internal_buffer->dma_buffer == NULL) {
		pr_err("%s alloc internal_buffer error!\n", __func__);
		return  -ENOMEM;
	}

	for (i = 0; i < internal_buffer->dma_buffer_size; i++) {
		/* If the physical address is greater than 4G, there is no need to map RGA2. */
		if ((rga_drvdata->rga_scheduler[i]->core == RGA2_SCHEDULER_CORE0) &&
		    (~internal_buffer->mm_flag & RGA_MEM_UNDER_4G) &&
		    i != 0)
			continue;

		ret = rga_dma_map_fd((int)external_buffer->memory,
				     &internal_buffer->dma_buffer[i],
				     DMA_BIDIRECTIONAL,
				     rga_drvdata->rga_scheduler[i]->dev);
		if (ret < 0) {
			pr_err("%s core[%d] map dma buffer error!\n",
				__func__, rga_drvdata->rga_scheduler[0]->core);
			goto FREE_RGA_DMA_BUF;
		}

		internal_buffer->dma_buffer[i].core = rga_drvdata->rga_scheduler[i]->core;

		/* At first, check whether the physical address. */
		if (i == 0) {
			if (rga_mm_check_range_sgt(internal_buffer->dma_buffer[0].sgt))
				internal_buffer->mm_flag |= RGA_MEM_UNDER_4G;

			/* If it's physically contiguous, there is no need to continue dma_map. */
			if (rga_mm_check_contiguous_sgt(internal_buffer->dma_buffer[0].sgt)) {
				internal_buffer->mm_flag |= RGA_MEM_PHYSICAL_CONTIGUOUS;
				internal_buffer->phys_addr =
					sg_phys(internal_buffer->dma_buffer[0].sgt->sgl);
				if (internal_buffer->phys_addr == 0) {
					pr_err("%s get physical address error!", __func__);
					goto FREE_RGA_DMA_BUF;
				}

				break;
			}
		}
	}

	return 0;

FREE_RGA_DMA_BUF:
	rga_mm_unmap_dma_buffer(internal_buffer);

	return ret;
}

static void rga_mm_unmap_virt_addr(struct rga_internal_buffer *internal_buffer)
{
	int i;

	WARN_ON(internal_buffer->dma_buffer == NULL || internal_buffer->virt_addr == NULL);

	for (i = 0; i < internal_buffer->dma_buffer_size; i++)
		if (rga_drvdata->rga_scheduler[i]->core == RGA3_SCHEDULER_CORE0 ||
		    rga_drvdata->rga_scheduler[i]->core == RGA3_SCHEDULER_CORE1)
			rga_iommu_unmap_virt_addr(&internal_buffer->dma_buffer[i]);
		else if (internal_buffer->dma_buffer[i].core != 0)
			dma_unmap_sg(rga_drvdata->rga_scheduler[i]->dev,
				     internal_buffer->dma_buffer[i].sgt->sgl,
				     internal_buffer->dma_buffer[i].sgt->orig_nents,
				     DMA_BIDIRECTIONAL);

	for (i = 0; i < internal_buffer->dma_buffer_size; i++)
		rga_free_sgt(&internal_buffer->dma_buffer[i]);
	kfree(internal_buffer->dma_buffer);
	internal_buffer->dma_buffer_size = 0;

	rga_free_virt_addr(&internal_buffer->virt_addr);

	mmput(internal_buffer->current_mm);
	mmdrop(internal_buffer->current_mm);
	internal_buffer->current_mm = NULL;
}

static int rga_mm_map_virt_addr(struct rga_external_buffer *external_buffer,
				struct rga_internal_buffer *internal_buffer)
{
	int i;
	int ret;

	internal_buffer->current_mm = current->mm;
	if (internal_buffer->current_mm == NULL) {
		pr_err("%s, cannot get current mm!\n", __func__);
		return -EFAULT;
	}
	mmgrab(internal_buffer->current_mm);
	mmget(internal_buffer->current_mm);

	ret = rga_alloc_virt_addr(&internal_buffer->virt_addr,
				  external_buffer->memory,
				  &internal_buffer->memory_parm,
				  0, internal_buffer->current_mm);
	if (ret < 0) {
		pr_err("Can not alloc rga_virt_addr from 0x%lx\n",
		       (unsigned long)external_buffer->memory);
		goto put_current_mm;
	}

	internal_buffer->dma_buffer = kcalloc(rga_drvdata->num_of_scheduler,
					      sizeof(struct rga_dma_buffer), GFP_KERNEL);
	if (internal_buffer->dma_buffer == NULL) {
		pr_err("%s alloc internal_buffer->dma_buffer error!\n", __func__);
		ret = -ENOMEM;
		goto free_virt_addr;
	}
	internal_buffer->dma_buffer_size = rga_drvdata->num_of_scheduler;

	for (i = 0; i < internal_buffer->dma_buffer_size; i++) {
		/* If the physical address is greater than 4G, there is no need to map RGA2. */
		if ((rga_drvdata->rga_scheduler[i]->core == RGA2_SCHEDULER_CORE0) &&
		    (~internal_buffer->mm_flag & RGA_MEM_UNDER_4G) &&
		    i != 0)
			continue;

		ret = rga_alloc_sgt(internal_buffer->virt_addr,
				    &internal_buffer->dma_buffer[i]);
		if (ret < 0) {
			pr_err("%s core[%d] alloc sgt error!\n", __func__,
			       rga_drvdata->rga_scheduler[0]->core);
			goto free_sgt_and_dma_buffer;
		}

		if (i == 0)
			if (rga_mm_check_range_sgt(internal_buffer->dma_buffer[0].sgt))
				internal_buffer->mm_flag |= RGA_MEM_UNDER_4G;
	}

	for (i = 0; i < internal_buffer->dma_buffer_size; i++) {
		if ((rga_drvdata->rga_scheduler[i]->core == RGA2_SCHEDULER_CORE0) &&
		    (~internal_buffer->mm_flag & RGA_MEM_UNDER_4G))
			continue;

		if (rga_drvdata->rga_scheduler[i]->core == RGA3_SCHEDULER_CORE0 ||
		    rga_drvdata->rga_scheduler[i]->core == RGA3_SCHEDULER_CORE1) {
			ret = rga_iommu_map_virt_addr(&internal_buffer->memory_parm,
						      &internal_buffer->dma_buffer[i],
						      rga_drvdata->rga_scheduler[i]->dev,
						      internal_buffer->current_mm);
			if (ret < 0) {
				pr_err("%s core[%d] iommu_map virtual address error!\n",
				       __func__, rga_drvdata->rga_scheduler[i]->core);
				goto unmap_virt_addr;
			}
		} else {
			ret = dma_map_sg(rga_drvdata->rga_scheduler[i]->dev,
					 internal_buffer->dma_buffer[i].sgt->sgl,
					 internal_buffer->dma_buffer[i].sgt->orig_nents,
					 DMA_BIDIRECTIONAL);
			if (ret == 0) {
				pr_err("%s core[%d] dma_map_sgt error! va = 0x%lx, nents = %d\n",
				       __func__, rga_drvdata->rga_scheduler[i]->core,
				       (unsigned long)internal_buffer->virt_addr->addr,
				       internal_buffer->dma_buffer[i].sgt->orig_nents);
				goto unmap_virt_addr;
			}
		}

		internal_buffer->dma_buffer[i].core = rga_drvdata->rga_scheduler[i]->core;
	}

	return 0;

unmap_virt_addr:
	for (i = 0; i < internal_buffer->dma_buffer_size; i++)
		if (rga_drvdata->rga_scheduler[i]->core == RGA3_SCHEDULER_CORE0 ||
		    rga_drvdata->rga_scheduler[i]->core == RGA3_SCHEDULER_CORE1)
			rga_iommu_unmap_virt_addr(&internal_buffer->dma_buffer[i]);
		else if (internal_buffer->dma_buffer[i].core != 0)
			dma_unmap_sg(rga_drvdata->rga_scheduler[i]->dev,
				     internal_buffer->dma_buffer[i].sgt->sgl,
				     internal_buffer->dma_buffer[i].sgt->orig_nents,
				     DMA_BIDIRECTIONAL);
free_sgt_and_dma_buffer:
	for (i = 0; i < internal_buffer->dma_buffer_size; i++)
		rga_free_sgt(&internal_buffer->dma_buffer[i]);
	kfree(internal_buffer->dma_buffer);
free_virt_addr:
	rga_free_virt_addr(&internal_buffer->virt_addr);
put_current_mm:
	mmput(internal_buffer->current_mm);
	mmdrop(internal_buffer->current_mm);
	internal_buffer->current_mm = NULL;

	return ret;
}

static int rga_mm_unmap_buffer(struct rga_internal_buffer *internal_buffer)
{
	switch (internal_buffer->type) {
	case RGA_DMA_BUFFER:
		rga_mm_unmap_dma_buffer(internal_buffer);
		break;
	case RGA_VIRTUAL_ADDRESS:
		rga_mm_unmap_virt_addr(internal_buffer);
		break;
	case RGA_PHYSICAL_ADDRESS:
		internal_buffer->phys_addr = 0;
		break;
	default:
		pr_err("Illegal external buffer!\n");
		return -EFAULT;
	}

	return 0;
}

static int rga_mm_map_buffer(struct rga_external_buffer *external_buffer,
	struct rga_internal_buffer *internal_buffer)
{
	int ret;

	memcpy(&internal_buffer->memory_parm, &external_buffer->memory_parm,
	       sizeof(internal_buffer->memory_parm));

	switch (external_buffer->type) {
	case RGA_DMA_BUFFER:
		internal_buffer->type = RGA_DMA_BUFFER;

		ret = rga_mm_map_dma_buffer(external_buffer, internal_buffer);
		if (ret < 0) {
			pr_err("%s map dma_buf error!\n", __func__);
			return ret;
		}

		internal_buffer->mm_flag |= RGA_MEM_NEED_USE_IOMMU;
		break;
	case RGA_VIRTUAL_ADDRESS:
		internal_buffer->type = RGA_VIRTUAL_ADDRESS;

		ret = rga_mm_map_virt_addr(external_buffer, internal_buffer);
		if (ret < 0) {
			pr_err("%s iommu_map virtual address error!\n", __func__);
			return ret;
		}

		internal_buffer->mm_flag |= RGA_MEM_NEED_USE_IOMMU;
		break;
	case RGA_PHYSICAL_ADDRESS:
		internal_buffer->type = RGA_PHYSICAL_ADDRESS;

		internal_buffer->phys_addr = external_buffer->memory;
		break;
	default:
		pr_err("Illegal external buffer!\n");
		return -EFAULT;
	}

	return 0;
}

static void rga_mm_kref_release_buffer(struct kref *ref)
{
	struct rga_internal_buffer *internal_buffer;

	internal_buffer = container_of(ref, struct rga_internal_buffer, refcount);
	rga_mm_unmap_buffer(internal_buffer);

	idr_remove(&rga_drvdata->mm->memory_idr, internal_buffer->handle);
	kfree(internal_buffer);
	rga_drvdata->mm->buffer_count--;
}

/*
 * Called at driver close to release the memory's handle references.
 */
static int rga_mm_handle_remove(int id, void *ptr, void *data)
{
	struct rga_internal_buffer *internal_buffer = ptr;

	rga_mm_kref_release_buffer(&internal_buffer->refcount);

	return 0;
}

static struct rga_internal_buffer *
rga_mm_lookup_external(struct rga_mm *mm_session,
		       struct rga_external_buffer *external_buffer)
{
	int id;
	struct dma_buf *dma_buf = NULL;
	struct rga_internal_buffer *temp_buffer = NULL;
	struct rga_internal_buffer *output_buffer = NULL;

	WARN_ON(!mutex_is_locked(&mm_session->lock));

	switch (external_buffer->type) {
	case RGA_DMA_BUFFER:
		dma_buf = dma_buf_get((int)external_buffer->memory);
		if (IS_ERR(dma_buf))
			return (struct rga_internal_buffer *)dma_buf;

		idr_for_each_entry(&mm_session->memory_idr, temp_buffer, id) {
			if (temp_buffer->dma_buffer == NULL)
				continue;

			if (temp_buffer->dma_buffer[0].dma_buf == dma_buf) {
				output_buffer = temp_buffer;
				break;
			}
		}

		dma_buf_put(dma_buf);
		break;
	case RGA_VIRTUAL_ADDRESS:
		idr_for_each_entry(&mm_session->memory_idr, temp_buffer, id) {
			if (temp_buffer->virt_addr == NULL)
				continue;

			if (temp_buffer->virt_addr->addr == external_buffer->memory) {
				output_buffer = temp_buffer;
				break;
			}
		}

		break;
	case RGA_PHYSICAL_ADDRESS:
		idr_for_each_entry(&mm_session->memory_idr, temp_buffer, id) {
			if (temp_buffer->phys_addr == external_buffer->memory) {
				output_buffer = temp_buffer;
				break;
			}
		}

		break;
	default:
		pr_err("Illegal external buffer!\n");
		return NULL;
	}

	return output_buffer;
}

struct rga_internal_buffer *rga_mm_lookup_handle(struct rga_mm *mm_session, uint32_t handle)
{
	struct rga_internal_buffer *output_buffer;

	WARN_ON(!mutex_is_locked(&mm_session->lock));

	output_buffer = idr_find(&mm_session->memory_idr, handle);

	return output_buffer;
}

int rga_mm_lookup_flag(struct rga_mm *mm_session, uint64_t handle)
{
	struct rga_internal_buffer *output_buffer;

	output_buffer = rga_mm_lookup_handle(mm_session, handle);
	if (output_buffer == NULL) {
		pr_err("This handle[%ld] is illegal.\n", (unsigned long)handle);
		return -EINVAL;
	}

	return output_buffer->mm_flag;
}

dma_addr_t rga_mm_lookup_iova(struct rga_internal_buffer *buffer, int core)
{
	int i;

	for (i = 0; i < buffer->dma_buffer_size; i++)
		if (buffer->dma_buffer[i].core == core)
			return buffer->dma_buffer[i].iova + buffer->dma_buffer[i].offset;

	return 0;
}

struct sg_table *rga_mm_lookup_sgt(struct rga_internal_buffer *buffer, int core)
{
	int i;

	for (i = 0; i < buffer->dma_buffer_size; i++)
		if (buffer->dma_buffer[i].core == core)
			return buffer->dma_buffer[i].sgt;

	return NULL;
}

void rga_mm_dump_info(struct rga_mm *mm_session)
{
	int id, i;
	struct rga_internal_buffer *dump_buffer;

	WARN_ON(!mutex_is_locked(&mm_session->lock));

	pr_info("rga mm info:\n");

	pr_info("buffer count = %d\n", mm_session->buffer_count);
	pr_info("===============================================================\n");

	idr_for_each_entry(&mm_session->memory_idr, dump_buffer, id) {
		pr_info("handle = %d	refcount = %d	mm_flag = 0x%x\n",
			dump_buffer->handle, kref_read(&dump_buffer->refcount),
			dump_buffer->mm_flag);

		switch (dump_buffer->type) {
		case RGA_DMA_BUFFER:
			pr_info("dma_buffer:\n");
			for (i = 0; i < dump_buffer->dma_buffer_size; i++) {
				pr_info("\t core %d:\n", dump_buffer->dma_buffer[i].core);
				pr_info("\t\t dma_buf = %p, iova = 0x%lx\n",
					dump_buffer->dma_buffer[i].dma_buf,
					(unsigned long)dump_buffer->dma_buffer[i].iova);
			}
			break;
		case RGA_VIRTUAL_ADDRESS:
			pr_info("virtual address:\n");
			pr_info("\t va = 0x%lx, pages = %p, size = %ld\n",
				(unsigned long)dump_buffer->virt_addr->addr,
				dump_buffer->virt_addr->pages,
				dump_buffer->virt_addr->size);

			for (i = 0; i < dump_buffer->dma_buffer_size; i++) {
				pr_info("\t core %d:\n", dump_buffer->dma_buffer[i].core);
				pr_info("\t\t iova = 0x%lx, sgt = %p, size = %ld\n",
					(unsigned long)dump_buffer->dma_buffer[i].iova,
					dump_buffer->dma_buffer[i].sgt,
					dump_buffer->dma_buffer[i].size);
			}
			break;
		case RGA_PHYSICAL_ADDRESS:
			pr_info("physical address:\n");
			pr_info("\t pa = 0x%lx\n", (unsigned long)dump_buffer->phys_addr);
			break;
		default:
			pr_err("Illegal external buffer!\n");
			break;
		}

		pr_info("---------------------------------------------------------------\n");
	}
}

static bool rga_mm_is_need_mmu(int core, struct rga_internal_buffer *buffer)
{
	if (buffer == NULL)
		return false;

	if (buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS &&
	    core == RGA2_SCHEDULER_CORE0)
		return false;
	else if (buffer->mm_flag & RGA_MEM_NEED_USE_IOMMU)
		return true;

	return false;
}

static int rga_mm_set_mmu_flag(struct rga_job *job)
{
	struct rga_mmu_t *mmu_info;
	int src_mmu_en;
	int src1_mmu_en;
	int dst_mmu_en;
	int els_mmu_en;

	src_mmu_en = rga_mm_is_need_mmu(job->core, job->src_buffer);
	src1_mmu_en = rga_mm_is_need_mmu(job->core, job->src1_buffer);
	dst_mmu_en = rga_mm_is_need_mmu(job->core, job->dst_buffer);
	els_mmu_en = rga_mm_is_need_mmu(job->core, job->els_buffer);

	mmu_info = &job->rga_command_base.mmu_info;
	if (src_mmu_en)
		mmu_info->mmu_flag |= (0x1 << 8);
	if (src1_mmu_en)
		mmu_info->mmu_flag |= (0x1 << 9);
	if (dst_mmu_en)
		mmu_info->mmu_flag |= (0x1 << 10);
	if (els_mmu_en)
		mmu_info->mmu_flag |= (0x1 << 11);

	if (mmu_info->mmu_flag & (0xf << 8)) {
		mmu_info->mmu_flag |= 1;
		mmu_info->mmu_flag |= 1 << 31;
		mmu_info->mmu_en  = 1;
	}

	return 0;
}

static int rga_mm_sync_dma_sg_for_device(struct rga_internal_buffer *buffer,
					 int core,
					 enum dma_data_direction dir)
{
	struct sg_table *sgt;
	struct rga_scheduler_t *scheduler;

	scheduler = rga_job_get_scheduler(core);
	if (scheduler == NULL) {
		pr_err("%s(%d), failed to get scheduler, core = 0x%x\n",
		       __func__, __LINE__, core);
		return -EFAULT;
	}

	sgt = rga_mm_lookup_sgt(buffer, core);
	if (sgt == NULL) {
		pr_err("%s(%d), failed to get sgt, core = 0x%x\n",
		       __func__, __LINE__, core);
		return -EINVAL;
	}

	dma_sync_sg_for_device(scheduler->dev, sgt->sgl, sgt->orig_nents, dir);

	return 0;
}

static int rga_mm_sync_dma_sg_for_cpu(struct rga_internal_buffer *buffer,
				      int core,
				      enum dma_data_direction dir)
{
	struct sg_table *sgt;
	struct rga_scheduler_t *scheduler;

	scheduler = rga_job_get_scheduler(core);
	if (scheduler == NULL) {
		pr_err("%s(%d), failed to get scheduler, core = 0x%x\n",
		       __func__, __LINE__, core);
		return -EFAULT;
	}

	sgt = rga_mm_lookup_sgt(buffer, core);
	if (sgt == NULL) {
		pr_err("%s(%d), failed to get sgt, core = 0x%x\n",
		       __func__, __LINE__, core);
		return -EINVAL;
	}

	dma_sync_sg_for_cpu(scheduler->dev, sgt->sgl, sgt->orig_nents, dir);

	return 0;
}

static int rga_mm_get_channel_handle_info(struct rga_mm *mm,
					  struct rga_job *job,
					  struct rga_img_info_t *img,
					  struct rga_internal_buffer **buf,
					  enum dma_data_direction dir)
{
	int ret = 0;
	struct rga_internal_buffer *internal_buffer = NULL;

	if (!(img->yrgb_addr > 0)) {
		pr_err("No buffer handle can be used!\n");
		return -EFAULT;
	}

	mutex_lock(&mm->lock);
	*buf = rga_mm_lookup_handle(mm, img->yrgb_addr);
	if (*buf == NULL) {
		pr_err("This handle[%ld] is illegal.\n", (unsigned long)img->yrgb_addr);

		ret = -EFAULT;
		goto unlock_mm_and_return;
	}

	internal_buffer = *buf;
	kref_get(&internal_buffer->refcount);

	switch (internal_buffer->type) {
	case RGA_DMA_BUFFER:
		if (job->core == RGA3_SCHEDULER_CORE0 ||
		    job->core == RGA3_SCHEDULER_CORE1) {
			img->yrgb_addr = rga_mm_lookup_iova(internal_buffer, job->core);
			if (img->yrgb_addr == 0) {
				pr_err("lookup dma_buf iova error!\n");

				ret = -EINVAL;
				goto unlock_mm_and_return;
			}
		} else if (job->core == RGA2_SCHEDULER_CORE0 &&
			   internal_buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS) {
			img->yrgb_addr = internal_buffer->phys_addr;
		} else {
			img->yrgb_addr = 0;
		}

		break;
	case RGA_VIRTUAL_ADDRESS:
		if (job->core == RGA3_SCHEDULER_CORE0 ||
		    job->core == RGA3_SCHEDULER_CORE1) {
			img->yrgb_addr = rga_mm_lookup_iova(internal_buffer, job->core);
			if (img->yrgb_addr == 0) {
				pr_err("lookup virt_addr iova error!\n");

				ret = -EINVAL;
				goto unlock_mm_and_return;
			}
		} else {
			img->yrgb_addr = internal_buffer->virt_addr->addr;
		}

		/*
		 * Some userspace virtual addresses do not have an
		 * interface for flushing the cache, so it is mandatory
		 * to flush the cache when the virtual address is used.
		 */
		ret = rga_mm_sync_dma_sg_for_device(internal_buffer, job->core, dir);
		if (ret < 0) {
			pr_err("sync sgt for device error!\n");
			goto unlock_mm_and_return;
		}

		break;
	case RGA_PHYSICAL_ADDRESS:
		img->yrgb_addr = internal_buffer->phys_addr;
		break;
	default:
		pr_err("Illegal external buffer!\n");

		ret = -EFAULT;
		goto unlock_mm_and_return;
	}
	mutex_unlock(&mm->lock);

	rga_convert_addr(img, false);

	return 0;
unlock_mm_and_return:
	mutex_unlock(&mm->lock);
	return ret;
}

static void rga_mm_put_channel_handle_info(struct rga_mm *mm,
					   struct rga_internal_buffer *internal_buffer,
					   int core,
					   enum dma_data_direction dir)
{
	int ret;

	if (internal_buffer->type == RGA_VIRTUAL_ADDRESS && dir != DMA_NONE) {
		ret = rga_mm_sync_dma_sg_for_cpu(internal_buffer, core, dir);
		if (ret < 0) {
			pr_err("sync sgt for cpu error!\n");
			goto put_internal_buffer;
		}
	}

put_internal_buffer:
	mutex_lock(&mm->lock);

	kref_put(&internal_buffer->refcount, rga_mm_kref_release_buffer);

	mutex_unlock(&mm->lock);
}

int rga_mm_get_handle_info(struct rga_job *job)
{
	int ret = 0;
	struct rga_req *req = NULL;
	struct rga_mm *mm = NULL;
	enum dma_data_direction dir;

	req = &job->rga_command_base;
	mm = rga_drvdata->mm;

	if (likely(req->src.yrgb_addr > 0)) {
		ret = rga_mm_get_channel_handle_info(mm, job, &req->src,
						     &job->src_buffer,
						     DMA_TO_DEVICE);
		if (ret < 0) {
			pr_err("Can't get src buffer info!\n");
			return ret;
		}
	}

	if (likely(req->dst.yrgb_addr > 0)) {
		ret = rga_mm_get_channel_handle_info(mm, job, &req->dst,
						     &job->dst_buffer,
						     DMA_TO_DEVICE);
		if (ret < 0) {
			pr_err("Can't get dst buffer info!\n");
			return ret;
		}
	}

	if (likely(req->pat.yrgb_addr > 0)) {

		if (job->rga_command_base.render_mode != UPDATE_PALETTE_TABLE_MODE) {
			if (job->rga_command_base.bsfilter_flag)
				dir = DMA_BIDIRECTIONAL;
			else
				dir = DMA_TO_DEVICE;

			ret = rga_mm_get_channel_handle_info(mm, job, &req->pat,
							     &job->src1_buffer,
							     dir);
		} else {
			ret = rga_mm_get_channel_handle_info(mm, job, &req->pat,
							     &job->els_buffer,
							     DMA_BIDIRECTIONAL);
		}
		if (ret < 0) {
			pr_err("Can't get pat buffer info!\n");
			return ret;
		}
	}

	rga_mm_set_mmu_flag(job);

	return 0;
}

void rga_mm_put_handle_info(struct rga_job *job)
{
	struct rga_mm *mm = NULL;

	mm = rga_drvdata->mm;

	if (job->src_buffer)
		rga_mm_put_channel_handle_info(mm, job->src_buffer, job->core, DMA_NONE);
	if (job->dst_buffer)
		rga_mm_put_channel_handle_info(mm, job->dst_buffer, job->core, DMA_FROM_DEVICE);
	if (job->src1_buffer)
		rga_mm_put_channel_handle_info(mm, job->src1_buffer, job->core, DMA_NONE);
	if (job->els_buffer)
		rga_mm_put_channel_handle_info(mm, job->els_buffer, job->core, DMA_NONE);
}

uint32_t rga_mm_import_buffer(struct rga_external_buffer *external_buffer)
{
	int ret = 0;
	struct rga_mm *mm;
	struct rga_internal_buffer *internal_buffer;

	mm = rga_drvdata->mm;
	if (mm == NULL) {
		pr_err("rga mm is null!\n");
		return -EFAULT;
	}

	mutex_lock(&mm->lock);

	/* first, Check whether to rga_mm */
	internal_buffer = rga_mm_lookup_external(mm, external_buffer);
	if (!IS_ERR_OR_NULL(internal_buffer)) {
		kref_get(&internal_buffer->refcount);

		mutex_unlock(&mm->lock);
		return internal_buffer->handle;
	}

	/* finally, map and cached external_buffer in rga_mm */
	internal_buffer = kzalloc(sizeof(struct rga_internal_buffer), GFP_KERNEL);
	if (internal_buffer == NULL) {
		pr_err("%s alloc internal_buffer error!\n", __func__);

		mutex_unlock(&mm->lock);
		return -ENOMEM;
	}

	ret = rga_mm_map_buffer(external_buffer, internal_buffer);
	if (ret < 0)
		goto FREE_INTERNAL_BUFFER;

	kref_init(&internal_buffer->refcount);

	/*
	 * Get the user-visible handle using idr. Preload and perform
	 * allocation under our spinlock.
	 */
	idr_preload(GFP_KERNEL);
	internal_buffer->handle = idr_alloc(&mm->memory_idr, internal_buffer, 1, 0, GFP_KERNEL);
	idr_preload_end();

	mm->buffer_count++;

	mutex_unlock(&mm->lock);
	return internal_buffer->handle;

FREE_INTERNAL_BUFFER:
	mutex_unlock(&mm->lock);
	kfree(internal_buffer);

	return ret;
}

int rga_mm_release_buffer(uint32_t handle)
{
	struct rga_mm *mm;
	struct rga_internal_buffer *internal_buffer;

	mm = rga_drvdata->mm;
	if (mm == NULL) {
		pr_err("rga mm is null!\n");
		return -EFAULT;
	}

	mutex_lock(&mm->lock);

	/* Find the buffer that has been imported */
	internal_buffer = rga_mm_lookup_handle(mm, handle);
	if (IS_ERR_OR_NULL(internal_buffer)) {
		pr_err("This is not a buffer that has been imported, handle = %d\n", (int)handle);

		mutex_unlock(&mm->lock);
		return -ENOENT;
	}

	kref_put(&internal_buffer->refcount, rga_mm_kref_release_buffer);

	mutex_unlock(&mm->lock);
	return 0;
}

int rga_mm_init(struct rga_mm **mm_session)
{
	struct rga_mm *mm = NULL;

	*mm_session = kzalloc(sizeof(struct rga_mm), GFP_KERNEL);
	if (*mm_session == NULL) {
		pr_err("can not kzalloc for rga buffer mm_session\n");
		return -ENOMEM;
	}

	mm = *mm_session;

	mutex_init(&mm->lock);
	idr_init_base(&mm->memory_idr, 1);

	return 0;
}

int rga_mm_remove(struct rga_mm **mm_session)
{
	struct rga_mm *mm = *mm_session;

	mutex_lock(&mm->lock);

	idr_for_each(&mm->memory_idr, &rga_mm_handle_remove, mm);
	idr_destroy(&mm->memory_idr);

	mutex_unlock(&mm->lock);

	kfree(*mm_session);
	*mm_session = NULL;

	return 0;
}
