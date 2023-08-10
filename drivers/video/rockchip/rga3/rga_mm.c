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
#include "rga_iommu.h"
#include "rga_hw_config.h"
#include "rga_debugger.h"

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
			pr_err("page[%d] failed to get vma\n", i);
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}

		pgd = pgd_offset(current_mm, (Memory + i) << PAGE_SHIFT);
		if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd))) {
			pr_err("page[%d] failed to get pgd\n", i);
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
			pr_err("page[%d] failed to get p4d\n", i);
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}

		pud = pud_offset(p4d, (Memory + i) << PAGE_SHIFT);
#else
		pud = pud_offset(pgd, (Memory + i) << PAGE_SHIFT);
#endif

		if (pud_none(*pud) || unlikely(pud_bad(*pud))) {
			pr_err("page[%d] failed to get pud\n", i);
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}
		pmd = pmd_offset(pud, (Memory + i) << PAGE_SHIFT);
		if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd))) {
			pr_err("page[%d] failed to get pmd\n", i);
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}
		pte = pte_offset_map_lock(current_mm, pmd,
					  (Memory + i) << PAGE_SHIFT, &ptl);
		if (pte_none(*pte)) {
			pr_err("page[%d] failed to get pte\n", i);
			pte_unmap_unlock(pte, ptl);
			ret = RGA_OUT_OF_RESOURCES;
			break;
		}

		pfn = pte_pfn(*pte);
		pages[i] = pfn_to_page(pfn);
		pte_unmap_unlock(pte, ptl);
	}

	if (ret == RGA_OUT_OF_RESOURCES && i > 0)
		pr_err("Only get buffer %d byte from vma, but current image required %d byte",
		       (int)(i * PAGE_SIZE), (int)(pageCount * PAGE_SIZE));

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
				pageCount, writeFlag ? FOLL_WRITE : 0, 0, pages, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	result = get_user_pages_remote(current, current_mm,
				       Memory << PAGE_SHIFT,
				       pageCount, writeFlag ? FOLL_WRITE : 0, pages, NULL, NULL);
#else
	result = get_user_pages_remote(current_mm, Memory << PAGE_SHIFT,
				       pageCount, writeFlag ? FOLL_WRITE : 0, pages, NULL, NULL);
#endif

	if (result > 0 && result >= pageCount) {
		ret = result;
	} else {
		if (result > 0)
			for (i = 0; i < result; i++)
				put_page(pages[i]);

		ret = rga_get_user_pages_from_vma(pages, Memory, pageCount, current_mm);
		if (ret < 0 && result > 0) {
			pr_err("Only get buffer %d byte from user pages, but current image required %d byte\n",
			       (int)(result * PAGE_SIZE), (int)(pageCount * PAGE_SIZE));
		}
	}

	rga_current_mm_read_unlock(current_mm);

	return ret;
}

static void rga_free_sgt(struct sg_table **sgt_ptr)
{
	if (sgt_ptr == NULL || *sgt_ptr == NULL)
		return;

	sg_free_table(*sgt_ptr);
	kfree(*sgt_ptr);
	*sgt_ptr = NULL;
}

static struct sg_table *rga_alloc_sgt(struct rga_virt_addr *virt_addr)
{
	int ret;
	struct sg_table *sgt = NULL;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (sgt == NULL) {
		pr_err("%s alloc sgt error!\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	/* get sg form pages. */
	/* iova requires minimum page alignment, so sgt cannot have offset */
	ret = sg_alloc_table_from_pages(sgt,
					virt_addr->pages,
					virt_addr->page_count,
					0,
					virt_addr->size,
					GFP_KERNEL);
	if (ret) {
		pr_err("sg_alloc_table_from_pages failed");
		goto out_free_sgt;
	}

	return sgt;

out_free_sgt:
	kfree(sgt);

	return ERR_PTR(ret);
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
	int img_size;
	size_t offset;
	unsigned long size;
	struct page **pages = NULL;
	struct rga_virt_addr *virt_addr = NULL;

	if (memory_parm->size)
		img_size = memory_parm->size;
	else
		img_size = rga_image_size_cal(memory_parm->width,
					      memory_parm->height,
					      memory_parm->format,
					      NULL, NULL, NULL);

	offset = viraddr & (~PAGE_MASK);
	count = RGA_GET_PAGE_COUNT(img_size + offset);
	size = count * PAGE_SIZE;
	if (!size) {
		pr_err("failed to calculating buffer size! size = %ld, count = %d, offset = %ld\n",
		       size, count, (unsigned long)offset);
		rga_dump_memory_parm(memory_parm);
		return -EFAULT;
	}

	/* alloc pages and page_table */
	order = get_order(count * sizeof(struct page *));
	if (order >= MAX_ORDER) {
		pr_err("Can not alloc pages with order[%d] for viraddr pages, max_order = %d\n",
		       order, MAX_ORDER);
		return -ENOMEM;
	}

	pages = (struct page **)__get_free_pages(GFP_KERNEL, order);
	if (pages == NULL) {
		pr_err("%s can not alloc pages for viraddr pages\n", __func__);
		return -ENOMEM;
	}

	/* get pages from virtual address. */
	ret = rga_get_user_pages(pages, viraddr >> PAGE_SHIFT, count, writeFlag, mm);
	if (ret < 0) {
		pr_err("failed to get pages from virtual adrees: 0x%lx\n",
		       (unsigned long)viraddr);
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
	virt_addr->offset = offset;
	virt_addr->result = result;

	return 0;

out_put_and_free_pages:
	for (i = 0; i < result; i++)
		put_page(pages[i]);
out_free_pages:
	free_pages((unsigned long)pages, order);

	return ret;
}

static inline bool rga_mm_check_memory_limit(struct rga_scheduler_t *scheduler, int mm_flag)
{
	if (!scheduler)
		return false;

	if (scheduler->data->mmu == RGA_MMU &&
	    !(mm_flag & RGA_MEM_UNDER_4G)) {
		pr_err("%s unsupported memory larger than 4G!\n",
		       rga_get_mmu_type_str(scheduler->data->mmu));
		return false;
	}

	return true;
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

static inline int rga_mm_check_range_phys_addr(phys_addr_t paddr, size_t size)
{
	return ((paddr + size) <= 0xffffffff);
}

static inline bool rga_mm_check_contiguous_sgt(struct sg_table *sgt)
{
	if (sgt->orig_nents == 1)
		return true;

	return false;
}

static void rga_mm_unmap_dma_buffer(struct rga_internal_buffer *internal_buffer)
{
	if (rga_mm_is_invalid_dma_buffer(internal_buffer->dma_buffer))
		return;

	rga_dma_unmap_buf(internal_buffer->dma_buffer);

	if (internal_buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS &&
	    internal_buffer->phys_addr > 0)
		internal_buffer->phys_addr = 0;

	kfree(internal_buffer->dma_buffer);
	internal_buffer->dma_buffer = NULL;
}

static int rga_mm_map_dma_buffer(struct rga_external_buffer *external_buffer,
				 struct rga_internal_buffer *internal_buffer,
				 struct rga_job *job)
{
	int ret;
	int ex_buffer_size;
	uint32_t mm_flag = 0;
	phys_addr_t phys_addr = 0;
	struct rga_dma_buffer *buffer;
	struct device *map_dev;
	struct rga_scheduler_t *scheduler;

	scheduler = job ? job->scheduler :
		    rga_drvdata->scheduler[rga_drvdata->map_scheduler_index];
	if (scheduler == NULL) {
		pr_err("Invalid scheduler device!\n");
		return -EINVAL;
	}

	if (external_buffer->memory_parm.size)
		ex_buffer_size = external_buffer->memory_parm.size;
	else
		ex_buffer_size = rga_image_size_cal(external_buffer->memory_parm.width,
						    external_buffer->memory_parm.height,
						    external_buffer->memory_parm.format,
						    NULL, NULL, NULL);
	if (ex_buffer_size <= 0) {
		pr_err("failed to calculating buffer size!\n");
		rga_dump_memory_parm(&external_buffer->memory_parm);
		return ex_buffer_size == 0 ? -EINVAL : ex_buffer_size;
	}

	/*
	 * dma-buf api needs to use default_domain of main dev,
	 * and not IOMMU for devices without iommu_info ptr.
	 */
	map_dev = scheduler->iommu_info ? scheduler->iommu_info->default_dev : scheduler->dev;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (buffer == NULL) {
		pr_err("%s alloc internal_buffer error!\n", __func__);
		return  -ENOMEM;
	}

	switch (external_buffer->type) {
	case RGA_DMA_BUFFER:
		ret = rga_dma_map_fd((int)external_buffer->memory,
				     buffer, DMA_BIDIRECTIONAL,
				     map_dev);
		break;
	case RGA_DMA_BUFFER_PTR:
		ret = rga_dma_map_buf((struct dma_buf *)u64_to_user_ptr(external_buffer->memory),
				      buffer, DMA_BIDIRECTIONAL,
				      map_dev);
		break;
	default:
		ret = -EFAULT;
		break;
	}
	if (ret < 0) {
		pr_err("%s core[%d] map dma buffer error!\n",
		       __func__, scheduler->core);
		goto free_buffer;
	}

	if (buffer->size < ex_buffer_size) {
		pr_err("Only get buffer %ld byte from %s = 0x%lx, but current image required %d byte\n",
		       buffer->size, rga_get_memory_type_str(external_buffer->type),
		       (unsigned long)external_buffer->memory, ex_buffer_size);
		rga_dump_memory_parm(&external_buffer->memory_parm);
		ret = -EINVAL;
		goto unmap_buffer;
	}

	buffer->scheduler = scheduler;

	if (rga_mm_check_range_sgt(buffer->sgt))
		mm_flag |= RGA_MEM_UNDER_4G;

	/*
	 * If it's physically contiguous, then the RGA_MMU can
	 * directly use the physical address.
	 */
	if (rga_mm_check_contiguous_sgt(buffer->sgt)) {
		phys_addr = sg_phys(buffer->sgt->sgl);
		if (phys_addr == 0) {
			pr_err("%s get physical address error!", __func__);
			goto unmap_buffer;
		}

		mm_flag |= RGA_MEM_PHYSICAL_CONTIGUOUS;
	}

	if (!rga_mm_check_memory_limit(scheduler, mm_flag)) {
		pr_err("scheduler core[%d] unsupported mm_flag[0x%x]!\n",
		       scheduler->core, mm_flag);
		ret = -EINVAL;
		goto unmap_buffer;
	}

	internal_buffer->dma_buffer = buffer;
	internal_buffer->mm_flag = mm_flag;
	internal_buffer->phys_addr = phys_addr ? phys_addr : 0;

	return 0;

unmap_buffer:
	rga_dma_unmap_buf(buffer);

free_buffer:
	kfree(buffer);

	return ret;
}

static void rga_mm_unmap_virt_addr(struct rga_internal_buffer *internal_buffer)
{
	WARN_ON(internal_buffer->dma_buffer == NULL || internal_buffer->virt_addr == NULL);

	if (rga_mm_is_invalid_dma_buffer(internal_buffer->dma_buffer))
		return;

	switch (internal_buffer->dma_buffer->scheduler->data->mmu) {
	case RGA_IOMMU:
		rga_iommu_unmap(internal_buffer->dma_buffer);
		break;
	case RGA_MMU:
		dma_unmap_sg(internal_buffer->dma_buffer->scheduler->dev,
			     internal_buffer->dma_buffer->sgt->sgl,
			     internal_buffer->dma_buffer->sgt->orig_nents,
			     DMA_BIDIRECTIONAL);
		break;
	default:
		break;
	}

	if (internal_buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS &&
	    internal_buffer->phys_addr > 0)
		internal_buffer->phys_addr = 0;

	rga_free_sgt(&internal_buffer->dma_buffer->sgt);

	kfree(internal_buffer->dma_buffer);
	internal_buffer->dma_buffer = NULL;

	rga_free_virt_addr(&internal_buffer->virt_addr);

	mmput(internal_buffer->current_mm);
	mmdrop(internal_buffer->current_mm);
	internal_buffer->current_mm = NULL;
}

static int rga_mm_map_virt_addr(struct rga_external_buffer *external_buffer,
				struct rga_internal_buffer *internal_buffer,
				struct rga_job *job, int write_flag)
{
	int ret;
	uint32_t mm_flag = 0;
	phys_addr_t phys_addr = 0;
	struct sg_table *sgt;
	struct rga_virt_addr *virt_addr;
	struct rga_dma_buffer *buffer;
	struct rga_scheduler_t *scheduler;

	scheduler = job ? job->scheduler :
		    rga_drvdata->scheduler[rga_drvdata->map_scheduler_index];
	if (scheduler == NULL) {
		pr_err("Invalid scheduler device!\n");
		return -EINVAL;
	}

	internal_buffer->current_mm = job ? job->mm : current->mm;
	if (internal_buffer->current_mm == NULL) {
		pr_err("%s, cannot get current mm!\n", __func__);
		return -EFAULT;
	}
	mmgrab(internal_buffer->current_mm);
	mmget(internal_buffer->current_mm);

	ret = rga_alloc_virt_addr(&virt_addr,
				  external_buffer->memory,
				  &internal_buffer->memory_parm,
				  write_flag, internal_buffer->current_mm);
	if (ret < 0) {
		pr_err("Can not alloc rga_virt_addr from 0x%lx\n",
		       (unsigned long)external_buffer->memory);
		goto put_current_mm;
	}

	sgt = rga_alloc_sgt(virt_addr);
	if (IS_ERR(sgt)) {
		pr_err("alloc sgt error!\n");
		ret = PTR_ERR(sgt);
		goto free_virt_addr;
	}

	if (rga_mm_check_range_sgt(sgt))
		mm_flag |= RGA_MEM_UNDER_4G;

	if (rga_mm_check_contiguous_sgt(sgt)) {
		phys_addr = sg_phys(sgt->sgl);
		if (phys_addr == 0) {
			pr_err("%s get physical address error!", __func__);
			goto free_sgt;
		}

		mm_flag |= RGA_MEM_PHYSICAL_CONTIGUOUS;
	}

	/*
	 * Some userspace virtual addresses do not have an
	 * interface for flushing the cache, so it is mandatory
	 * to flush the cache when the virtual address is used.
	 */
	mm_flag |= RGA_MEM_FORCE_FLUSH_CACHE;

	if (!rga_mm_check_memory_limit(scheduler, mm_flag)) {
		pr_err("scheduler core[%d] unsupported mm_flag[0x%x]!\n",
		       scheduler->core, mm_flag);
		ret = -EINVAL;
		goto free_sgt;
	}

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (buffer == NULL) {
		pr_err("%s alloc internal dma_buffer error!\n", __func__);
		ret =  -ENOMEM;
		goto free_sgt;
	}

	switch (scheduler->data->mmu) {
	case RGA_IOMMU:
		ret = rga_iommu_map_sgt(sgt, virt_addr->size, buffer, scheduler->dev);
		if (ret < 0) {
			pr_err("%s core[%d] iommu_map virtual address error!\n",
			       __func__, scheduler->core);
			goto free_dma_buffer;
		}
		break;
	case RGA_MMU:
		ret = dma_map_sg(scheduler->dev, sgt->sgl, sgt->orig_nents, DMA_BIDIRECTIONAL);
		if (ret == 0) {
			pr_err("%s core[%d] dma_map_sgt error! va = 0x%lx, nents = %d\n",
				__func__, scheduler->core,
				(unsigned long)virt_addr->addr, sgt->orig_nents);
			ret = -EINVAL;
			goto free_dma_buffer;
		}
		break;
	default:
		if (mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS)
			break;

		pr_err("Current %s[%d] cannot support virtual address!\n",
		       rga_get_mmu_type_str(scheduler->data->mmu), scheduler->data->mmu);
		goto free_dma_buffer;
	}

	buffer->sgt = sgt;
	buffer->offset = virt_addr->offset;
	buffer->size = virt_addr->size;
	buffer->scheduler = scheduler;

	internal_buffer->virt_addr = virt_addr;
	internal_buffer->dma_buffer = buffer;
	internal_buffer->mm_flag = mm_flag;
	internal_buffer->phys_addr = phys_addr ? phys_addr + virt_addr->offset : 0;

	return 0;

free_dma_buffer:
	kfree(buffer);
free_sgt:
	rga_free_sgt(&sgt);
free_virt_addr:
	rga_free_virt_addr(&virt_addr);
put_current_mm:
	mmput(internal_buffer->current_mm);
	mmdrop(internal_buffer->current_mm);
	internal_buffer->current_mm = NULL;

	return ret;
}

static void rga_mm_unmap_phys_addr(struct rga_internal_buffer *internal_buffer)
{
	WARN_ON(internal_buffer->dma_buffer == NULL);

	if (rga_mm_is_invalid_dma_buffer(internal_buffer->dma_buffer))
		return;

	if (internal_buffer->dma_buffer->scheduler->data->mmu == RGA_IOMMU)
		rga_iommu_unmap(internal_buffer->dma_buffer);

	kfree(internal_buffer->dma_buffer);
	internal_buffer->dma_buffer = NULL;
	internal_buffer->phys_addr = 0;
	internal_buffer->size = 0;
}

static int rga_mm_map_phys_addr(struct rga_external_buffer *external_buffer,
				struct rga_internal_buffer *internal_buffer,
				struct rga_job *job)
{
	int ret;
	phys_addr_t phys_addr;
	int buffer_size;
	uint32_t mm_flag = 0;
	struct rga_dma_buffer *buffer;
	struct rga_scheduler_t *scheduler;

	scheduler = job ? job->scheduler :
		    rga_drvdata->scheduler[rga_drvdata->map_scheduler_index];
	if (scheduler == NULL) {
		pr_err("Invalid scheduler device!\n");
		return -EINVAL;
	}

	if (internal_buffer->memory_parm.size)
		buffer_size = internal_buffer->memory_parm.size;
	else
		buffer_size = rga_image_size_cal(internal_buffer->memory_parm.width,
						 internal_buffer->memory_parm.height,
						 internal_buffer->memory_parm.format,
						 NULL, NULL, NULL);
	if (buffer_size <= 0) {
		pr_err("Failed to get phys addr size!\n");
		rga_dump_memory_parm(&internal_buffer->memory_parm);
		return buffer_size == 0 ? -EINVAL : buffer_size;
	}

	phys_addr = external_buffer->memory;
	mm_flag |= RGA_MEM_PHYSICAL_CONTIGUOUS;
	if (rga_mm_check_range_phys_addr(phys_addr, buffer_size))
		mm_flag |= RGA_MEM_UNDER_4G;

	if (!rga_mm_check_memory_limit(scheduler, mm_flag)) {
		pr_err("scheduler core[%d] unsupported mm_flag[0x%x]!\n",
		       scheduler->core, mm_flag);
		return -EINVAL;
	}

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (buffer == NULL) {
		pr_err("%s alloc internal dma buffer error!\n", __func__);
		return  -ENOMEM;
	}

	if (scheduler->data->mmu == RGA_IOMMU) {
		ret = rga_iommu_map(phys_addr, buffer_size, buffer, scheduler->dev);
		if (ret < 0) {
			pr_err("%s core[%d] map phys_addr error!\n", __func__, scheduler->core);
			goto free_dma_buffer;
		}
	}

	buffer->scheduler = scheduler;

	internal_buffer->phys_addr = phys_addr;
	internal_buffer->size = buffer_size;
	internal_buffer->mm_flag = mm_flag;
	internal_buffer->dma_buffer = buffer;

	return 0;

free_dma_buffer:
	kfree(buffer);

	return ret;
}

static int rga_mm_unmap_buffer(struct rga_internal_buffer *internal_buffer)
{
	switch (internal_buffer->type) {
	case RGA_DMA_BUFFER:
	case RGA_DMA_BUFFER_PTR:
		rga_mm_unmap_dma_buffer(internal_buffer);
		break;
	case RGA_VIRTUAL_ADDRESS:
		rga_mm_unmap_virt_addr(internal_buffer);
		break;
	case RGA_PHYSICAL_ADDRESS:
		rga_mm_unmap_phys_addr(internal_buffer);
		break;
	default:
		pr_err("Illegal external buffer!\n");
		return -EFAULT;
	}

	return 0;
}

static int rga_mm_map_buffer(struct rga_external_buffer *external_buffer,
			     struct rga_internal_buffer *internal_buffer,
			     struct rga_job *job, int write_flag)
{
	int ret;

	memcpy(&internal_buffer->memory_parm, &external_buffer->memory_parm,
	       sizeof(internal_buffer->memory_parm));

	switch (external_buffer->type) {
	case RGA_DMA_BUFFER:
	case RGA_DMA_BUFFER_PTR:
		internal_buffer->type = external_buffer->type;

		ret = rga_mm_map_dma_buffer(external_buffer, internal_buffer, job);
		if (ret < 0) {
			pr_err("%s map dma_buf error!\n", __func__);
			return ret;
		}

		internal_buffer->size = internal_buffer->dma_buffer->size -
					internal_buffer->dma_buffer->offset;
		internal_buffer->mm_flag |= RGA_MEM_NEED_USE_IOMMU;
		break;
	case RGA_VIRTUAL_ADDRESS:
		internal_buffer->type = RGA_VIRTUAL_ADDRESS;

		ret = rga_mm_map_virt_addr(external_buffer, internal_buffer, job, write_flag);
		if (ret < 0) {
			pr_err("%s map virtual address error!\n", __func__);
			return ret;
		}

		internal_buffer->size = internal_buffer->virt_addr->size -
					internal_buffer->virt_addr->offset;
		internal_buffer->mm_flag |= RGA_MEM_NEED_USE_IOMMU;
		break;
	case RGA_PHYSICAL_ADDRESS:
		internal_buffer->type = RGA_PHYSICAL_ADDRESS;

		ret = rga_mm_map_phys_addr(external_buffer, internal_buffer, job);
		if (ret < 0) {
			pr_err("%s map physical address error!\n", __func__);
			return ret;
		}

		internal_buffer->mm_flag |= RGA_MEM_NEED_USE_IOMMU;
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
	case RGA_DMA_BUFFER_PTR:
		idr_for_each_entry(&mm_session->memory_idr, temp_buffer, id) {
			if (temp_buffer->dma_buffer == NULL)
				continue;

			if ((unsigned long)temp_buffer->dma_buffer[0].dma_buf ==
			    external_buffer->memory) {
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

dma_addr_t rga_mm_lookup_iova(struct rga_internal_buffer *buffer)
{
	if (rga_mm_is_invalid_dma_buffer(buffer->dma_buffer))
		return 0;

	return buffer->dma_buffer->iova + buffer->dma_buffer->offset;
}

struct sg_table *rga_mm_lookup_sgt(struct rga_internal_buffer *buffer)
{
	if (rga_mm_is_invalid_dma_buffer(buffer->dma_buffer))
		return NULL;

	return buffer->dma_buffer->sgt;
}

void rga_mm_dump_buffer(struct rga_internal_buffer *dump_buffer)
{
	pr_info("handle = %d refcount = %d mm_flag = 0x%x\n",
		dump_buffer->handle, kref_read(&dump_buffer->refcount),
		dump_buffer->mm_flag);

	switch (dump_buffer->type) {
	case RGA_DMA_BUFFER:
	case RGA_DMA_BUFFER_PTR:
		if (rga_mm_is_invalid_dma_buffer(dump_buffer->dma_buffer))
			break;

		pr_info("dma_buffer:\n");
		pr_info("dma_buf = %p, iova = 0x%lx, sgt = %p, size = %ld, map_core = 0x%x\n",
			dump_buffer->dma_buffer->dma_buf,
			(unsigned long)dump_buffer->dma_buffer->iova,
			dump_buffer->dma_buffer->sgt,
			dump_buffer->dma_buffer->size,
			dump_buffer->dma_buffer->scheduler->core);

		if (dump_buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS)
			pr_info("is contiguous, pa = 0x%lx\n",
				(unsigned long)dump_buffer->phys_addr);
		break;
	case RGA_VIRTUAL_ADDRESS:
		if (dump_buffer->virt_addr == NULL)
			break;

		pr_info("virtual address:\n");
		pr_info("va = 0x%lx, pages = %p, size = %ld\n",
			(unsigned long)dump_buffer->virt_addr->addr,
			dump_buffer->virt_addr->pages,
			dump_buffer->virt_addr->size);

		if (rga_mm_is_invalid_dma_buffer(dump_buffer->dma_buffer))
			break;

		pr_info("iova = 0x%lx, offset = 0x%lx, sgt = %p, size = %ld, map_core = 0x%x\n",
			(unsigned long)dump_buffer->dma_buffer->iova,
			(unsigned long)dump_buffer->dma_buffer->offset,
			dump_buffer->dma_buffer->sgt,
			dump_buffer->dma_buffer->size,
			dump_buffer->dma_buffer->scheduler->core);

		if (dump_buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS)
			pr_info("is contiguous, pa = 0x%lx\n",
				(unsigned long)dump_buffer->phys_addr);
		break;
	case RGA_PHYSICAL_ADDRESS:
		pr_info("physical address: pa = 0x%lx\n", (unsigned long)dump_buffer->phys_addr);
		break;
	default:
		pr_err("Illegal external buffer!\n");
		break;
	}
}

void rga_mm_dump_info(struct rga_mm *mm_session)
{
	int id;
	struct rga_internal_buffer *dump_buffer;

	WARN_ON(!mutex_is_locked(&mm_session->lock));

	pr_info("rga mm info:\n");

	pr_info("buffer count = %d\n", mm_session->buffer_count);
	pr_info("===============================================================\n");

	idr_for_each_entry(&mm_session->memory_idr, dump_buffer, id) {
		rga_mm_dump_buffer(dump_buffer);

		pr_info("---------------------------------------------------------------\n");
	}
}

static bool rga_mm_is_need_mmu(struct rga_job *job, struct rga_internal_buffer *buffer)
{
	if (buffer == NULL || job == NULL || job->scheduler == NULL)
		return false;

	/* RK_IOMMU no need to configure enable or not in the driver. */
	if (job->scheduler->data->mmu == RGA_IOMMU)
		return false;

	/* RK_MMU need to configure enable or not in the driver. */
	if (buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS)
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

	src_mmu_en = rga_mm_is_need_mmu(job, job->src_buffer.addr);
	src1_mmu_en = rga_mm_is_need_mmu(job, job->src1_buffer.addr);
	dst_mmu_en = rga_mm_is_need_mmu(job, job->dst_buffer.addr);
	els_mmu_en = rga_mm_is_need_mmu(job, job->els_buffer.addr);

	mmu_info = &job->rga_command_base.mmu_info;
	memset(mmu_info, 0x0, sizeof(*mmu_info));
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

static int rga_mm_sgt_to_page_table(struct sg_table *sg,
				    uint32_t *page_table,
				    int32_t pageCount,
				    int32_t use_dma_address)
{
	uint32_t i;
	unsigned long Address;
	uint32_t mapped_size = 0;
	uint32_t len;
	struct scatterlist *sgl = sg->sgl;
	uint32_t sg_num = 0;
	uint32_t break_flag = 0;

	do {
		/*
		 *   The length of each sgl is expected to be obtained here, not
		 * the length of the entire dma_buf, so sg_dma_len() is not used.
		 */
		len = sgl->length >> PAGE_SHIFT;

		if (use_dma_address)
			/*
			 *   The fd passed by user space gets sg through
			 * dma_buf_map_attachment, so dma_address can
			 * be use here.
			 *   When the mapped device does not have iommu, it will
			 * return the first address of the real physical page
			 * when it meets the requirements of the current device,
			 * and will trigger swiotlb when it does not meet the
			 * requirements to obtain a software-mapped physical
			 * address that is mapped to meet the device address
			 * requirements.
			 */
			Address = sg_dma_address(sgl);
		else
			Address = sg_phys(sgl);

		for (i = 0; i < len; i++) {
			if (mapped_size + i >= pageCount) {
				break_flag = 1;
				break;
			}
			page_table[mapped_size + i] = (uint32_t)(Address + (i << PAGE_SHIFT));
		}
		if (break_flag)
			break;
		mapped_size += len;
		sg_num += 1;
	} while ((sgl = sg_next(sgl)) && (mapped_size < pageCount) && (sg_num < sg->orig_nents));

	return 0;
}

static int rga_mm_set_mmu_base(struct rga_job *job,
			       struct rga_img_info_t *img,
			       struct rga_job_buffer *job_buf)
{
	int ret;
	int yrgb_count = 0;
	int uv_count = 0;
	int v_count = 0;
	int page_count = 0;
	int order = 0;
	uint32_t *page_table = NULL;
	struct sg_table *sgt = NULL;

	int img_size, yrgb_size, uv_size, v_size;
	int img_offset = 0;
	int yrgb_offset = 0;
	int uv_offset = 0;
	int v_offset = 0;

	img_size = rga_image_size_cal(img->vir_w, img->vir_h, img->format,
				      &yrgb_size, &uv_size, &v_size);
	if (img_size <= 0) {
		pr_err("Image size cal error! width = %d, height = %d, format = %s\n",
		       img->vir_w, img->vir_h, rga_get_format_name(img->format));
		return -EINVAL;
	}

	/* using third-address */
	if (job_buf->uv_addr) {
		if (job_buf->y_addr->virt_addr != NULL)
			yrgb_offset = job_buf->y_addr->virt_addr->offset;
		if (job_buf->uv_addr->virt_addr != NULL)
			uv_offset = job_buf->uv_addr->virt_addr->offset;
		if (job_buf->v_addr->virt_addr != NULL)
			v_offset = job_buf->v_addr->virt_addr->offset;

		yrgb_count = RGA_GET_PAGE_COUNT(yrgb_size + yrgb_offset);
		uv_count = RGA_GET_PAGE_COUNT(uv_size + uv_offset);
		v_count = RGA_GET_PAGE_COUNT(v_size + v_offset);
		page_count = yrgb_count + uv_count + v_count;

		if (page_count <= 0) {
			pr_err("page count cal error! yrba = %d, uv = %d, v = %d\n",
			       yrgb_count, uv_count, v_count);
			return -EFAULT;
		}

		if (job->flags & RGA_JOB_USE_HANDLE) {
			order = get_order(page_count * sizeof(uint32_t *));
			if (order >= MAX_ORDER) {
				pr_err("Can not alloc pages with order[%d] for page_table, max_order = %d\n",
				       order, MAX_ORDER);
				return -ENOMEM;
			}

			page_table = (uint32_t *)__get_free_pages(GFP_KERNEL | GFP_DMA32, order);
			if (page_table == NULL) {
				pr_err("%s can not alloc pages for page_table, order = %d\n",
				       __func__, order);
				return -ENOMEM;
			}
		} else {
			mutex_lock(&rga_drvdata->lock);

			page_table = rga_mmu_buf_get(rga_drvdata->mmu_base, page_count);
			if (page_table == NULL) {
				pr_err("mmu_buf get error!\n");
				mutex_unlock(&rga_drvdata->lock);
				return -EFAULT;
			}

			mutex_unlock(&rga_drvdata->lock);
		}

		sgt = rga_mm_lookup_sgt(job_buf->y_addr);
		if (sgt == NULL) {
			pr_err("rga2 cannot get sgt from internal buffer!\n");
			ret = -EINVAL;
			goto err_free_page_table;
		}
		rga_mm_sgt_to_page_table(sgt, page_table, yrgb_count, false);

		sgt = rga_mm_lookup_sgt(job_buf->uv_addr);
		if (sgt == NULL) {
			pr_err("rga2 cannot get sgt from internal buffer!\n");
			ret = -EINVAL;
			goto err_free_page_table;
		}
		rga_mm_sgt_to_page_table(sgt, page_table + yrgb_count, uv_count, false);

		sgt = rga_mm_lookup_sgt(job_buf->v_addr);
		if (sgt == NULL) {
			pr_err("rga2 cannot get sgt from internal buffer!\n");
			ret = -EINVAL;
			goto err_free_page_table;
		}
		rga_mm_sgt_to_page_table(sgt, page_table + yrgb_count + uv_count, v_count, false);

		img->yrgb_addr = yrgb_offset;
		img->uv_addr = (yrgb_count << PAGE_SHIFT) + uv_offset;
		img->v_addr = ((yrgb_count + uv_count) << PAGE_SHIFT) + v_offset;
	} else {
		if (job_buf->addr->virt_addr != NULL)
			img_offset = job_buf->addr->virt_addr->offset;

		page_count = RGA_GET_PAGE_COUNT(img_size + img_offset);
		if (page_count < 0) {
			pr_err("page count cal error! yrba = %d, uv = %d, v = %d\n",
			       yrgb_count, uv_count, v_count);
			return -EFAULT;
		}

		if (job->flags & RGA_JOB_USE_HANDLE) {
			order = get_order(page_count * sizeof(uint32_t *));
			if (order >= MAX_ORDER) {
				pr_err("Can not alloc pages with order[%d] for page_table, max_order = %d\n",
				       order, MAX_ORDER);
				return -ENOMEM;
			}

			page_table = (uint32_t *)__get_free_pages(GFP_KERNEL | GFP_DMA32, order);
			if (page_table == NULL) {
				pr_err("%s can not alloc pages for page_table, order = %d\n",
				       __func__, order);
				return -ENOMEM;
			}
		} else {
			mutex_lock(&rga_drvdata->lock);

			page_table = rga_mmu_buf_get(rga_drvdata->mmu_base, page_count);
			if (page_table == NULL) {
				pr_err("mmu_buf get error!\n");
				mutex_unlock(&rga_drvdata->lock);
				return -EFAULT;
			}

			mutex_unlock(&rga_drvdata->lock);
		}

		sgt = rga_mm_lookup_sgt(job_buf->addr);
		if (sgt == NULL) {
			pr_err("rga2 cannot get sgt from internal buffer!\n");
			ret = -EINVAL;
			goto err_free_page_table;
		}
		rga_mm_sgt_to_page_table(sgt, page_table, page_count, false);

		img->yrgb_addr = img_offset;
		rga_convert_addr(img, false);
	}

	job_buf->page_table = page_table;
	job_buf->order = order;
	job_buf->page_count = page_count;

	return 0;

err_free_page_table:
	if (job->flags & RGA_JOB_USE_HANDLE)
		free_pages((unsigned long)page_table, order);
	return ret;
}

static int rga_mm_sync_dma_sg_for_device(struct rga_internal_buffer *buffer,
					 struct rga_job *job,
					 enum dma_data_direction dir)
{
	struct sg_table *sgt;
	struct rga_scheduler_t *scheduler;

	scheduler = buffer->dma_buffer->scheduler;
	if (scheduler == NULL) {
		pr_err("%s(%d), failed to get scheduler, core = 0x%x\n",
		       __func__, __LINE__, job->core);
		return -EFAULT;
	}

	if (buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS) {
		dma_sync_single_for_device(scheduler->dev, buffer->phys_addr, buffer->size, dir);
	} else {
		sgt = rga_mm_lookup_sgt(buffer);
		if (sgt == NULL) {
			pr_err("%s(%d), failed to get sgt, core = 0x%x\n",
			       __func__, __LINE__, job->core);
			return -EINVAL;
		}

		dma_sync_sg_for_device(scheduler->dev, sgt->sgl, sgt->orig_nents, dir);
	}

	return 0;
}

static int rga_mm_sync_dma_sg_for_cpu(struct rga_internal_buffer *buffer,
				      struct rga_job *job,
				      enum dma_data_direction dir)
{
	struct sg_table *sgt;
	struct rga_scheduler_t *scheduler;

	scheduler = buffer->dma_buffer->scheduler;
	if (scheduler == NULL) {
		pr_err("%s(%d), failed to get scheduler, core = 0x%x\n",
		       __func__, __LINE__, job->core);
		return -EFAULT;
	}

	if (buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS) {
		dma_sync_single_for_cpu(scheduler->dev, buffer->phys_addr, buffer->size, dir);
	} else {
		sgt = rga_mm_lookup_sgt(buffer);
		if (sgt == NULL) {
			pr_err("%s(%d), failed to get sgt, core = 0x%x\n",
			       __func__, __LINE__, job->core);
			return -EINVAL;
		}

		dma_sync_sg_for_cpu(scheduler->dev, sgt->sgl, sgt->orig_nents, dir);
	}

	return 0;
}

static int rga_mm_get_buffer_info(struct rga_job *job,
				  struct rga_internal_buffer *internal_buffer,
				  uint64_t *channel_addr)
{
	uint64_t addr;

	switch (job->scheduler->data->mmu) {
	case RGA_IOMMU:
		addr = rga_mm_lookup_iova(internal_buffer);
		if (addr == 0) {
			pr_err("core[%d] lookup buffer_type[0x%x] iova error!\n",
			       job->core, internal_buffer->type);
			return -EINVAL;
		}
		break;
	case RGA_MMU:
	default:
		if (internal_buffer->mm_flag & RGA_MEM_PHYSICAL_CONTIGUOUS) {
			addr = internal_buffer->phys_addr;
			break;
		}

		switch (internal_buffer->type) {
		case RGA_DMA_BUFFER:
		case RGA_DMA_BUFFER_PTR:
			addr = 0;
			break;
		case RGA_VIRTUAL_ADDRESS:
			addr = internal_buffer->virt_addr->addr;
			break;
		case RGA_PHYSICAL_ADDRESS:
			addr = internal_buffer->phys_addr;
			break;
		default:
			pr_err("Illegal external buffer!\n");
			return -EFAULT;
		}
		break;
	}

	*channel_addr = addr;

	return 0;
}

static int rga_mm_get_buffer(struct rga_mm *mm,
			     struct rga_job *job,
			     uint64_t handle,
			     uint64_t *channel_addr,
			     struct rga_internal_buffer **buf,
			     int require_size,
			     enum dma_data_direction dir)
{
	int ret = 0;
	struct rga_internal_buffer *internal_buffer = NULL;

	if (handle == 0) {
		pr_err("No buffer handle can be used!\n");
		return -EFAULT;
	}

	mutex_lock(&mm->lock);
	*buf = rga_mm_lookup_handle(mm, handle);
	if (*buf == NULL) {
		pr_err("This handle[%ld] is illegal.\n", (unsigned long)handle);

		mutex_unlock(&mm->lock);
		return -EFAULT;
	}

	internal_buffer = *buf;
	kref_get(&internal_buffer->refcount);

	if (DEBUGGER_EN(MM)) {
		pr_info("handle[%d] get info:\n", (int)handle);
		rga_mm_dump_buffer(internal_buffer);
	}

	mutex_unlock(&mm->lock);

	ret = rga_mm_get_buffer_info(job, internal_buffer, channel_addr);
	if (ret < 0) {
		pr_err("handle[%ld] failed to get internal buffer info!\n", (unsigned long)handle);
		return ret;
	}

	if (internal_buffer->size < require_size) {
		ret = -EINVAL;
		pr_err("Only get buffer %ld byte from handle[%ld], but current required %d byte\n",
		       internal_buffer->size, (unsigned long)handle, require_size);

		goto put_internal_buffer;
	}

	if (internal_buffer->mm_flag & RGA_MEM_FORCE_FLUSH_CACHE) {
		/*
		 * Some userspace virtual addresses do not have an
		 * interface for flushing the cache, so it is mandatory
		 * to flush the cache when the virtual address is used.
		 */
		ret = rga_mm_sync_dma_sg_for_device(internal_buffer, job, dir);
		if (ret < 0) {
			pr_err("sync sgt for device error!\n");
			goto put_internal_buffer;
		}
	}

	return 0;

put_internal_buffer:
	mutex_lock(&mm->lock);
	kref_put(&internal_buffer->refcount, rga_mm_kref_release_buffer);
	mutex_unlock(&mm->lock);

	return ret;

}

static void rga_mm_put_buffer(struct rga_mm *mm,
			      struct rga_job *job,
			      struct rga_internal_buffer *internal_buffer,
			      enum dma_data_direction dir)
{
	if (internal_buffer->mm_flag & RGA_MEM_FORCE_FLUSH_CACHE && dir != DMA_NONE)
		if (rga_mm_sync_dma_sg_for_cpu(internal_buffer, job, dir))
			pr_err("sync sgt for cpu error!\n");

	mutex_lock(&mm->lock);
	kref_put(&internal_buffer->refcount, rga_mm_kref_release_buffer);
	mutex_unlock(&mm->lock);
}

static void rga_mm_put_channel_handle_info(struct rga_mm *mm,
					   struct rga_job *job,
					   struct rga_job_buffer *job_buf,
					   enum dma_data_direction dir)
{
	if (job_buf->y_addr)
		rga_mm_put_buffer(mm, job, job_buf->y_addr, dir);
	if (job_buf->uv_addr)
		rga_mm_put_buffer(mm, job, job_buf->uv_addr, dir);
	if (job_buf->v_addr)
		rga_mm_put_buffer(mm, job, job_buf->v_addr, dir);

	if (job_buf->page_table)
		free_pages((unsigned long)job_buf->page_table, job_buf->order);
}

static int rga_mm_get_channel_handle_info(struct rga_mm *mm,
					  struct rga_job *job,
					  struct rga_img_info_t *img,
					  struct rga_job_buffer *job_buf,
					  enum dma_data_direction dir)
{
	int ret = 0;
	int handle = 0;
	int img_size, yrgb_size, uv_size, v_size;

	img_size = rga_image_size_cal(img->vir_w, img->vir_h, img->format,
				      &yrgb_size, &uv_size, &v_size);
	if (img_size <= 0) {
		pr_err("Image size cal error! width = %d, height = %d, format = %s\n",
		       img->vir_w, img->vir_h, rga_get_format_name(img->format));
		return -EINVAL;
	}

	/* using third-address */
	if (img->uv_addr > 0) {
		handle = img->yrgb_addr;
		if (handle > 0) {
			ret = rga_mm_get_buffer(mm, job, handle, &img->yrgb_addr,
						&job_buf->y_addr, yrgb_size, dir);
			if (ret < 0) {
				pr_err("handle[%d] Can't get y/rgb address info!\n", handle);
				return ret;
			}
		}

		handle = img->uv_addr;
		if (handle > 0) {
			ret = rga_mm_get_buffer(mm, job, handle, &img->uv_addr,
						&job_buf->uv_addr, uv_size, dir);
			if (ret < 0) {
				pr_err("handle[%d] Can't get uv address info!\n", handle);
				return ret;
			}
		}

		handle = img->v_addr;
		if (handle > 0) {
			ret = rga_mm_get_buffer(mm, job, handle, &img->v_addr,
						&job_buf->v_addr, v_size, dir);
			if (ret < 0) {
				pr_err("handle[%d] Can't get uv address info!\n", handle);
				return ret;
			}
		}
	} else {
		handle = img->yrgb_addr;
		if (handle > 0) {
			ret = rga_mm_get_buffer(mm, job, handle, &img->yrgb_addr,
						&job_buf->addr, img_size, dir);
			if (ret < 0) {
				pr_err("handle[%d] Can't get y/rgb address info!\n", handle);
				return ret;
			}
		}

		rga_convert_addr(img, false);
	}

	if (job->scheduler->data->mmu == RGA_MMU &&
	    rga_mm_is_need_mmu(job, job_buf->addr)) {
		ret = rga_mm_set_mmu_base(job, img, job_buf);
		if (ret < 0) {
			pr_err("Can't set RGA2 MMU_BASE from handle!\n");

			rga_mm_put_channel_handle_info(mm, job, job_buf, dir);
			return ret;
		}
	}

	return 0;
}

static int rga_mm_get_handle_info(struct rga_job *job)
{
	int ret = 0;
	struct rga_req *req = NULL;
	struct rga_mm *mm = NULL;
	enum dma_data_direction dir;

	req = &job->rga_command_base;
	mm = rga_drvdata->mm;

	switch (req->render_mode) {
	case BITBLT_MODE:
	case COLOR_PALETTE_MODE:
		if (unlikely(req->src.yrgb_addr <= 0)) {
			pr_err("render_mode[0x%x] src0 channel handle[%ld] must is valid!",
			       req->render_mode, (unsigned long)req->src.yrgb_addr);
			return -EINVAL;
		}

		if (unlikely(req->dst.yrgb_addr <= 0)) {
			pr_err("render_mode[0x%x] dst channel handle[%ld] must is valid!",
			       req->render_mode, (unsigned long)req->dst.yrgb_addr);
			return -EINVAL;
		}

		if (req->bsfilter_flag) {
			if (unlikely(req->pat.yrgb_addr <= 0)) {
				pr_err("render_mode[0x%x] src1/pat channel handle[%ld] must is valid!",
				       req->render_mode, (unsigned long)req->pat.yrgb_addr);
				return -EINVAL;
			}
		}

		break;
	case COLOR_FILL_MODE:
		if (unlikely(req->dst.yrgb_addr <= 0)) {
			pr_err("render_mode[0x%x] dst channel handle[%ld] must is valid!",
			       req->render_mode, (unsigned long)req->dst.yrgb_addr);
			return -EINVAL;
		}

		break;

	case UPDATE_PALETTE_TABLE_MODE:
	case UPDATE_PATTEN_BUF_MODE:
		if (unlikely(req->pat.yrgb_addr <= 0)) {
			pr_err("render_mode[0x%x] lut/pat channel handle[%ld] must is valid!, req->render_mode",
			       req->render_mode, (unsigned long)req->pat.yrgb_addr);
			return -EINVAL;
		}

		break;
	default:
		pr_err("%s, unknown render mode!\n", __func__);
		break;
	}

	if (likely(req->src.yrgb_addr > 0)) {
		ret = rga_mm_get_channel_handle_info(mm, job, &req->src,
						     &job->src_buffer,
						     DMA_TO_DEVICE);
		if (ret < 0) {
			pr_err("Can't get src buffer info from handle!\n");
			return ret;
		}
	}

	if (likely(req->dst.yrgb_addr > 0)) {
		ret = rga_mm_get_channel_handle_info(mm, job, &req->dst,
						     &job->dst_buffer,
						     DMA_TO_DEVICE);
		if (ret < 0) {
			pr_err("Can't get dst buffer info from handle!\n");
			return ret;
		}
	}

	if (likely(req->pat.yrgb_addr > 0)) {

		if (req->render_mode != UPDATE_PALETTE_TABLE_MODE) {
			if (req->bsfilter_flag)
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
			pr_err("Can't get pat buffer info from handle!\n");
			return ret;
		}
	}

	rga_mm_set_mmu_flag(job);

	return 0;
}

static void rga_mm_put_handle_info(struct rga_job *job)
{
	struct rga_mm *mm = rga_drvdata->mm;

	rga_mm_put_channel_handle_info(mm, job, &job->src_buffer, DMA_NONE);
	rga_mm_put_channel_handle_info(mm, job, &job->dst_buffer, DMA_FROM_DEVICE);
	rga_mm_put_channel_handle_info(mm, job, &job->src1_buffer, DMA_NONE);
	rga_mm_put_channel_handle_info(mm, job, &job->els_buffer, DMA_NONE);
}

static void rga_mm_put_channel_external_buffer(struct rga_job_buffer *job_buffer)
{
	if (job_buffer->ex_addr->type == RGA_DMA_BUFFER_PTR)
		dma_buf_put((struct dma_buf *)(unsigned long)job_buffer->ex_addr->memory);

	kfree(job_buffer->ex_addr);
	job_buffer->ex_addr = NULL;
}

static int rga_mm_get_channel_external_buffer(int mmu_flag,
					      struct rga_img_info_t *img_info,
					      struct rga_job_buffer *job_buffer)
{
	struct dma_buf *dma_buf = NULL;
	struct rga_external_buffer *external_buffer = NULL;

	/* Default unsupported multi-planar format */
	external_buffer = kzalloc(sizeof(*external_buffer), GFP_KERNEL);
	if (external_buffer == NULL) {
		pr_err("Cannot alloc job_buffer!\n");
		return -ENOMEM;
	}

	if (img_info->yrgb_addr) {
		dma_buf = dma_buf_get(img_info->yrgb_addr);
		if (IS_ERR(dma_buf)) {
			pr_err("%s dma_buf_get fail fd[%lu]\n",
			       __func__, (unsigned long)img_info->yrgb_addr);
			kfree(external_buffer);
			return -EINVAL;
		}

		external_buffer->memory = (unsigned long)dma_buf;
		external_buffer->type = RGA_DMA_BUFFER_PTR;
	} else if (mmu_flag && img_info->uv_addr) {
		external_buffer->memory = (uint64_t)img_info->uv_addr;
		external_buffer->type = RGA_VIRTUAL_ADDRESS;
	} else if (img_info->uv_addr) {
		external_buffer->memory = (uint64_t)img_info->uv_addr;
		external_buffer->type = RGA_PHYSICAL_ADDRESS;
	} else {
		kfree(external_buffer);
		return -EINVAL;
	}

	external_buffer->memory_parm.width = img_info->vir_w;
	external_buffer->memory_parm.height = img_info->vir_h;
	external_buffer->memory_parm.format = img_info->format;

	job_buffer->ex_addr = external_buffer;

	return 0;
}

static void rga_mm_put_external_buffer(struct rga_job *job)
{
	if (job->src_buffer.ex_addr)
		rga_mm_put_channel_external_buffer(&job->src_buffer);
	if (job->src1_buffer.ex_addr)
		rga_mm_put_channel_external_buffer(&job->src1_buffer);
	if (job->dst_buffer.ex_addr)
		rga_mm_put_channel_external_buffer(&job->dst_buffer);
	if (job->els_buffer.ex_addr)
		rga_mm_put_channel_external_buffer(&job->els_buffer);
}

static int rga_mm_get_external_buffer(struct rga_job *job)
{
	int ret = -EINVAL;
	int mmu_flag;

	struct rga_img_info_t *src0 = NULL;
	struct rga_img_info_t *src1 = NULL;
	struct rga_img_info_t *dst = NULL;
	struct rga_img_info_t *els = NULL;

	if (job->rga_command_base.render_mode != COLOR_FILL_MODE)
		src0 = &job->rga_command_base.src;

	if (job->rga_command_base.render_mode != UPDATE_PALETTE_TABLE_MODE)
		src1 = job->rga_command_base.bsfilter_flag ?
		       &job->rga_command_base.pat : NULL;
	else
		els = &job->rga_command_base.pat;

	dst = &job->rga_command_base.dst;

	if (likely(src0)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 8) & 1);
		ret = rga_mm_get_channel_external_buffer(mmu_flag, src0, &job->src_buffer);
		if (ret < 0) {
			pr_err("Cannot get src0 channel buffer!\n");
			return ret;
		}
	}

	if (likely(dst)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 10) & 1);
		ret = rga_mm_get_channel_external_buffer(mmu_flag, dst, &job->dst_buffer);
		if (ret < 0) {
			pr_err("Cannot get dst channel buffer!\n");
			goto error_put_buffer;
		}
	}

	if (src1) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 9) & 1);
		ret = rga_mm_get_channel_external_buffer(mmu_flag, src1, &job->src1_buffer);
		if (ret < 0) {
			pr_err("Cannot get src1 channel buffer!\n");
			goto error_put_buffer;
		}
	}

	if (els) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 11) & 1);
		ret = rga_mm_get_channel_external_buffer(mmu_flag, els, &job->els_buffer);
		if (ret < 0) {
			pr_err("Cannot get els channel buffer!\n");
			goto error_put_buffer;
		}
	}

	return 0;
error_put_buffer:
	rga_mm_put_external_buffer(job);
	return ret;
}

static void rga_mm_unmap_channel_job_buffer(struct rga_job *job,
					    struct rga_job_buffer *job_buffer,
					    enum dma_data_direction dir)
{
	if (job_buffer->addr->mm_flag & RGA_MEM_FORCE_FLUSH_CACHE && dir != DMA_NONE)
		if (rga_mm_sync_dma_sg_for_cpu(job_buffer->addr, job, dir))
			pr_err("sync sgt for cpu error!\n");

	rga_mm_unmap_buffer(job_buffer->addr);
	kfree(job_buffer->addr);

	job_buffer->page_table = NULL;
}

static int rga_mm_map_channel_job_buffer(struct rga_job *job,
					 struct rga_img_info_t *img,
					 struct rga_job_buffer *job_buffer,
					 enum dma_data_direction dir,
					 int write_flag)
{
	int ret;
	struct rga_internal_buffer *buffer = NULL;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (buffer == NULL) {
		pr_err("%s alloc internal_buffer error!\n", __func__);
		return -ENOMEM;
	}

	ret = rga_mm_map_buffer(job_buffer->ex_addr, buffer, job, write_flag);
	if (ret < 0) {
		pr_err("job buffer map failed!\n");
		goto error_free_buffer;
	}

	ret = rga_mm_get_buffer_info(job, buffer, &img->yrgb_addr);
	if (ret < 0) {
		pr_err("Failed to get internal buffer info!\n");
		goto error_unmap_buffer;
	}

	if (buffer->mm_flag & RGA_MEM_FORCE_FLUSH_CACHE) {
		ret = rga_mm_sync_dma_sg_for_device(buffer, job, dir);
		if (ret < 0) {
			pr_err("sync sgt for device error!\n");
			goto error_unmap_buffer;
		}
	}

	rga_convert_addr(img, false);

	job_buffer->addr = buffer;

	if (job->scheduler->data->mmu == RGA_MMU &&
	    rga_mm_is_need_mmu(job, job_buffer->addr)) {
		ret = rga_mm_set_mmu_base(job, img, job_buffer);
		if (ret < 0) {
			pr_err("Can't set RGA2 MMU_BASE!\n");
			job_buffer->addr = NULL;
			goto error_unmap_buffer;
		}
	}

	return 0;

error_unmap_buffer:
	rga_mm_unmap_buffer(buffer);
error_free_buffer:
	kfree(buffer);

	return ret;
}

static void rga_mm_unmap_buffer_info(struct rga_job *job)
{
	if (job->src_buffer.addr)
		rga_mm_unmap_channel_job_buffer(job, &job->src_buffer, DMA_NONE);
	if (job->dst_buffer.addr)
		rga_mm_unmap_channel_job_buffer(job, &job->dst_buffer, DMA_FROM_DEVICE);
	if (job->src1_buffer.addr)
		rga_mm_unmap_channel_job_buffer(job, &job->src1_buffer, DMA_NONE);
	if (job->els_buffer.addr)
		rga_mm_unmap_channel_job_buffer(job, &job->els_buffer, DMA_NONE);

	rga_mm_put_external_buffer(job);
}

static int rga_mm_map_buffer_info(struct rga_job *job)
{
	int ret = 0;
	struct rga_req *req = NULL;
	enum dma_data_direction dir;

	ret = rga_mm_get_external_buffer(job);
	if (ret < 0) {
		pr_err("failed to get external buffer from job_cmd!\n");
		return ret;
	}

	req = &job->rga_command_base;

	if (likely(job->src_buffer.ex_addr)) {
		ret = rga_mm_map_channel_job_buffer(job, &req->src,
						    &job->src_buffer,
						    DMA_TO_DEVICE, false);
		if (ret < 0) {
			pr_err("src channel map job buffer failed!");
			goto error_unmap_buffer;
		}
	}

	if (likely(job->dst_buffer.ex_addr)) {
		ret = rga_mm_map_channel_job_buffer(job, &req->dst,
						    &job->dst_buffer,
						    DMA_TO_DEVICE, true);
		if (ret < 0) {
			pr_err("dst channel map job buffer failed!");
			goto error_unmap_buffer;
		}
	}

	if (job->src1_buffer.ex_addr) {
		if (req->bsfilter_flag)
			dir = DMA_BIDIRECTIONAL;
		else
			dir = DMA_TO_DEVICE;

		ret = rga_mm_map_channel_job_buffer(job, &req->pat,
						    &job->src1_buffer,
						    dir, false);
		if (ret < 0) {
			pr_err("src1 channel map job buffer failed!");
			goto error_unmap_buffer;
		}
	}

	if (job->els_buffer.ex_addr) {
		ret = rga_mm_map_channel_job_buffer(job, &req->pat,
						    &job->els_buffer,
						    DMA_BIDIRECTIONAL, false);
		if (ret < 0) {
			pr_err("els channel map job buffer failed!");
			goto error_unmap_buffer;
		}
	}

	rga_mm_set_mmu_flag(job);
	return 0;

error_unmap_buffer:
	rga_mm_unmap_buffer_info(job);

	return ret;
}

int rga_mm_map_job_info(struct rga_job *job)
{
	int ret;

	if (job->flags & RGA_JOB_USE_HANDLE) {
		ret = rga_mm_get_handle_info(job);
		if (ret < 0) {
			pr_err("failed to get buffer from handle\n");
			return ret;
		}
	} else {
		ret = rga_mm_map_buffer_info(job);
		if (ret < 0) {
			pr_err("failed to map buffer\n");
			return ret;
		}
	}

	return 0;
}

void rga_mm_unmap_job_info(struct rga_job *job)
{
	if (job->flags & RGA_JOB_USE_HANDLE)
		rga_mm_put_handle_info(job);
	else
		rga_mm_unmap_buffer_info(job);
}

uint32_t rga_mm_import_buffer(struct rga_external_buffer *external_buffer,
			      struct rga_session *session)
{
	int ret = 0, new_id;
	struct rga_mm *mm;
	struct rga_internal_buffer *internal_buffer;

	mm = rga_drvdata->mm;
	if (mm == NULL) {
		pr_err("rga mm is null!\n");
		return 0;
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
		return 0;
	}

	ret = rga_mm_map_buffer(external_buffer, internal_buffer, NULL, true);
	if (ret < 0)
		goto FREE_INTERNAL_BUFFER;

	kref_init(&internal_buffer->refcount);
	internal_buffer->session = session;

	/*
	 * Get the user-visible handle using idr. Preload and perform
	 * allocation under our spinlock.
	 */
	idr_preload(GFP_KERNEL);
	new_id = idr_alloc_cyclic(&mm->memory_idr, internal_buffer, 1, 0, GFP_NOWAIT);
	idr_preload_end();
	if (new_id < 0) {
		pr_err("internal_buffer alloc id failed!\n");
		goto FREE_INTERNAL_BUFFER;
	}

	internal_buffer->handle = new_id;
	mm->buffer_count++;

	if (DEBUGGER_EN(MM)) {
		pr_info("import buffer:\n");
		rga_mm_dump_buffer(internal_buffer);
	}

	mutex_unlock(&mm->lock);
	return internal_buffer->handle;

FREE_INTERNAL_BUFFER:
	mutex_unlock(&mm->lock);
	kfree(internal_buffer);

	return 0;
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

	if (DEBUGGER_EN(MM)) {
		pr_info("release buffer:\n");
		rga_mm_dump_buffer(internal_buffer);
	}

	kref_put(&internal_buffer->refcount, rga_mm_kref_release_buffer);

	mutex_unlock(&mm->lock);
	return 0;
}

int rga_mm_session_release_buffer(struct rga_session *session)
{
	int i;
	struct rga_mm *mm;
	struct rga_internal_buffer *buffer;

	mm = rga_drvdata->mm;
	if (mm == NULL) {
		pr_err("rga mm is null!\n");
		return -EFAULT;
	}

	mutex_lock(&mm->lock);

	idr_for_each_entry(&mm->memory_idr, buffer, i) {
		if (session == buffer->session) {
			pr_err("[tgid:%d] Decrement the reference of handle[%d] when the user exits\n",
			       session->tgid, buffer->handle);
			kref_put(&buffer->refcount, rga_mm_kref_release_buffer);
		}
	}

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
