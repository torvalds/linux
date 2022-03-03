// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga_dma_buf: " fmt

#include "rga_dma_buf.h"
#include "rga.h"
#include "rga_common.h"
#include "rga_job.h"

/**
 * rga_dma_info_to_prot - Translate DMA API directions and attributes to IOMMU API
 *                    page flags.
 * @dir: Direction of DMA transfer
 * @coherent: Is the DMA master cache-coherent?
 *
 * Return: corresponding IOMMU API page protection flags
 */
static int rga_dma_info_to_prot(enum dma_data_direction dir, bool coherent)
{
	int prot = coherent ? IOMMU_CACHE : 0;

	switch (dir) {
	case DMA_BIDIRECTIONAL:
		return prot | IOMMU_READ | IOMMU_WRITE;
	case DMA_TO_DEVICE:
		return prot | IOMMU_READ;
	case DMA_FROM_DEVICE:
		return prot | IOMMU_WRITE;
	default:
		return 0;
	}
}

int rga_buf_size_cal(unsigned long yrgb_addr, unsigned long uv_addr,
		      unsigned long v_addr, int format, uint32_t w,
		      uint32_t h, unsigned long *StartAddr, unsigned long *size)
{
	uint32_t size_yrgb = 0;
	uint32_t size_uv = 0;
	uint32_t size_v = 0;
	uint32_t stride = 0;
	unsigned long start, end;
	uint32_t pageCount;

	switch (format) {
	case RGA_FORMAT_RGBA_8888:
	case RGA_FORMAT_RGBX_8888:
	case RGA_FORMAT_BGRA_8888:
	case RGA_FORMAT_BGRX_8888:
	case RGA_FORMAT_ARGB_8888:
	case RGA_FORMAT_XRGB_8888:
	case RGA_FORMAT_ABGR_8888:
	case RGA_FORMAT_XBGR_8888:
		stride = (w * 4 + 3) & (~3);
		size_yrgb = stride * h;
		start = yrgb_addr >> PAGE_SHIFT;
		end = yrgb_addr + size_yrgb;
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	case RGA_FORMAT_RGB_888:
	case RGA_FORMAT_BGR_888:
		stride = (w * 3 + 3) & (~3);
		size_yrgb = stride * h;
		start = yrgb_addr >> PAGE_SHIFT;
		end = yrgb_addr + size_yrgb;
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	case RGA_FORMAT_RGB_565:
	case RGA_FORMAT_RGBA_5551:
	case RGA_FORMAT_RGBA_4444:
	case RGA_FORMAT_BGR_565:
	case RGA_FORMAT_BGRA_5551:
	case RGA_FORMAT_BGRA_4444:
	case RGA_FORMAT_ARGB_5551:
	case RGA_FORMAT_ARGB_4444:
	case RGA_FORMAT_ABGR_5551:
	case RGA_FORMAT_ABGR_4444:
		stride = (w * 2 + 3) & (~3);
		size_yrgb = stride * h;
		start = yrgb_addr >> PAGE_SHIFT;
		end = yrgb_addr + size_yrgb;
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;

		/* YUV FORMAT */
	case RGA_FORMAT_YCbCr_422_SP:
	case RGA_FORMAT_YCrCb_422_SP:
		stride = (w + 3) & (~3);
		size_yrgb = stride * h;
		size_uv = stride * h;
		start = min(yrgb_addr, uv_addr);
		start >>= PAGE_SHIFT;
		end = max((yrgb_addr + size_yrgb), (uv_addr + size_uv));
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	case RGA_FORMAT_YCbCr_422_P:
	case RGA_FORMAT_YCrCb_422_P:
		stride = (w + 3) & (~3);
		size_yrgb = stride * h;
		size_uv = ((stride >> 1) * h);
		size_v = ((stride >> 1) * h);
		start = min3(yrgb_addr, uv_addr, v_addr);
		start = start >> PAGE_SHIFT;
		end =
			max3((yrgb_addr + size_yrgb), (uv_addr + size_uv),
			(v_addr + size_v));
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	case RGA_FORMAT_YCbCr_420_SP:
	case RGA_FORMAT_YCrCb_420_SP:
		stride = (w + 3) & (~3);
		size_yrgb = stride * h;
		size_uv = (stride * (h >> 1));
		start = min(yrgb_addr, uv_addr);
		start >>= PAGE_SHIFT;
		end = max((yrgb_addr + size_yrgb), (uv_addr + size_uv));
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	case RGA_FORMAT_YCbCr_420_P:
	case RGA_FORMAT_YCrCb_420_P:
		stride = (w + 3) & (~3);
		size_yrgb = stride * h;
		size_uv = ((stride >> 1) * (h >> 1));
		size_v = ((stride >> 1) * (h >> 1));
		start = min3(yrgb_addr, uv_addr, v_addr);
		start >>= PAGE_SHIFT;
		end =
			max3((yrgb_addr + size_yrgb), (uv_addr + size_uv),
			(v_addr + size_v));
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	case RGA_FORMAT_YCbCr_400:
		stride = (w + 3) & (~3);
		size_yrgb = stride * h;
		start = yrgb_addr >> PAGE_SHIFT;
		end = yrgb_addr + size_yrgb;
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	case RGA_FORMAT_Y4:
		stride = ((w + 3) & (~3)) >> 1;
		size_yrgb = stride * h;
		start = yrgb_addr >> PAGE_SHIFT;
		end = yrgb_addr + size_yrgb;
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	case RGA_FORMAT_YVYU_422:
	case RGA_FORMAT_VYUY_422:
	case RGA_FORMAT_YUYV_422:
	case RGA_FORMAT_UYVY_422:
		stride = (w + 3) & (~3);
		size_yrgb = stride * h;
		size_uv = stride * h;
		start = min(yrgb_addr, uv_addr);
		start >>= PAGE_SHIFT;
		end = max((yrgb_addr + size_yrgb), (uv_addr + size_uv));
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	case RGA_FORMAT_YVYU_420:
	case RGA_FORMAT_VYUY_420:
	case RGA_FORMAT_YUYV_420:
	case RGA_FORMAT_UYVY_420:
		stride = (w + 3) & (~3);
		size_yrgb = stride * h;
		size_uv = (stride * (h >> 1));
		start = min(yrgb_addr, uv_addr);
		start >>= PAGE_SHIFT;
		end = max((yrgb_addr + size_yrgb), (uv_addr + size_uv));
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	case RGA_FORMAT_YCbCr_420_SP_10B:
	case RGA_FORMAT_YCrCb_420_SP_10B:
		stride = (w + 3) & (~3);
		size_yrgb = stride * h;
		size_uv = (stride * (h >> 1));
		start = min(yrgb_addr, uv_addr);
		start >>= PAGE_SHIFT;
		end = max((yrgb_addr + size_yrgb), (uv_addr + size_uv));
		end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		pageCount = end - start;
		break;
	default:
		pageCount = 0;
		start = 0;
		break;
	}

	*StartAddr = start;

	if (size != NULL)
		*size = size_yrgb + size_uv + size_v;

	return pageCount;
}

static int rga_MapUserMemory(struct page **pages, uint32_t *pageTable,
			      unsigned long Memory, uint32_t pageCount, int writeFlag,
			      struct mm_struct *mm)
{
	uint32_t i, status;
	int32_t result;
	unsigned long Address;
	unsigned long pfn;
	struct page __maybe_unused *page;
	struct vm_area_struct *vma;
	spinlock_t *ptl;
	pte_t *pte;
	pgd_t *pgd;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	p4d_t *p4d;
#endif
	pud_t *pud;
	pmd_t *pmd;

	status = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	mmap_read_lock(mm);
#else
	down_read(&mm->mmap_sem);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 168) && \
	LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
	result = get_user_pages(current, mm, Memory << PAGE_SHIFT,
		pageCount, writeFlag ? FOLL_WRITE : 0,
		pages, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	result = get_user_pages(current, mm, Memory << PAGE_SHIFT,
		pageCount, writeFlag, 0, pages, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	result = get_user_pages_remote(current, mm,
		Memory << PAGE_SHIFT,
		pageCount, writeFlag, pages, NULL, NULL);
#else
	result = get_user_pages_remote(mm, Memory << PAGE_SHIFT,
		pageCount, writeFlag, pages, NULL, NULL);
#endif

	if (result > 0 && result >= pageCount) {
		/* Fill the page table. */
		for (i = 0; i < pageCount; i++) {
			/* Get the physical address from page struct. */
			pageTable[i] = page_to_phys(pages[i]);
		}

		for (i = 0; i < result; i++)
			put_page(pages[i]);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
		mmap_read_unlock(mm);
#else
		up_read(&mm->mmap_sem);
#endif
		return 0;
	}

	if (result > 0) {
		for (i = 0; i < result; i++)
			put_page(pages[i]);
	}

	for (i = 0; i < pageCount; i++) {
		vma = find_vma(mm, (Memory + i) << PAGE_SHIFT);
		if (!vma) {
			pr_err("failed to get vma, result = %d, pageCount = %d\n",
				result, pageCount);
			status = RGA_OUT_OF_RESOURCES;
			break;
		}

		pgd = pgd_offset(mm, (Memory + i) << PAGE_SHIFT);
		if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd))) {
			pr_err("failed to get pgd, result = %d, pageCount = %d\n",
				result, pageCount);
			status = RGA_OUT_OF_RESOURCES;
			break;
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
		/*
		 * In the four-level page table,
		 * it will do nothing and return pgd.
		 */
		p4d = p4d_offset(pgd, (Memory + i) << PAGE_SHIFT);
		if (p4d_none(*p4d) || unlikely(p4d_bad(*p4d))) {
			pr_err("failed to get p4d, result = %d, pageCount = %d\n",
				result, pageCount);
			status = RGA_OUT_OF_RESOURCES;
			break;
		}

		pud = pud_offset(p4d, (Memory + i) << PAGE_SHIFT);
#else
		pud = pud_offset(pgd, (Memory + i) << PAGE_SHIFT);
#endif

		if (pud_none(*pud) || unlikely(pud_bad(*pud))) {
			pr_err("failed to get pud, result = %d, pageCount = %d\n",
				result, pageCount);
			status = RGA_OUT_OF_RESOURCES;
			break;
		}
		pmd = pmd_offset(pud, (Memory + i) << PAGE_SHIFT);
		if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd))) {
			pr_err("failed to get pmd, result = %d, pageCount = %d\n",
				result, pageCount);
			status = RGA_OUT_OF_RESOURCES;
			break;
		}
		pte = pte_offset_map_lock(mm, pmd,
					 (Memory + i) << PAGE_SHIFT, &ptl);
		if (pte_none(*pte)) {
			pr_err("failed to get pte, result = %d, pageCount = %d\n",
				result, pageCount);
			pte_unmap_unlock(pte, ptl);
			status = RGA_OUT_OF_RESOURCES;
			break;
		}

		pfn = pte_pfn(*pte);
		Address = ((pfn << PAGE_SHIFT) |
			 (((unsigned long)((Memory + i) << PAGE_SHIFT)) &
				~PAGE_MASK));

		pages[i] = pfn_to_page(pfn);
		pageTable[i] = (uint32_t)Address;
		pte_unmap_unlock(pte, ptl);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	mmap_read_unlock(mm);
#else
	up_read(&mm->mmap_sem);
#endif

	return status;
}

static dma_addr_t rga_iommu_dma_alloc_iova(struct iommu_domain *domain,
					    size_t size, u64 dma_limit,
					    struct device *dev)
{
	struct rga_iommu_dma_cookie *cookie = domain->iova_cookie;
	struct iova_domain *iovad = &cookie->iovad;
	unsigned long shift, iova_len, iova = 0;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	dma_addr_t limit;
#endif

	shift = iova_shift(iovad);
	iova_len = size >> shift;
	/*
	 * Freeing non-power-of-two-sized allocations back into the IOVA caches
	 * will come back to bite us badly, so we have to waste a bit of space
	 * rounding up anything cacheable to make sure that can't happen. The
	 * order of the unadjusted size will still match upon freeing.
	 */
	if (iova_len < (1 << (IOVA_RANGE_CACHE_MAX_SIZE - 1)))
		iova_len = roundup_pow_of_two(iova_len);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	dma_limit = min_not_zero(dma_limit, dev->bus_dma_limit);
#else
	if (dev->bus_dma_mask)
		dma_limit &= dev->bus_dma_mask;
#endif

	if (domain->geometry.force_aperture)
		dma_limit = min(dma_limit, (u64)domain->geometry.aperture_end);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	iova = alloc_iova_fast(iovad, iova_len, dma_limit >> shift, true);
#else
	limit = min_t(dma_addr_t, dma_limit >> shift, iovad->end_pfn);

	iova = alloc_iova_fast(iovad, iova_len, limit, true);
#endif

	return (dma_addr_t)iova << shift;
}

static void rga_iommu_dma_free_iova(struct rga_iommu_dma_cookie *cookie,
				     dma_addr_t iova, size_t size)
{
	struct iova_domain *iovad = &cookie->iovad;

	free_iova_fast(iovad, iova_pfn(iovad, iova),
		size >> iova_shift(iovad));
}

static inline size_t rga_iommu_map_sg(struct iommu_domain *domain,
				      unsigned long iova, struct scatterlist *sg,
				      unsigned int nents, int prot)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	return iommu_map_sg_atomic(domain, iova, sg, nents, prot);
#else
	return iommu_map_sg(domain, iova, sg, nents, prot);
#endif
}

static inline bool rga_dev_is_dma_coherent(struct device *dev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	return dev_is_dma_coherent(dev);
#else
	return dev->archdata.dma_coherent;
#endif
}

static inline struct iommu_domain *rga_iommu_get_dma_domain(struct device *dev)
{
	return iommu_get_domain_for_dev(dev);
}

static inline void rga_dma_flush_cache_by_sgt(struct sg_table *sgt)
{
	struct scatterlist *sg;
	int i;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i)
		arch_dma_prep_coherent(sg_page(sg), sg->length);
#else
	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i)
		__dma_flush_area(sg_page(sg), sg->length);
#endif
}

static void rga_viraddr_put_channel_info(struct rga_dma_buffer_t **rga_dma_buffer)
{
	struct rga_dma_buffer_t *buffer;

	buffer = *rga_dma_buffer;
	if (buffer == NULL)
		return;

	if (!buffer->use_viraddr)
		return;

	iommu_unmap(buffer->domain, buffer->iova, buffer->size);
	rga_iommu_dma_free_iova(buffer->cookie, buffer->iova, buffer->size);

	kfree(buffer);

	*rga_dma_buffer = NULL;
}

void rga_iommu_unmap_virt_addr(struct rga_dma_buffer *virt_dma_buf)
{
	if (virt_dma_buf == NULL)
		return;
	if (virt_dma_buf->iova == 0)
		return;

	iommu_unmap(virt_dma_buf->domain, virt_dma_buf->iova, virt_dma_buf->size);
	rga_iommu_dma_free_iova(virt_dma_buf->cookie, virt_dma_buf->iova, virt_dma_buf->size);
}

int rga_iommu_map_virt_addr(struct rga_memory_parm *memory_parm,
			    struct rga_dma_buffer *virt_dma_buf,
			    struct device *rga_dev,
			    struct mm_struct *mm)
{
	unsigned long size;
	size_t map_size;
	bool coherent;
	int ioprot;
	struct iommu_domain *domain = NULL;
	struct rga_iommu_dma_cookie *cookie;
	struct iova_domain *iovad;
	dma_addr_t iova;
	struct sg_table *sgt = NULL;

	coherent = rga_dev_is_dma_coherent(rga_dev);
	domain = rga_iommu_get_dma_domain(rga_dev);
	ioprot = rga_dma_info_to_prot(DMA_BIDIRECTIONAL, coherent);

	cookie = domain->iova_cookie;
	iovad = &cookie->iovad;
	size = iova_align(iovad, virt_dma_buf->size);
	sgt = virt_dma_buf->sgt;
	if (sgt == NULL) {
		pr_err("can not map iommu, because sgt is null!\n");
		return -EFAULT;
	}

	if (DEBUGGER_EN(MSG))
		pr_debug("iova_align size = %ld", size);

	iova = rga_iommu_dma_alloc_iova(domain, size, rga_dev->coherent_dma_mask, rga_dev);
	if (!iova) {
		pr_err("rga_iommu_dma_alloc_iova failed");
		return -ENOMEM;
	}

	if (!(ioprot & IOMMU_CACHE))
		rga_dma_flush_cache_by_sgt(sgt);

	map_size = rga_iommu_map_sg(domain, iova, sgt->sgl, sgt->orig_nents, ioprot);
	if (map_size < size) {
		pr_err("iommu can not map sgt to iova");
		return -EINVAL;
	}

	virt_dma_buf->cookie = cookie;
	virt_dma_buf->domain = domain;
	virt_dma_buf->iova = iova;
	virt_dma_buf->size = size;

	return 0;
}

static int rga_viraddr_get_channel_info(struct rga_img_info_t *channel_info,
					 struct rga_dma_buffer_t **rga_dma_buffer,
					 int writeFlag, int core, struct mm_struct *mm)
{
	struct rga_scheduler_t *scheduler = NULL;
	struct rga_dma_buffer_t *alloc_buffer;

	unsigned long size;
	unsigned long start_addr;
	unsigned int count;
	int pages_order = 0;
	int page_table_order = 0;

	uint32_t *page_table = NULL;
	struct page **pages = NULL;
	struct sg_table sgt;

	int ret = 0;
	size_t map_size = 0;

	struct iommu_domain *domain = NULL;
	struct rga_iommu_dma_cookie *cookie;
	struct iova_domain *iovad;
	bool coherent;
	int ioprot;
	dma_addr_t iova;

	alloc_buffer =
		kmalloc(sizeof(struct rga_dma_buffer_t),
			GFP_KERNEL);
	if (alloc_buffer == NULL) {
		pr_err("rga_dma_buffer alloc error!\n");
		return -ENOMEM;
	}

	scheduler = rga_job_get_scheduler(core);
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__,
			__LINE__);
		ret = -EINVAL;
		goto out_free_buffer;
	}

	coherent = rga_dev_is_dma_coherent(scheduler->dev);
	domain = rga_iommu_get_dma_domain(scheduler->dev);
	ioprot = rga_dma_info_to_prot(DMA_BIDIRECTIONAL, coherent);
	cookie = domain->iova_cookie;
	iovad = &cookie->iovad;

	/* Calculate page size. */
	count = rga_buf_size_cal(channel_info->yrgb_addr, channel_info->uv_addr,
				 channel_info->v_addr, channel_info->format,
				 channel_info->vir_w, channel_info->vir_h,
				 &start_addr, NULL);
	size = count * PAGE_SIZE;

	/* alloc pages and page_table */
	pages_order = get_order(count * sizeof(struct page *));
	pages = (struct page **)__get_free_pages(GFP_KERNEL, pages_order);
	if (pages == NULL) {
		pr_err("Can not alloc pages for pages\n");
		ret = -ENOMEM;
		goto out_free_buffer;
	}

	page_table_order = get_order(count * sizeof(uint32_t *));
	page_table = (uint32_t *)__get_free_pages(GFP_KERNEL, page_table_order);
	if (page_table == NULL) {
		pr_err("Can not alloc pages for page_table\n");
		ret = -ENOMEM;
		goto out_free_pages;
	}

	/* get pages from virtual address. */
	ret = rga_MapUserMemory(pages, page_table, start_addr, count, writeFlag, mm);
	if (ret) {
		pr_err("failed to get pages");
		ret = -EINVAL;
		goto out_free_pages_table;
	}

	size = iova_align(iovad, size);

	if (DEBUGGER_EN(MSG))
		pr_err("iova_align size = %ld", size);

	iova = rga_iommu_dma_alloc_iova(domain, size, scheduler->dev->coherent_dma_mask,
					scheduler->dev);
	if (!iova) {
		pr_err("rga_iommu_dma_alloc_iova failed");
		ret = -ENOMEM;
		goto out_free_pages_table;
	}

	/* get sg form pages. */
	if (sg_alloc_table_from_pages(&sgt, pages, count, 0, size, GFP_KERNEL)) {
		pr_err("sg_alloc_table_from_pages failed");
		ret = -ENOMEM;
		goto out_free_sg;
	}

	if (!(ioprot & IOMMU_CACHE))
		rga_dma_flush_cache_by_sgt(&sgt);

	map_size = rga_iommu_map_sg(domain, iova, sgt.sgl, sgt.orig_nents, ioprot);
	if (map_size < size) {
		pr_err("iommu can not map sgt to iova");
		ret = -EINVAL;
		goto out_free_sg;
	}

	/*
	 * When the virtual address has an in-page offset, it needs to be offset to
	 * the corresponding starting point.
	 */
	channel_info->yrgb_addr = iova + (channel_info->yrgb_addr & (~PAGE_MASK));

	alloc_buffer->iova = iova;
	alloc_buffer->size = size;
	alloc_buffer->cookie = cookie;
	alloc_buffer->use_viraddr = true;
	alloc_buffer->domain = domain;

	sg_free_table(&sgt);

	free_pages((unsigned long)pages, pages_order);
	free_pages((unsigned long)page_table, page_table_order);

	*rga_dma_buffer = alloc_buffer;

	return ret;

out_free_sg:
	sg_free_table(&sgt);
	rga_iommu_dma_free_iova(cookie, iova, size);

out_free_pages_table:
	free_pages((unsigned long)page_table, page_table_order);

out_free_pages:
	free_pages((unsigned long)pages, pages_order);

out_free_buffer:
	kfree(alloc_buffer);

	return ret;
}

static int rga_virtual_memory_check(void *vaddr, u32 w, u32 h, u32 format,
					int fd)
{
	int bits = 32;
	int temp_data = 0;
	void *one_line = NULL;

	bits = rga_get_format_bits(format);
	if (bits < 0)
		return -1;

	one_line = kzalloc(w * 4, GFP_KERNEL);
	if (!one_line) {
		pr_err("kzalloc fail %s[%d]\n", __func__, __LINE__);
		return 0;
	}

	temp_data = w * (h - 1) * bits >> 3;
	if (fd > 0) {
		pr_info("vaddr is%p, bits is %d, fd check\n", vaddr, bits);
		memcpy(one_line, (char *)vaddr + temp_data, w * bits >> 3);
		pr_info("fd check ok\n");
	} else {
		pr_info("vir addr memory check.\n");
		memcpy((void *)((char *)vaddr + temp_data), one_line,
			 w * bits >> 3);
		pr_info("vir addr check ok.\n");
	}

	kfree(one_line);
	return 0;
}

static int rga_dma_memory_check(struct rga_dma_buffer_t *rga_dma_buffer,
				struct rga_img_info_t *img)
{
	int ret = 0;
	void *vaddr;
	struct dma_buf *dma_buf;

	dma_buf = rga_dma_buffer->dma_buf;

	if (!IS_ERR_OR_NULL(dma_buf)) {
		vaddr = dma_buf_vmap(dma_buf);
		if (vaddr) {
			ret = rga_virtual_memory_check(vaddr, img->vir_w,
				img->vir_h, img->format, img->yrgb_addr);
		} else {
			pr_err("can't vmap the dma buffer!\n");
			return -EINVAL;
		}

		dma_buf_vunmap(dma_buf, vaddr);
	}

	return ret;
}

int rga_dma_map_fd(int fd, struct rga_dma_buffer *rga_dma_buffer,
		   enum dma_data_direction dir, struct device *rga_dev)
{
	struct dma_buf *dma_buf = NULL;
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *sgt = NULL;
	int ret = 0;

	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf)) {
		pr_err("dma_buf_get fail fd[%d]\n", fd);
		ret = -EINVAL;
		return ret;
	}

	attach = dma_buf_attach(dma_buf, rga_dev);
	if (IS_ERR(attach)) {
		pr_err("Failed to attach dma_buf\n");
		ret = -EINVAL;
		goto err_get_attach;
	}

	sgt = dma_buf_map_attachment(attach, dir);
	if (IS_ERR(sgt)) {
		pr_err("Failed to map src attachment\n");
		ret = -EINVAL;
		goto err_get_sgt;
	}

	rga_dma_buffer->dma_buf = dma_buf;
	rga_dma_buffer->attach = attach;
	rga_dma_buffer->sgt = sgt;
	rga_dma_buffer->iova = sg_dma_address(sgt->sgl);
	rga_dma_buffer->size = sg_dma_len(sgt->sgl);
	rga_dma_buffer->dir = dir;

	return ret;

err_get_sgt:
	if (attach)
		dma_buf_detach(dma_buf, attach);
err_get_attach:
	if (dma_buf)
		dma_buf_put(dma_buf);

	return ret;
}

void rga_dma_unmap_fd(struct rga_dma_buffer *rga_dma_buffer)
{
	if (rga_dma_buffer->attach && rga_dma_buffer->sgt)
		dma_buf_unmap_attachment(rga_dma_buffer->attach,
					 rga_dma_buffer->sgt,
					 rga_dma_buffer->dir);

	if (rga_dma_buffer->attach) {
		dma_buf_detach(rga_dma_buffer->dma_buf, rga_dma_buffer->attach);
		dma_buf_put(rga_dma_buffer->dma_buf);
	}
}

static int rga_dma_map_buffer(struct dma_buf *dma_buf,
				 struct rga_dma_buffer_t *rga_dma_buffer,
				 enum dma_data_direction dir, struct device *rga_dev)
{
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *sgt = NULL;

	int ret = 0;

	attach = dma_buf_attach(dma_buf, rga_dev);
	if (IS_ERR(attach)) {
		ret = -EINVAL;
		pr_err("Failed to attach dma_buf\n");
		goto err_get_attach;
	}

	sgt = dma_buf_map_attachment(attach, dir);
	if (IS_ERR(sgt)) {
		ret = -EINVAL;
		pr_err("Failed to map src attachment\n");
		goto err_get_sg;
	}

	rga_dma_buffer->dma_buf = dma_buf;
	rga_dma_buffer->attach = attach;
	rga_dma_buffer->sgt = sgt;
	rga_dma_buffer->iova = sg_dma_address(sgt->sgl);

	/* TODO: size for check */
	rga_dma_buffer->size = sg_dma_len(sgt->sgl);
	rga_dma_buffer->dir = dir;

	return ret;

err_get_sg:
	if (sgt)
		dma_buf_unmap_attachment(attach, sgt, dir);
	if (attach)
		dma_buf_detach(dma_buf, attach);
err_get_attach:
	if (dma_buf)
		dma_buf_put(dma_buf);

	return ret;
}

static void rga_dma_unmap_buffer(struct rga_dma_buffer_t *rga_dma_buffer)
{
	if (rga_dma_buffer->attach && rga_dma_buffer->sgt) {
		dma_buf_unmap_attachment(rga_dma_buffer->attach,
			rga_dma_buffer->sgt, rga_dma_buffer->dir);
	}

	if (rga_dma_buffer->attach)
		dma_buf_detach(rga_dma_buffer->dma_buf, rga_dma_buffer->attach);
}

static int rga_dma_buf_get_channel_info(struct rga_img_info_t *channel_info,
			struct rga_dma_buffer_t **rga_dma_buffer, int mmu_flag,
			struct dma_buf **dma_buf, int core)
{
	int ret;
	struct rga_dma_buffer_t *alloc_buffer;
	struct rga_scheduler_t *scheduler = NULL;

	if (unlikely(!mmu_flag && *dma_buf)) {
		pr_err("Fix it please enable mmu on dma buf channel\n");
		return -EINVAL;
	} else if (mmu_flag && *dma_buf) {
		/* perform a single mapping to dma buffer */
		alloc_buffer =
			kmalloc(sizeof(struct rga_dma_buffer_t),
				GFP_KERNEL);
		if (alloc_buffer == NULL) {
			pr_err("rga_dma_buffer alloc error!\n");
			return -ENOMEM;
		}

		alloc_buffer->use_viraddr = false;

		scheduler = rga_job_get_scheduler(core);
		if (scheduler == NULL) {
			pr_err("failed to get scheduler, %s(%d)\n", __func__,
				 __LINE__);
			kfree(alloc_buffer);
			ret = -EINVAL;
			return ret;
		}

		ret =
			rga_dma_map_buffer(*dma_buf, alloc_buffer,
						DMA_BIDIRECTIONAL, scheduler->dev);
		if (ret < 0) {
			pr_err("Can't map dma-buf\n");
			kfree(alloc_buffer);
			return ret;
		}

		*rga_dma_buffer = alloc_buffer;
	}

	if (DEBUGGER_EN(CHECK_MODE)) {
		ret = rga_dma_memory_check(*rga_dma_buffer,
			channel_info);
		if (ret < 0) {
			pr_err("Channel check memory error!\n");
			/*
			 * Note: This error is released by external
			 *	 rga_dma_put_channel_info().
			 */
			return ret;
		}
	}

	/* The value of dma_fd is no longer needed. */
	channel_info->yrgb_addr = 0;

	if (core == RGA3_SCHEDULER_CORE0 || core == RGA3_SCHEDULER_CORE1)
		if (*rga_dma_buffer)
			channel_info->yrgb_addr = (*rga_dma_buffer)->iova;

	return 0;
}

static void rga_dma_put_channel_info(struct rga_dma_buffer_t **rga_dma_buffer, struct dma_buf **dma_buf)
{
	struct rga_dma_buffer_t *buffer;

	buffer = *rga_dma_buffer;
	if (buffer == NULL)
		return;

	if (buffer->use_viraddr)
		return;

	rga_dma_unmap_buffer(buffer);
	if (*dma_buf) {
		dma_buf_put(*dma_buf);
		*dma_buf = NULL;
	}

	kfree(buffer);

	*rga_dma_buffer = NULL;
}

int rga_dma_buf_get(struct rga_job *job)
{
	int ret = -EINVAL;
	int mmu_flag;

	struct rga_img_info_t *src0 = NULL;
	struct rga_img_info_t *src1 = NULL;
	struct rga_img_info_t *dst = NULL;
	struct rga_img_info_t *els = NULL;

	src0 = &job->rga_command_base.src;
	dst = &job->rga_command_base.dst;
	if (job->rga_command_base.render_mode != UPDATE_PALETTE_TABLE_MODE)
		src1 = &job->rga_command_base.pat;
	else
		els = &job->rga_command_base.pat;

	if (likely(src0 != NULL)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 8) & 1);
		if (mmu_flag && src0->yrgb_addr) {
			job->dma_buf_src0 = dma_buf_get(src0->yrgb_addr);
			if (IS_ERR(job->dma_buf_src0)) {
				ret = -EINVAL;
				pr_err("%s src0 dma_buf_get fail fd[%lu]\n",
					__func__, (unsigned long)src0->yrgb_addr);
				return ret;
			}
		}
	}

	if (likely(dst != NULL)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 10) & 1);
		if (mmu_flag && dst->yrgb_addr) {
			job->dma_buf_dst = dma_buf_get(dst->yrgb_addr);
			if (IS_ERR(job->dma_buf_dst)) {
				ret = -EINVAL;
				pr_err("%s dst dma_buf_get fail fd[%lu]\n",
					__func__, (unsigned long)dst->yrgb_addr);
				return ret;
			}
		}
	}

	if (src1 != NULL) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 9) & 1);
		if (mmu_flag && src1->yrgb_addr) {
			job->dma_buf_src1 = dma_buf_get(src1->yrgb_addr);
			if (IS_ERR(job->dma_buf_src0)) {
				ret = -EINVAL;
				pr_err("%s src1 dma_buf_get fail fd[%lu]\n",
					__func__, (unsigned long)src1->yrgb_addr);
				return ret;
			}
		}
	}

	if (els != NULL) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 11) & 1);
		if (mmu_flag && els->yrgb_addr) {
			job->dma_buf_els = dma_buf_get(els->yrgb_addr);
			if (IS_ERR(job->dma_buf_els)) {
				ret = -EINVAL;
				pr_err("%s els dma_buf_get fail fd[%lu]\n",
					__func__, (unsigned long)els->yrgb_addr);
				return ret;
			}
		}
	}

	return 0;
}

void rga_get_dma_buf(struct rga_job *job)
{
	int mmu_flag;

	mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 8) & 1);
	if (mmu_flag && job->dma_buf_src0)
		get_dma_buf(job->dma_buf_src0);

	mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 10) & 1);
	if (mmu_flag && job->dma_buf_dst)
		get_dma_buf(job->dma_buf_dst);

	mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 9) & 1);
	if (mmu_flag && job->dma_buf_src1)
		get_dma_buf(job->dma_buf_src1);

	mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 11) & 1);
	if (mmu_flag && job->dma_buf_els)
		get_dma_buf(job->dma_buf_els);
}

int rga_dma_get_info(struct rga_job *job)
{
	int ret = 0;
	uint32_t mmu_flag;
	struct rga_img_info_t *src0 = NULL;
	struct rga_img_info_t *src1 = NULL;
	struct rga_img_info_t *dst = NULL;
	struct rga_img_info_t *els = NULL;

	src0 = &job->rga_command_base.src;
	dst = &job->rga_command_base.dst;
	if (job->rga_command_base.render_mode != UPDATE_PALETTE_TABLE_MODE)
		src1 = &job->rga_command_base.pat;
	else
		els = &job->rga_command_base.pat;

	/* src0 channel */
	if (likely(src0 != NULL)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 8) & 1);
		if (job->dma_buf_src0 != NULL) {
			ret = rga_dma_buf_get_channel_info(src0,
				&job->rga_dma_buffer_src0, mmu_flag,
				&job->dma_buf_src0, job->core);

			if (unlikely(ret < 0)) {
				pr_err("src0 channel get info error!\n");
				goto src0_channel_err;
			}
		} else {
			src0->yrgb_addr = src0->uv_addr;
			rga_convert_addr(src0, true);
			if (job->core == RGA3_SCHEDULER_CORE0 || job->core == RGA3_SCHEDULER_CORE1) {
				if (src0->yrgb_addr > 0 && mmu_flag) {
					ret = rga_viraddr_get_channel_info(src0, &job->rga_dma_buffer_src0,
						0, job->core, job->mm);

					if (unlikely(ret < 0)) {
						pr_err("src0 channel viraddr get info error!\n");
						return ret;
					}
				}
			}
		}

		rga_convert_addr(src0, false);
	}

	/* dst channel */
	if (likely(dst != NULL)) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 10) & 1);
		if (job->dma_buf_dst != NULL) {
			ret = rga_dma_buf_get_channel_info(dst,
				&job->rga_dma_buffer_dst, mmu_flag,
				&job->dma_buf_dst, job->core);

			if (unlikely(ret < 0)) {
				pr_err("dst channel get info error!\n");
				goto dst_channel_err;
			}
		} else {
			dst->yrgb_addr = dst->uv_addr;
			rga_convert_addr(dst, true);
			if (job->core == RGA3_SCHEDULER_CORE0 || job->core == RGA3_SCHEDULER_CORE1) {
				if (dst->yrgb_addr > 0 && mmu_flag) {
					ret = rga_viraddr_get_channel_info(dst, &job->rga_dma_buffer_dst,
						1, job->core, job->mm);

					if (unlikely(ret < 0)) {
						pr_err("dst channel viraddr get info error!\n");
						return ret;
					}
				}
			}
		}

		rga_convert_addr(dst, false);
	}

	/* src1 channel */
	if (src1 != NULL) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 9) & 1);
		if (job->dma_buf_src1 != NULL) {
			ret = rga_dma_buf_get_channel_info(src1,
				&job->rga_dma_buffer_src1, mmu_flag,
				&job->dma_buf_src1, job->core);

			if (unlikely(ret < 0)) {
				pr_err("src1 channel get info error!\n");
				goto src1_channel_err;
			}
		} else {
			src1->yrgb_addr = src1->uv_addr;
			rga_convert_addr(src1, true);
			if (job->core == RGA3_SCHEDULER_CORE0 || job->core == RGA3_SCHEDULER_CORE1) {
				if (src1->yrgb_addr > 0 && mmu_flag) {
					ret = rga_viraddr_get_channel_info(src1, &job->rga_dma_buffer_src1,
						0, job->core, job->mm);

					if (unlikely(ret < 0)) {
						pr_err("src1 channel viraddr get info error!\n");
						return ret;
					}
				}
			}
		}

		rga_convert_addr(src1, false);
	}

	/* els channel */
	if (els != NULL) {
		mmu_flag = ((job->rga_command_base.mmu_info.mmu_flag >> 11) & 1);
		if (job->dma_buf_els != NULL) {
			ret = rga_dma_buf_get_channel_info(els,
				&job->rga_dma_buffer_els, mmu_flag,
				&job->dma_buf_els, job->core);

			if (unlikely(ret < 0)) {
				pr_err("els channel get info error!\n");
				goto els_channel_err;
			}
		} else {
			els->yrgb_addr = els->uv_addr;
			rga_convert_addr(els, true);
			if (job->core == RGA3_SCHEDULER_CORE0 || job->core == RGA3_SCHEDULER_CORE1) {
				if (els->yrgb_addr > 0 && mmu_flag) {
					ret = rga_viraddr_get_channel_info(els, &job->rga_dma_buffer_els,
						0, job->core, job->mm);

					if (unlikely(ret < 0)) {
						pr_err("els channel viraddr get info error!\n");
						return ret;
					}
				}
			}
		}

		rga_convert_addr(els, false);
	}

	return 0;

els_channel_err:
	rga_dma_put_channel_info(&job->rga_dma_buffer_els, &job->dma_buf_els);
dst_channel_err:
	rga_dma_put_channel_info(&job->rga_dma_buffer_dst, &job->dma_buf_dst);
src1_channel_err:
	rga_dma_put_channel_info(&job->rga_dma_buffer_src1, &job->dma_buf_src1);
src0_channel_err:
	rga_dma_put_channel_info(&job->rga_dma_buffer_src0, &job->dma_buf_src0);

	return ret;
}

void rga_dma_put_info(struct rga_job *job)
{
	rga_dma_put_channel_info(&job->rga_dma_buffer_src0, &job->dma_buf_src0);
	rga_viraddr_put_channel_info(&job->rga_dma_buffer_src0);
	rga_dma_put_channel_info(&job->rga_dma_buffer_src1, &job->dma_buf_src1);
	rga_viraddr_put_channel_info(&job->rga_dma_buffer_src1);
	rga_dma_put_channel_info(&job->rga_dma_buffer_dst, &job->dma_buf_dst);
	rga_viraddr_put_channel_info(&job->rga_dma_buffer_dst);
	rga_dma_put_channel_info(&job->rga_dma_buffer_els, &job->dma_buf_els);
	rga_viraddr_put_channel_info(&job->rga_dma_buffer_els);
}
