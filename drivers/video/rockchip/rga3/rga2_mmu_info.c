// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rga2_mmu: " fmt

#include "rga2_mmu_info.h"
#include "rga_dma_buf.h"
#include "rga_mm.h"
#include "rga_job.h"
#include "rga_common.h"

extern struct rga2_mmu_info_t rga2_mmu_info;

#define KERNEL_SPACE_VALID	0xc0000000

#define V7_VATOPA_SUCESS_MASK	(0x1)
#define V7_VATOPA_GET_PADDR(X)	(X & 0xFFFFF000)
#define V7_VATOPA_GET_INER(X)		((X>>4) & 7)
#define V7_VATOPA_GET_OUTER(X)		((X>>2) & 3)
#define V7_VATOPA_GET_SH(X)		((X>>7) & 1)
#define V7_VATOPA_GET_NS(X)		((X>>9) & 1)
#define V7_VATOPA_GET_SS(X)		((X>>1) & 1)

static void rga2_dma_sync_flush_range(void *pstart, void *pend, struct rga_scheduler_t *scheduler)
{
	dma_sync_single_for_device(scheduler->dev, virt_to_phys(pstart),
				 pend - pstart, DMA_TO_DEVICE);
}

static dma_addr_t rga2_dma_map_flush_page(struct page *page, int map, struct rga_scheduler_t *scheduler)
{
	dma_addr_t paddr = 0;

	/*
	 * Through dma_map_page to ensure that the physical address
	 * will not exceed the addressing range of dma.
	 */

	if (map & MMU_MAP_MASK) {
		switch (map) {
		case MMU_MAP_CLEAN:
			paddr = dma_map_page(scheduler->dev, page, 0,
						 PAGE_SIZE, DMA_TO_DEVICE);
			break;
		case MMU_MAP_INVALID:
			paddr = dma_map_page(scheduler->dev, page, 0,
						 PAGE_SIZE, DMA_FROM_DEVICE);
			break;
		case MMU_MAP_CLEAN | MMU_MAP_INVALID:
			paddr = dma_map_page(scheduler->dev, page, 0,
						 PAGE_SIZE, DMA_BIDIRECTIONAL);
			break;
		default:
			paddr = 0;
			pr_err("unknown map cmd 0x%x\n", map);
			break;
		}

		return paddr;
	} else if (map & MMU_UNMAP_MASK) {
		paddr = page_to_phys(page);

		switch (map) {
		case MMU_UNMAP_CLEAN:
			dma_unmap_page(scheduler->dev, paddr,
					 PAGE_SIZE, DMA_TO_DEVICE);
			break;
		case MMU_UNMAP_INVALID:
			dma_unmap_page(scheduler->dev, paddr,
					 PAGE_SIZE, DMA_FROM_DEVICE);
			break;
		case MMU_UNMAP_CLEAN | MMU_UNMAP_INVALID:
			dma_unmap_page(scheduler->dev, paddr,
					 PAGE_SIZE, DMA_BIDIRECTIONAL);
			break;
		default:
			paddr = 0;
			pr_err("unknown map cmd 0x%x\n", map);
			break;
		}

		return paddr;
	}

	pr_err("failed to flush page, map= %x\n", map);
	return 0;
}

void rga2_dma_flush_cache_for_virtual_address(struct rga2_mmu_other_t *reg,
		struct rga_scheduler_t *scheduler)
{
	int i;

	if (reg->MMU_src0_base != NULL) {
		for (i = 0; i < reg->MMU_src0_count; i++)
			rga2_dma_map_flush_page(phys_to_page(reg->MMU_src0_base[i]),
						MMU_UNMAP_CLEAN, scheduler);
	}

	if (reg->MMU_src1_base != NULL) {
		for (i = 0; i < reg->MMU_src1_count; i++)
			rga2_dma_map_flush_page(phys_to_page(reg->MMU_src1_base[i]),
						MMU_UNMAP_CLEAN, scheduler);
	}

	if (reg->MMU_dst_base != NULL) {
		for (i = 0; i < reg->MMU_dst_count; i++)
			rga2_dma_map_flush_page(phys_to_page(reg->MMU_dst_base[i]),
						MMU_UNMAP_INVALID, scheduler);
	}
}

static int rga2_mmu_buf_get(struct rga2_mmu_info_t *t, uint32_t size)
{
	mutex_lock(&rga_drvdata->lock);
	t->front += size;
	mutex_unlock(&rga_drvdata->lock);

	return 0;
}

static int rga2_mmu_buf_get_try(struct rga2_mmu_info_t *t, uint32_t size)
{
	int ret = 0;

	mutex_lock(&rga_drvdata->lock);
	if ((t->back - t->front) > t->size) {
		if (t->front + size > t->back - t->size) {
			pr_info("front %d, back %d dsize %d size %d",
				t->front, t->back, t->size, size);
			ret = -ENOMEM;
			goto out;
		}
	} else {
		if ((t->front + size) > t->back) {
			pr_info("front %d, back %d dsize %d size %d",
				t->front, t->back, t->size, size);
			ret = -ENOMEM;
			goto out;
		}

		if (t->front + size > t->size) {
			if (size > (t->back - t->size)) {
				pr_info("front %d, back %d dsize %d size %d",
					t->front, t->back, t->size, size);
				ret = -ENOMEM;
				goto out;
			}
			t->front = 0;
		}
	}
out:
	mutex_unlock(&rga_drvdata->lock);
	return ret;
}

static int rga2_mem_size_cal(unsigned long Mem, uint32_t MemSize,
				 unsigned long *StartAddr)
{
	unsigned long start, end;
	uint32_t pageCount;

	end = (Mem + (MemSize + PAGE_SIZE - 1)) >> PAGE_SHIFT;
	start = Mem >> PAGE_SHIFT;
	pageCount = end - start;
	*StartAddr = start;
	return pageCount;
}

static int rga2_user_memory_check(struct page **pages, u32 w, u32 h, u32 format,
				 int flag)
{
	int bits;
	void *vaddr = NULL;
	int taipage_num;
	int taidata_num;
	int *tai_vaddr = NULL;

	bits = rga_get_format_bits(format);
	if (bits < 0)
		return -1;

	taipage_num = w * h * bits / 8 / (1024 * 4);
	taidata_num = w * h * bits / 8 % (1024 * 4);
	if (taidata_num == 0) {
		vaddr = kmap(pages[taipage_num - 1]);
		tai_vaddr = (int *)vaddr + 1023;
	} else {
		vaddr = kmap(pages[taipage_num]);
		tai_vaddr = (int *)vaddr + taidata_num / 4 - 1;
	}

	if (flag == 1) {
		pr_info("src user memory check\n");
		pr_info("tai data is %d\n", *tai_vaddr);
	} else {
		pr_info("dst user memory check\n");
		pr_info("tai data is %d\n", *tai_vaddr);
	}

	if (taidata_num == 0)
		kunmap(pages[taipage_num - 1]);
	else
		kunmap(pages[taipage_num]);

	return 0;
}

static int rga2_MapUserMemory(struct page **pages, uint32_t *pageTable,
				 unsigned long Memory, uint32_t pageCount,
				 int writeFlag, int map, struct rga_scheduler_t *scheduler,
				 struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	int32_t result;
	uint32_t i;
	uint32_t status;
	unsigned long Address;
	unsigned long pfn;
	struct page __maybe_unused *page;
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
			pageTable[i] = rga2_dma_map_flush_page(pages[i], map, scheduler);
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

		pageTable[i] = rga2_dma_map_flush_page(phys_to_page(Address), map, scheduler);

		pte_unmap_unlock(pte, ptl);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	mmap_read_unlock(mm);
#else
	up_read(&mm->mmap_sem);
#endif

	return status;
}

static int rga2_MapION(struct sg_table *sg,
			 uint32_t *Memory, int32_t pageCount)
{
	uint32_t i;
	unsigned long Address;
	uint32_t mapped_size = 0;
	uint32_t len;
	struct scatterlist *sgl = sg->sgl;
	uint32_t sg_num = 0;
	uint32_t break_flag = 0;

	do {
		len = sg_dma_len(sgl) >> PAGE_SHIFT;

		/*
		 * The fd passed by user space gets sg through
		 * dma_buf_map_attachment,
		 * so dma_address can be use here.
		 */
		Address = sg_dma_address(sgl);

		for (i = 0; i < len; i++) {
			if (mapped_size + i >= pageCount) {
				break_flag = 1;
				break;
			}
			Memory[mapped_size + i] =
				(uint32_t) (Address + (i << PAGE_SHIFT));
		}
		if (break_flag)
			break;
		mapped_size += len;
		sg_num += 1;
	} while ((sgl = sg_next(sgl)) && (mapped_size < pageCount)
		 && (sg_num < sg->nents));

	return 0;
}

static int rga2_mmu_flush_cache(struct rga2_mmu_other_t *reg,
				struct rga2_req *req, struct rga_job *job)
{
	int DstMemSize;
	unsigned long DstStart, DstPageCount;
	uint32_t *MMU_Base;
	int ret;
	int status;
	struct page **pages = NULL;
	struct rga_scheduler_t *scheduler = NULL;

	MMU_Base = NULL;
	DstPageCount = 0;
	DstStart = 0;

	scheduler = rga_job_get_scheduler(job->core);
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__,
				__LINE__);
		ret = -EINVAL;
		return ret;
	}

	if (reg->MMU_map != true) {
		status = -EINVAL;
		goto out;
	}

	/* cal dst buf mmu info */
	if (req->mmu_info.dst_mmu_flag & 1) {
		DstPageCount = rga_buf_size_cal(req->dst.yrgb_addr,
						 req->dst.uv_addr,
						 req->dst.v_addr,
						 req->dst.format,
						 req->dst.vir_w,
						 req->dst.vir_h, &DstStart, NULL);
		if (DstPageCount == 0)
			return -EINVAL;
	}
	/* Cal out the needed mem size */
	DstMemSize = (DstPageCount + 15) & (~15);

	if (rga2_mmu_buf_get_try(&rga2_mmu_info, DstMemSize)) {
		pr_err("Get MMU mem failed\n");
		status = RGA_MALLOC_ERROR;
		goto out;
	}
	pages = rga2_mmu_info.pages;
	mutex_lock(&rga_drvdata->lock);
	MMU_Base = rga2_mmu_info.buf_virtual +
		(rga2_mmu_info.front & (rga2_mmu_info.size - 1));

	mutex_unlock(&rga_drvdata->lock);
	if (DstMemSize) {
		if (job->rga_dma_buffer_dst) {
			status = -EINVAL;
			goto out;
		} else {
			ret = rga2_MapUserMemory(&pages[0],
						 MMU_Base,
						 DstStart, DstPageCount, 1,
						 MMU_MAP_CLEAN |
						 MMU_MAP_INVALID, scheduler, job->mm);
			if (DEBUGGER_EN(CHECK_MODE))
				rga2_user_memory_check(&pages[0],
							 req->dst.vir_w,
							 req->dst.vir_h,
							 req->dst.format, 2);
		}
		if (ret < 0) {
			pr_err("rga2 unmap dst memory failed\n");
			status = ret;
			goto out;
		}
	}
	rga2_mmu_buf_get(&rga2_mmu_info, DstMemSize);
	reg->MMU_len = DstMemSize;
	status = 0;
out:
	return status;
}

static int rga2_sgt_to_page_table(struct sg_table *sg,
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
		len = sg_dma_len(sgl) >> PAGE_SHIFT;
		if (len == 0)
			len = sgl->length >> PAGE_SHIFT;

		if (use_dma_address)
			/*
			 * The fd passed by user space gets sg through
			 * dma_buf_map_attachment,
			 * so dma_address can be use here.
			 */
			Address = sg_dma_address(sgl);
		else
			Address = sg_phys(sgl);

		for (i = 0; i < len; i++) {
			if (mapped_size + i >= pageCount) {
				break_flag = 1;
				break;
			}
			page_table[mapped_size + i] =
				(uint32_t) (Address + (i << PAGE_SHIFT));
		}
		if (break_flag)
			break;
		mapped_size += len;
		sg_num += 1;
	} while ((sgl = sg_next(sgl)) && (mapped_size < pageCount)
		 && (sg_num < sg->nents));

	return 0;
}

static int rga2_mmu_set_channel_internal(struct rga_scheduler_t *scheduler,
					 struct rga_internal_buffer *internal_buffer,
					 uint32_t *mmu_base,
					 unsigned long page_count,
					 uint32_t **virt_flush_base,
					 uint32_t *virt_flush_count,
					 int map_flag)
{
	struct sg_table *sgt = NULL;

	sgt = rga_mm_lookup_sgt(internal_buffer, scheduler->core);
	if (sgt == NULL) {
		pr_err("rga2 cannot get sgt from handle!\n");
		return -EINVAL;
	}

	if (internal_buffer->type == RGA_VIRTUAL_ADDRESS) {
		rga2_sgt_to_page_table(sgt, mmu_base, page_count, false);
	} else {
		page_count = (page_count + 15) & (~15);
		rga2_sgt_to_page_table(sgt, mmu_base, page_count, true);
	}

	return page_count;
}

static int rga2_mmu_info_BitBlt_mode(struct rga2_mmu_other_t *reg,
			struct rga2_req *req, struct rga_job *job)
{
	int Src0MemSize, DstMemSize, Src1MemSize;
	unsigned long Src0Start, Src1Start, DstStart;
	unsigned long Src0PageCount, Src1PageCount, DstPageCount;
	uint32_t AllSize;
	uint32_t *MMU_Base, *MMU_Base_phys;
	int ret;
	int status;
	int map_flag;
	uint32_t uv_size, v_size;
	struct page **pages = NULL;

	struct rga_scheduler_t *scheduler = NULL;

	scheduler = rga_job_get_scheduler(job->core);
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__,
				__LINE__);
		ret = -EINVAL;
		return ret;
	}

	MMU_Base = NULL;
	Src0PageCount = 0;
	Src1PageCount = 0;
	DstPageCount = 0;
	Src0Start = 0;
	Src1Start = 0;
	DstStart = 0;

	/* cal src0 buf mmu info */
	if (req->mmu_info.src0_mmu_flag & 1) {
		Src0PageCount = rga_buf_size_cal(req->src.yrgb_addr,
						 req->src.uv_addr,
						 req->src.v_addr,
						 req->src.format,
						 req->src.vir_w,
						 (req->src.vir_h), &Src0Start, NULL);
		if (Src0PageCount == 0)
			return -EINVAL;
	}
	/* cal src1 buf mmu info */
	if (req->mmu_info.src1_mmu_flag & 1) {
		Src1PageCount = rga_buf_size_cal(req->src1.yrgb_addr,
						 req->src1.uv_addr,
						 req->src1.v_addr,
						 req->src1.format,
						 req->src1.vir_w,
						 (req->src1.vir_h),
						 &Src1Start, NULL);
		if (Src1PageCount == 0)
			return -EINVAL;
	}
	/* cal dst buf mmu info */
	if (req->mmu_info.dst_mmu_flag & 1) {
		DstPageCount = rga_buf_size_cal(req->dst.yrgb_addr,
						 req->dst.uv_addr,
						 req->dst.v_addr,
						 req->dst.format,
						 req->dst.vir_w,
						 req->dst.vir_h, &DstStart, NULL);
		if (DstPageCount == 0)
			return -EINVAL;
	}
	/* Cal out the needed mem size */
	Src0MemSize = (Src0PageCount + 15) & (~15);
	Src1MemSize = (Src1PageCount + 15) & (~15);
	DstMemSize = (DstPageCount + 15) & (~15);
	AllSize = Src0MemSize + Src1MemSize + DstMemSize;

	if (rga2_mmu_buf_get_try(&rga2_mmu_info, AllSize)) {
		pr_err("Get MMU mem failed\n");
		status = RGA_MALLOC_ERROR;
		goto out;
	}

	pages = rga2_mmu_info.pages;
	if (pages == NULL) {
		pr_err("MMU malloc pages mem failed\n");
		return -EINVAL;
	}

	mutex_lock(&rga_drvdata->lock);

	MMU_Base = rga2_mmu_info.buf_virtual + rga2_mmu_info.front;
	MMU_Base_phys = rga2_mmu_info.buf + rga2_mmu_info.front;

	mutex_unlock(&rga_drvdata->lock);

	if (Src0MemSize) {
		if (job->src_buffer) {
			ret = rga2_mmu_set_channel_internal(scheduler,
							    job->src_buffer,
							    MMU_Base,
							    Src0PageCount,
							    &reg->MMU_src0_base,
							    &reg->MMU_src0_count,
							    MMU_MAP_CLEAN);
			if (ret < 0) {
				pr_err("src0 channel set mmu base error!\n");
				return ret;
			}
		} else {
			if (job->rga_dma_buffer_src0) {
				ret = rga2_MapION(job->rga_dma_buffer_src0->sgt,
						  &MMU_Base[0], Src0MemSize);
			} else {
				ret = rga2_MapUserMemory(&pages[0], &MMU_Base[0],
							 Src0Start, Src0PageCount,
							 0, MMU_MAP_CLEAN, scheduler, job->mm);
				if (DEBUGGER_EN(CHECK_MODE))
					/* TODO: */
					rga2_user_memory_check(&pages[0],
							       req->src.vir_w,
							       req->src.vir_h,
							       req->src.format, 1);
				/* Save pagetable to unmap. */
				reg->MMU_src0_base = MMU_Base;
				reg->MMU_src0_count = Src0PageCount;
			}
		}

		if (ret < 0) {
			pr_err("rga2 map src0 memory failed\n");
			status = ret;
			goto out;
		}
		/* change the buf address in req struct */
		req->mmu_info.src0_base_addr = (((unsigned long)MMU_Base_phys));
		uv_size = (req->src.uv_addr
			 - (Src0Start << PAGE_SHIFT)) >> PAGE_SHIFT;
		v_size = (req->src.v_addr
			 - (Src0Start << PAGE_SHIFT)) >> PAGE_SHIFT;

		req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK));
		req->src.uv_addr = (req->src.uv_addr & (~PAGE_MASK)) |
			(uv_size << PAGE_SHIFT);
		req->src.v_addr = (req->src.v_addr & (~PAGE_MASK)) |
			(v_size << PAGE_SHIFT);
	}

	if (Src1MemSize) {
		if (job->src1_buffer) {
			ret = rga2_mmu_set_channel_internal(scheduler,
							    job->src1_buffer,
							    MMU_Base + Src0MemSize,
							    Src1PageCount,
							    &reg->MMU_src1_base,
							    &reg->MMU_src1_count,
							    MMU_MAP_CLEAN);
			if (ret < 0) {
				pr_err("src1 channel set mmu base error!\n");
				return ret;
			}
		} else {
			if (job->rga_dma_buffer_src1) {
				ret = rga2_MapION(job->rga_dma_buffer_src1->sgt,
						  MMU_Base + Src0MemSize, Src1MemSize);
			} else {
				ret = rga2_MapUserMemory(&pages[0],
							 MMU_Base + Src0MemSize,
							 Src1Start, Src1PageCount,
							 0, MMU_MAP_CLEAN, scheduler, job->mm);

				/* Save pagetable to unmap. */
				reg->MMU_src1_base = MMU_Base + Src0MemSize;
				reg->MMU_src1_count = Src1PageCount;
			}
		}

		if (ret < 0) {
			pr_err("rga2 map src1 memory failed\n");
			status = ret;
			goto out;
		}
		/* change the buf address in req struct */
		req->mmu_info.src1_base_addr = ((unsigned long)(MMU_Base_phys
								+ Src0MemSize));
		req->src1.yrgb_addr = (req->src1.yrgb_addr & (~PAGE_MASK));
	}

	if (DstMemSize) {
		if (req->alpha_mode_0 != 0 && req->bitblt_mode == 0)
			/*
			 * The blend mode of src + dst => dst
			 * requires clean and invalidate
			 */
			map_flag = MMU_MAP_CLEAN | MMU_MAP_INVALID;
		else
			map_flag = MMU_MAP_INVALID;

		if (job->dst_buffer) {
			ret = rga2_mmu_set_channel_internal(scheduler,
							    job->dst_buffer,
							    MMU_Base + Src0MemSize + Src1MemSize,
							    DstPageCount,
							    &reg->MMU_dst_base,
							    &reg->MMU_dst_count,
							    map_flag);
			if (ret < 0) {
				pr_err("dst channel set mmu base error!\n");
				return ret;
			}
		} else {
			if (job->rga_dma_buffer_dst) {
				ret = rga2_MapION(job->rga_dma_buffer_dst->sgt,
						  MMU_Base + Src0MemSize + Src1MemSize,
						  DstMemSize);
			} else {
				ret = rga2_MapUserMemory(&pages[0],
							 MMU_Base + Src0MemSize + Src1MemSize,
							 DstStart, DstPageCount,
							 1, map_flag,
							 scheduler, job->mm);

				if (DEBUGGER_EN(CHECK_MODE))
					rga2_user_memory_check(&pages[0],
							       req->dst.vir_w,
							       req->dst.vir_h,
							       req->dst.format, 2);

				/* Save pagetable to invalid cache and unmap. */
				reg->MMU_dst_base = MMU_Base + Src0MemSize + Src1MemSize;
				reg->MMU_dst_count = DstPageCount;
			}
		}

		if (ret < 0) {
			pr_err("rga2 map dst memory failed\n");
			status = ret;
			goto out;
		}

		/* change the buf address in req struct */
		req->mmu_info.dst_base_addr = ((unsigned long)(MMU_Base_phys
								 + Src0MemSize +
								 Src1MemSize));
		req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK));
		uv_size = (req->dst.uv_addr
			 - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;
		v_size = (req->dst.v_addr
			 - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;
		req->dst.uv_addr = (req->dst.uv_addr & (~PAGE_MASK)) |
			((uv_size) << PAGE_SHIFT);
		req->dst.v_addr = (req->dst.v_addr & (~PAGE_MASK)) |
			((v_size) << PAGE_SHIFT);

		if (((req->alpha_rop_flag & 1) == 1) &&
			(req->bitblt_mode == 0)) {
			req->mmu_info.src1_base_addr =
				req->mmu_info.dst_base_addr;
			req->mmu_info.src1_mmu_flag =
				req->mmu_info.dst_mmu_flag;
		}
	}
	/* flush data to DDR */
	rga2_dma_sync_flush_range(MMU_Base, (MMU_Base + AllSize), scheduler);
	rga2_mmu_buf_get(&rga2_mmu_info, AllSize);
	reg->MMU_len = AllSize;
	status = 0;
out:
	return status;
}

static int rga2_mmu_info_color_palette_mode(struct rga2_mmu_other_t *reg,
						struct rga2_req *req,
						struct rga_job *job)
{
	int SrcMemSize, DstMemSize;
	unsigned long SrcStart, DstStart;
	unsigned long SrcPageCount, DstPageCount;
	struct page **pages = NULL;
	uint32_t uv_size, v_size;
	uint32_t AllSize;
	uint32_t *MMU_Base = NULL, *MMU_Base_phys;
	int ret;
	uint32_t stride;

	uint8_t shift;
	uint32_t sw, byte_num;

	struct rga_scheduler_t *scheduler = NULL;

	if (job->flags & RGA_JOB_USE_HANDLE) {
		pr_err("color palette mode can not support handle.\n");
		return -EINVAL;
	}

	scheduler = rga_job_get_scheduler(job->core);
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__,
				__LINE__);
		ret = -EINVAL;
		return ret;
	}

	shift = 3 - (req->palette_mode & 3);
	sw = req->src.vir_w * req->src.vir_h;
	byte_num = sw >> shift;
	stride = (byte_num + 3) & (~3);

	SrcStart = 0;
	DstStart = 0;
	SrcPageCount = 0;
	DstPageCount = 0;

	do {
		if (req->mmu_info.src0_mmu_flag) {
			if (req->mmu_info.els_mmu_flag & 1) {
				req->mmu_info.src0_mmu_flag = 0;
				req->mmu_info.src1_mmu_flag = 0;
			} else {
				req->mmu_info.els_mmu_flag =
					req->mmu_info.src0_mmu_flag;
				req->mmu_info.src0_mmu_flag = 0;
			}

			SrcPageCount =
				rga2_mem_size_cal(req->src.yrgb_addr, stride,
						 &SrcStart);
			if (SrcPageCount == 0)
				return -EINVAL;
		}

		if (req->mmu_info.dst_mmu_flag) {
			DstPageCount =
				rga_buf_size_cal(req->dst.yrgb_addr,
					req->dst.uv_addr, req->dst.v_addr,
					req->dst.format, req->dst.vir_w,
					req->dst.vir_h, &DstStart, NULL);
			if (DstPageCount == 0)
				return -EINVAL;
		}

		SrcMemSize = (SrcPageCount + 15) & (~15);
		DstMemSize = (DstPageCount + 15) & (~15);

		AllSize = SrcMemSize + DstMemSize;

		if (rga2_mmu_buf_get_try(&rga2_mmu_info, AllSize)) {
			pr_err("Get MMU mem failed\n");
			break;
		}

		pages = rga2_mmu_info.pages;
		if (pages == NULL) {
			pr_err("MMU malloc pages mem failed\n");
			return -EINVAL;
		}

		mutex_lock(&rga_drvdata->lock);
		MMU_Base = rga2_mmu_info.buf_virtual + rga2_mmu_info.front;
		MMU_Base_phys = rga2_mmu_info.buf + rga2_mmu_info.front;
		mutex_unlock(&rga_drvdata->lock);

		if (SrcMemSize) {
			if (job->rga_dma_buffer_src0) {
				ret = rga2_MapION(job->rga_dma_buffer_src0->sgt,
						 &MMU_Base[0], SrcMemSize);
			} else {
				ret = rga2_MapUserMemory(&pages[0],
					&MMU_Base[0], SrcStart, SrcPageCount,
					0, MMU_MAP_CLEAN, scheduler, job->mm);

				if (DEBUGGER_EN(CHECK_MODE))
					rga2_user_memory_check(&pages[0],
						req->src.vir_w, req->src.vir_h,
						req->src.format, 1);
			}

			if (ret < 0) {
				pr_err("rga2 map src0 memory failed\n");
				break;
			}

			/* change the buf address in req struct */
			req->mmu_info.els_base_addr =
				(((unsigned long)MMU_Base_phys));
			/*
			 * The color palette mode will not have
			 * YUV format as input,
			 * so UV component address is not needed
			 */
			req->src.yrgb_addr =
				(req->src.yrgb_addr & (~PAGE_MASK));
		}

		if (DstMemSize) {
			if (job->rga_dma_buffer_dst) {
				ret = rga2_MapION(job->rga_dma_buffer_dst->sgt,
						 MMU_Base + SrcMemSize,
						 DstMemSize);
			} else {
				ret =
					rga2_MapUserMemory(&pages[0],
							 MMU_Base + SrcMemSize,
							 DstStart, DstPageCount,
							 1, MMU_MAP_INVALID, scheduler, job->mm);

				if (DEBUGGER_EN(CHECK_MODE))
					rga2_user_memory_check(&pages[0],
						req->dst.vir_w, req->dst.vir_h,
						req->dst.format, 1);
			}

			if (ret < 0) {
				pr_err("rga2 map dst memory failed\n");
				break;
			}

			/* change the buf address in req struct */
			req->mmu_info.dst_base_addr =
				((unsigned long)(MMU_Base_phys + SrcMemSize));
			req->dst.yrgb_addr =
				(req->dst.yrgb_addr & (~PAGE_MASK));

			uv_size = (req->dst.uv_addr
				 - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;
			v_size = (req->dst.v_addr
				 - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;
			req->dst.uv_addr = (req->dst.uv_addr & (~PAGE_MASK)) |
				((uv_size) << PAGE_SHIFT);
			req->dst.v_addr = (req->dst.v_addr & (~PAGE_MASK)) |
				((v_size) << PAGE_SHIFT);
		}

		/* flush data to DDR */
		rga2_dma_sync_flush_range(MMU_Base, (MMU_Base + AllSize), scheduler);
		rga2_mmu_buf_get(&rga2_mmu_info, AllSize);
		reg->MMU_len = AllSize;

		return 0;
	} while (0);

	return 0;
}

static int rga2_mmu_info_color_fill_mode(struct rga2_mmu_other_t *reg,
					 struct rga2_req *req,
					 struct rga_job *job)
{
	int DstMemSize = 0;
	unsigned long DstStart = 0;
	unsigned long DstPageCount = 0;
	struct page **pages = NULL;
	uint32_t uv_size, v_size;
	uint32_t AllSize;
	uint32_t *MMU_Base, *MMU_Base_phys;
	int ret;
	int status;
	struct sg_table *sgt;

	struct rga_scheduler_t *scheduler = NULL;

	scheduler = rga_job_get_scheduler(job->core);
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__,
				__LINE__);
		ret = -EINVAL;
		return ret;
	}

	MMU_Base = NULL;

	do {
		if (req->mmu_info.dst_mmu_flag & 1) {
			DstPageCount = rga_buf_size_cal(req->dst.yrgb_addr,
				req->dst.uv_addr, req->dst.v_addr,
				req->dst.format, req->dst.vir_w,
				req->dst.vir_h, &DstStart, NULL);
			if (DstPageCount == 0)
				return -EINVAL;
		}

		DstMemSize = (DstPageCount + 15) & (~15);
		AllSize = DstMemSize;

		if (rga2_mmu_buf_get_try(&rga2_mmu_info, AllSize)) {
			pr_err("Get MMU mem failed\n");
			status = RGA_MALLOC_ERROR;
			break;
		}

		pages = rga2_mmu_info.pages;
		if (pages == NULL) {
			pr_err("MMU malloc pages mem failed\n");
			return -EINVAL;
		}

		mutex_lock(&rga_drvdata->lock);
		MMU_Base_phys = rga2_mmu_info.buf + rga2_mmu_info.front;
		MMU_Base = rga2_mmu_info.buf_virtual + rga2_mmu_info.front;
		mutex_unlock(&rga_drvdata->lock);

		if (DstMemSize) {
			if (job->dst_buffer) {
				switch (job->dst_buffer->type) {
				case RGA_DMA_BUFFER:
					sgt = rga_mm_lookup_sgt(job->dst_buffer, scheduler->core);
					if (sgt == NULL) {
						pr_err("rga2 cannot get sgt from handle!\n");
						status = -EFAULT;
						goto out;
					}
					ret = rga2_MapION(sgt, &MMU_Base[0], DstMemSize);

					break;
				case RGA_VIRTUAL_ADDRESS:
					ret = rga2_MapUserMemory(&pages[0], &MMU_Base[0],
								 DstStart, DstPageCount,
								 1, MMU_MAP_INVALID,
								 scheduler,
								 job->dst_buffer->current_mm);

					/* Save pagetable to invalid cache and unmap. */
					reg->MMU_dst_base = MMU_Base;
					reg->MMU_dst_count = DstPageCount;

					break;
				default:
					status = -EFAULT;
					goto out;
				}

			} else {
				if (job->rga_dma_buffer_dst) {
					ret = rga2_MapION(job->rga_dma_buffer_dst->sgt,
							  &MMU_Base[0], DstMemSize);
				} else {
					ret = rga2_MapUserMemory(&pages[0],
								 &MMU_Base[0], DstStart,
								 DstPageCount, 1,
								 MMU_MAP_INVALID,
								 scheduler, job->mm);
				}
			}
			if (ret < 0) {
				pr_err("map dst memory failed\n");
				status = ret;
				break;
			}

			/* change the buf address in req struct */
			req->mmu_info.dst_base_addr =
				((unsigned long)MMU_Base_phys);
			req->dst.yrgb_addr =
				(req->dst.yrgb_addr & (~PAGE_MASK));

			uv_size = (req->dst.uv_addr
				 - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;
			v_size = (req->dst.v_addr
				 - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;
			req->dst.uv_addr = (req->dst.uv_addr & (~PAGE_MASK)) |
				((uv_size) << PAGE_SHIFT);
			req->dst.v_addr = (req->dst.v_addr & (~PAGE_MASK)) |
				((v_size) << PAGE_SHIFT);
		}

		/* flush data to DDR */
		rga2_dma_sync_flush_range(MMU_Base, (MMU_Base + AllSize + 1), scheduler);
		rga2_mmu_buf_get(&rga2_mmu_info, AllSize);
		reg->MMU_len = AllSize;

		return 0;
	} while (0);

out:
	return status;
}

static int rga2_mmu_info_update_palette_table_mode(struct rga2_mmu_other_t *reg,
						 struct rga2_req *req,
						 struct rga_job *job)
{
	int LutMemSize = 0;
	unsigned long LutStart = 0;
	unsigned long LutPageCount = 0;
	struct page **pages = NULL;
	uint32_t uv_size, v_size;
	uint32_t AllSize;
	uint32_t *MMU_Base, *MMU_Base_phys;
	int ret, status;

	struct rga_scheduler_t *scheduler = NULL;

	if (job->flags & RGA_JOB_USE_HANDLE) {
		pr_err("update palette table mode can not support handle.\n");
		return -EINVAL;
	}

	scheduler = rga_job_get_scheduler(job->core);
	if (scheduler == NULL) {
		pr_err("failed to get scheduler, %s(%d)\n", __func__,
				__LINE__);
		ret = -EINVAL;
		return ret;
	}

	MMU_Base = NULL;

	do {
		/* cal lut buf mmu info */
		if (req->mmu_info.els_mmu_flag & 1) {
			req->mmu_info.src0_mmu_flag =
				req->mmu_info.src0_mmu_flag ==
				1 ? 0 : req->mmu_info.src0_mmu_flag;
			req->mmu_info.src1_mmu_flag =
				req->mmu_info.src1_mmu_flag ==
				1 ? 0 : req->mmu_info.src1_mmu_flag;
			req->mmu_info.dst_mmu_flag =
				req->mmu_info.dst_mmu_flag ==
				1 ? 0 : req->mmu_info.dst_mmu_flag;

			LutPageCount =
				rga_buf_size_cal(req->pat.yrgb_addr,
					req->pat.uv_addr, req->pat.v_addr,
					req->pat.format, req->pat.vir_w,
					req->pat.vir_h, &LutStart, NULL);
			if (LutPageCount == 0)
				return -EINVAL;
		}

		LutMemSize = (LutPageCount + 15) & (~15);
		AllSize = LutMemSize;

		if (rga2_mmu_buf_get_try(&rga2_mmu_info, AllSize)) {
			pr_err("Get MMU mem failed\n");
			status = RGA_MALLOC_ERROR;
			break;
		}

		pages = rga2_mmu_info.pages;
		if (pages == NULL) {
			pr_err("MMU malloc pages mem failed\n");
			return -EINVAL;
		}

		mutex_lock(&rga_drvdata->lock);
		MMU_Base = rga2_mmu_info.buf_virtual + rga2_mmu_info.front;
		MMU_Base_phys = rga2_mmu_info.buf + rga2_mmu_info.front;
		mutex_unlock(&rga_drvdata->lock);

		if (LutMemSize) {
			if (job->rga_dma_buffer_els) {
				ret = rga2_MapION(job->rga_dma_buffer_els->sgt,
						&MMU_Base[0], LutMemSize);
			} else {
				ret = rga2_MapUserMemory(&pages[0],
						&MMU_Base[0], LutStart,
						LutPageCount, 0, MMU_MAP_CLEAN, scheduler, job->mm);
			}
			if (ret < 0) {
				pr_err("rga2 map palette memory failed\n");
				status = ret;
				break;
			}

			/* change the buf address in req struct */
			req->mmu_info.els_base_addr =
				(((unsigned long)MMU_Base_phys));

			req->pat.yrgb_addr =
				(req->pat.yrgb_addr & (~PAGE_MASK));

			uv_size = (req->pat.uv_addr
				 - (LutStart << PAGE_SHIFT)) >> PAGE_SHIFT;
			v_size = (req->pat.v_addr
				 - (LutStart << PAGE_SHIFT)) >> PAGE_SHIFT;
			req->pat.uv_addr = (req->pat.uv_addr & (~PAGE_MASK)) |
				((uv_size) << PAGE_SHIFT);
			req->pat.v_addr = (req->pat.v_addr & (~PAGE_MASK)) |
				((v_size) << PAGE_SHIFT);
		}

		/* flush data to DDR */
		rga2_dma_sync_flush_range(MMU_Base, (MMU_Base + AllSize), scheduler);
		rga2_mmu_buf_get(&rga2_mmu_info, AllSize);
		reg->MMU_len = AllSize;

		return 0;
	} while (0);

	return status;
}

int rga2_set_mmu_reg_info(struct rga2_mmu_other_t *reg, struct rga2_req *req,
			 struct rga_job *job)
{
	int ret;

	if (reg->MMU_map == true) {
		ret = rga2_mmu_flush_cache(reg, req, job);
		return ret;
	}

	switch (req->render_mode) {
	case BITBLT_MODE:
		ret = rga2_mmu_info_BitBlt_mode(reg, req, job);
		break;
	case COLOR_PALETTE_MODE:
		ret = rga2_mmu_info_color_palette_mode(reg, req, job);
		break;
	case COLOR_FILL_MODE:
		ret = rga2_mmu_info_color_fill_mode(reg, req, job);
		break;
	case UPDATE_PALETTE_TABLE_MODE:
		ret = rga2_mmu_info_update_palette_table_mode(reg, req, job);
		break;
	default:
		ret = -1;
		break;
	}

	return ret;
}
