/* SPDX-License-Identifier: GPL-2.0 */

#define pr_fmt(fmt) "rga2_mmu: " fmt
#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/memory.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <asm/memory.h>
#include <asm/atomic.h>
#include <asm/cacheflush.h>
#include "rga2_mmu_info.h"
#if RGA2_DEBUGFS
extern int RGA2_CHECK_MODE;
#endif
extern struct rga2_service_info rga2_service;
extern struct rga2_mmu_buf_t rga2_mmu_buf;

//extern int mmu_buff_temp[1024];

#define KERNEL_SPACE_VALID    0xc0000000

#define V7_VATOPA_SUCESS_MASK	(0x1)
#define V7_VATOPA_GET_PADDR(X)	(X & 0xFFFFF000)
#define V7_VATOPA_GET_INER(X)		((X>>4) & 7)
#define V7_VATOPA_GET_OUTER(X)		((X>>2) & 3)
#define V7_VATOPA_GET_SH(X)		((X>>7) & 1)
#define V7_VATOPA_GET_NS(X)		((X>>9) & 1)
#define V7_VATOPA_GET_SS(X)		((X>>1) & 1)

void rga2_dma_flush_range(void *pstart, void *pend)
{
	dma_sync_single_for_device(rga2_drvdata->dev, virt_to_phys(pstart), pend - pstart, DMA_TO_DEVICE);
}

dma_addr_t rga2_dma_flush_page(struct page *page, int map)
{
	dma_addr_t paddr;

	/*
	 * Through dma_map_page to ensure that the physical address
	 * will not exceed the addressing range of dma.
	 */
	if (map & MMU_MAP_MASK) {
		switch (map) {
		case MMU_MAP_CLEAN:
			paddr = dma_map_page(rga2_drvdata->dev, page, 0,
					     PAGE_SIZE, DMA_TO_DEVICE);
			break;
		case MMU_MAP_INVALID:
			paddr = dma_map_page(rga2_drvdata->dev, page, 0,
					     PAGE_SIZE, DMA_FROM_DEVICE);
			break;
		case MMU_MAP_CLEAN | MMU_MAP_INVALID:
			paddr = dma_map_page(rga2_drvdata->dev, page, 0,
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
			dma_unmap_page(rga2_drvdata->dev, paddr,
				       PAGE_SIZE, DMA_TO_DEVICE);
			break;
		case MMU_UNMAP_INVALID:
			dma_unmap_page(rga2_drvdata->dev, paddr,
				       PAGE_SIZE, DMA_FROM_DEVICE);
			break;
		case MMU_UNMAP_CLEAN | MMU_UNMAP_INVALID:
			dma_unmap_page(rga2_drvdata->dev, paddr,
				       PAGE_SIZE, DMA_BIDIRECTIONAL);
			break;
		default:
			pr_err("unknown map cmd 0x%x\n", map);
			break;
		}

		return paddr;
	}

	pr_err("RGA2 failed to flush page, map= %x\n", map);
	return 0;
}

#if 0
static unsigned int armv7_va_to_pa(unsigned int v_addr)
{
	unsigned int p_addr;
	__asm__ volatile (	"mcr p15, 0, %1, c7, c8, 0\n"
						"isb\n"
						"dsb\n"
						"mrc p15, 0, %0, c7, c4, 0\n"
						: "=r" (p_addr)
						: "r" (v_addr)
						: "cc");

	if (p_addr & V7_VATOPA_SUCESS_MASK)
		return 0xFFFFFFFF;
	else
		return (V7_VATOPA_GET_SS(p_addr) ? 0xFFFFFFFF : V7_VATOPA_GET_PADDR(p_addr));
}
#endif

static int rga2_mmu_buf_get(struct rga2_mmu_buf_t *t, uint32_t size)
{
    mutex_lock(&rga2_service.lock);
    t->front += size;
    mutex_unlock(&rga2_service.lock);

    return 0;
}

static int rga2_mmu_buf_get_try(struct rga2_mmu_buf_t *t, uint32_t size)
{
	int ret = 0;

	mutex_lock(&rga2_service.lock);
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
	mutex_unlock(&rga2_service.lock);
	return ret;
}

static int rga2_mem_size_cal(unsigned long Mem, uint32_t MemSize, unsigned long *StartAddr)
{
    unsigned long start, end;
    uint32_t pageCount;

    end = (Mem + (MemSize + PAGE_SIZE - 1)) >> PAGE_SHIFT;
    start = Mem >> PAGE_SHIFT;
    pageCount = end - start;
    *StartAddr = start;
    return pageCount;
}

static int rga2_buf_size_cal(unsigned long yrgb_addr, unsigned long uv_addr, unsigned long v_addr,
                                        int format, uint32_t w, uint32_t h, unsigned long *StartAddr )
{
    uint32_t size_yrgb = 0;
    uint32_t size_uv = 0;
    uint32_t size_v = 0;
    uint32_t stride = 0;
    unsigned long start, end;
    uint32_t pageCount;

    switch(format)
    {
        case RGA2_FORMAT_RGBA_8888 :
        case RGA2_FORMAT_RGBX_8888 :
        case RGA2_FORMAT_BGRA_8888 :
        case RGA2_FORMAT_BGRX_8888 :
            stride = (w * 4 + 3) & (~3);
            size_yrgb = stride*h;
            start = yrgb_addr >> PAGE_SHIFT;
	    end = yrgb_addr + size_yrgb;
	    end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	    pageCount = end - start;
            break;
        case RGA2_FORMAT_RGB_888 :
        case RGA2_FORMAT_BGR_888 :
            stride = (w * 3 + 3) & (~3);
            size_yrgb = stride*h;
            start = yrgb_addr >> PAGE_SHIFT;
	    end = yrgb_addr + size_yrgb;
	    end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	    pageCount = end - start;
            break;
        case RGA2_FORMAT_RGB_565 :
        case RGA2_FORMAT_RGBA_5551 :
        case RGA2_FORMAT_RGBA_4444 :
        case RGA2_FORMAT_BGR_565 :
        case RGA2_FORMAT_BGRA_5551 :
        case RGA2_FORMAT_BGRA_4444 :
            stride = (w*2 + 3) & (~3);
            size_yrgb = stride * h;
            start = yrgb_addr >> PAGE_SHIFT;
	    end = yrgb_addr + size_yrgb;
	    end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	    pageCount = end - start;
            break;

        /* YUV FORMAT */
        case RGA2_FORMAT_YCbCr_422_SP :
        case RGA2_FORMAT_YCrCb_422_SP :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = stride * h;
            start = MIN(yrgb_addr, uv_addr);
            start >>= PAGE_SHIFT;
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RGA2_FORMAT_YCbCr_422_P :
        case RGA2_FORMAT_YCrCb_422_P :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = ((stride >> 1) * h);
            size_v = ((stride >> 1) * h);
            start = MIN(MIN(yrgb_addr, uv_addr), v_addr);
            start = start >> PAGE_SHIFT;
            end = MAX(MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv)), (v_addr + size_v));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RGA2_FORMAT_YCbCr_420_SP :
        case RGA2_FORMAT_YCrCb_420_SP :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = (stride * (h >> 1));
            start = MIN(yrgb_addr, uv_addr);
            start >>= PAGE_SHIFT;
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RGA2_FORMAT_YCbCr_420_P :
        case RGA2_FORMAT_YCrCb_420_P :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = ((stride >> 1) * (h >> 1));
            size_v = ((stride >> 1) * (h >> 1));
            start = MIN(MIN(yrgb_addr, uv_addr), v_addr);
            start >>= PAGE_SHIFT;
            end = MAX(MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv)), (v_addr + size_v));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
		case RGA2_FORMAT_YCbCr_400:
			stride = (w + 3) & (~3);
			size_yrgb = stride * h;
			size_uv = 0;
			size_v = 0;
			start = yrgb_addr >> PAGE_SHIFT;
			end = yrgb_addr + size_yrgb;
			end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
			pageCount = end - start;
			break;
		case RGA2_FORMAT_Y4:
			stride = ((w + 3) & (~3) ) >> 1;
			size_yrgb = stride * h;
			size_uv = 0;
			size_v = 0;
			start = yrgb_addr >> PAGE_SHIFT;
			end = yrgb_addr + size_yrgb;
			end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
			pageCount = end - start;
			break;
		case RGA2_FORMAT_YVYU_422:
		case RGA2_FORMAT_VYUY_422:
		case RGA2_FORMAT_YUYV_422:
		case RGA2_FORMAT_UYVY_422:
			stride = (w + 3) & (~3);
			size_yrgb = stride * h;
			size_uv = stride * h;
			start = MIN(yrgb_addr, uv_addr);
			start >>= PAGE_SHIFT;
			end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
			end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
			pageCount = end - start;
			break;
		case RGA2_FORMAT_YVYU_420:
		case RGA2_FORMAT_VYUY_420:
		case RGA2_FORMAT_YUYV_420:
		case RGA2_FORMAT_UYVY_420:
			stride = (w + 3) & (~3);
			size_yrgb = stride * h;
			size_uv = (stride * (h >> 1));
			start = MIN(yrgb_addr, uv_addr);
			start >>= PAGE_SHIFT;
			end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
			end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
			pageCount = end - start;
			break;
#if 0
        case RK_FORMAT_BPP1 :
            break;
        case RK_FORMAT_BPP2 :
            break;
        case RK_FORMAT_BPP4 :
            break;
        case RK_FORMAT_BPP8 :
            break;
#endif
        case RGA2_FORMAT_YCbCr_420_SP_10B:
        case RGA2_FORMAT_YCrCb_420_SP_10B:
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = (stride * (h >> 1));
            start = MIN(yrgb_addr, uv_addr);
            start >>= PAGE_SHIFT;
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        default :
            pageCount = 0;
            start = 0;
            break;
    }

    *StartAddr = start;
    return pageCount;
}

#if RGA2_DEBUGFS
static int rga2_UserMemory_cheeck(struct page **pages, u32 w, u32 h, u32 format, int flag)
{
	int bits;
	void *vaddr = NULL;
	int taipage_num;
	int taidata_num;
	int *tai_vaddr = NULL;

	switch (format) {
	case RGA2_FORMAT_RGBA_8888:
	case RGA2_FORMAT_RGBX_8888:
	case RGA2_FORMAT_BGRA_8888:
	case RGA2_FORMAT_BGRX_8888:
		bits = 32;
		break;
	case RGA2_FORMAT_RGB_888:
	case RGA2_FORMAT_BGR_888:
		bits = 24;
		break;
	case RGA2_FORMAT_RGB_565:
	case RGA2_FORMAT_RGBA_5551:
	case RGA2_FORMAT_RGBA_4444:
	case RGA2_FORMAT_BGR_565:
	case RGA2_FORMAT_YCbCr_422_SP:
	case RGA2_FORMAT_YCbCr_422_P:
	case RGA2_FORMAT_YCrCb_422_SP:
	case RGA2_FORMAT_YCrCb_422_P:
	case RGA2_FORMAT_BGRA_5551:
	case RGA2_FORMAT_BGRA_4444:
		bits = 16;
		break;
	case RGA2_FORMAT_YCbCr_420_SP:
	case RGA2_FORMAT_YCbCr_420_P:
	case RGA2_FORMAT_YCrCb_420_SP:
	case RGA2_FORMAT_YCrCb_420_P:
		bits = 12;
		break;
	case RGA2_FORMAT_YCbCr_420_SP_10B:
	case RGA2_FORMAT_YCrCb_420_SP_10B:
	case RGA2_FORMAT_YCbCr_422_SP_10B:
	case RGA2_FORMAT_YCrCb_422_SP_10B:
		bits = 15;
		break;
	default:
		printk("un know format\n");
		return -1;
	}
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
#endif

static int rga2_MapUserMemory(struct page **pages, uint32_t *pageTable,
			      unsigned long Memory, uint32_t pageCount,
			      int writeFlag, int map)
{
	struct vm_area_struct *vma;
	int32_t result;
	uint32_t i;
	uint32_t status;
	unsigned long Address;
	unsigned long pfn;
	struct page __maybe_unused *page;
	spinlock_t * ptl;
	pte_t * pte;
	pgd_t * pgd;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	p4d_t * p4d;
#endif
	pud_t * pud;
	pmd_t * pmd;

	status = 0;
	Address = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	mmap_read_lock(current->mm);
#else
	down_read(&current->mm->mmap_sem);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 168) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
	result = get_user_pages(current, current->mm, Memory << PAGE_SHIFT,
				pageCount, writeFlag ? FOLL_WRITE : 0,
				pages, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	result = get_user_pages(current, current->mm, Memory << PAGE_SHIFT,
				pageCount, writeFlag, 0, pages, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	result = get_user_pages_remote(current, current->mm,
				       Memory << PAGE_SHIFT,
				       pageCount, writeFlag, pages, NULL, NULL);
#else
	result = get_user_pages_remote(current->mm, Memory << PAGE_SHIFT,
				       pageCount, writeFlag, pages, NULL, NULL);
#endif

	if (result > 0 && result >= pageCount) {
		/* Fill the page table. */
		for (i = 0; i < pageCount; i++) {
			/* Get the physical address from page struct. */
			pageTable[i] = rga2_dma_flush_page(pages[i], map);
		}

		for (i = 0; i < result; i++)
			put_page(pages[i]);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
		mmap_read_unlock(current->mm);
#else
		up_read(&current->mm->mmap_sem);
#endif
		return 0;
	}
	if (result > 0) {
		for (i = 0; i < result; i++)
			put_page(pages[i]);
	}
	for (i = 0; i < pageCount; i++) {
		vma = find_vma(current->mm, (Memory + i) << PAGE_SHIFT);
		if (!vma) {
			pr_err("RGA2 failed to get vma, result = %d, pageCount = %d\n",
			       result, pageCount);
			status = RGA2_OUT_OF_RESOURCES;
			break;
		}
		pgd = pgd_offset(current->mm, (Memory + i) << PAGE_SHIFT);
		if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd))) {
			pr_err("RGA2 failed to get pgd, result = %d, pageCount = %d\n",
			       result, pageCount);
			status = RGA2_OUT_OF_RESOURCES;
			break;
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
		/* In the four-level page table, it will do nothing and return pgd. */
		p4d = p4d_offset(pgd, (Memory + i) << PAGE_SHIFT);
		if (p4d_none(*p4d) || unlikely(p4d_bad(*p4d))) {
			pr_err("RGA2 failed to get p4d, result = %d, pageCount = %d\n",
			       result, pageCount);
			status = RGA2_OUT_OF_RESOURCES;
			break;
		}

		pud = pud_offset(p4d, (Memory + i) << PAGE_SHIFT);
#else
		pud = pud_offset(pgd, (Memory + i) << PAGE_SHIFT);
#endif
		if (pud_none(*pud) || unlikely(pud_bad(*pud))) {
			pr_err("RGA2 failed to get pud, result = %d, pageCount = %d\n",
			       result, pageCount);
			status = RGA2_OUT_OF_RESOURCES;
			break;
		}
		pmd = pmd_offset(pud, (Memory + i) << PAGE_SHIFT);
		if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd))) {
			pr_err("RGA2 failed to get pmd, result = %d, pageCount = %d\n",
			       result, pageCount);
			status = RGA2_OUT_OF_RESOURCES;
			break;
		}
		pte = pte_offset_map_lock(current->mm, pmd,
					  (Memory + i) << PAGE_SHIFT,
					  &ptl);
		if (pte_none(*pte)) {
			pr_err("RGA2 failed to get pte, result = %d, pageCount = %d\n",
				result, pageCount);
			pte_unmap_unlock(pte, ptl);
			status = RGA2_OUT_OF_RESOURCES;
			break;
		}
		pfn = pte_pfn(*pte);
		Address = ((pfn << PAGE_SHIFT) |
			  (((unsigned long)((Memory + i) << PAGE_SHIFT)) & ~PAGE_MASK));

		pageTable[i] = rga2_dma_flush_page(phys_to_page(Address), map);

		pte_unmap_unlock(pte, ptl);
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	mmap_read_unlock(current->mm);
#else
	up_read(&current->mm->mmap_sem);
#endif
	return status;
}

static int rga2_MapION(struct sg_table *sg,
                               uint32_t *Memory,
                               int32_t  pageCount)
{
    uint32_t i;
    uint32_t status;
    unsigned long Address;
    uint32_t mapped_size = 0;
    uint32_t len;
    struct scatterlist *sgl = sg->sgl;
    uint32_t sg_num = 0;
    uint32_t break_flag = 0;

    status = 0;
    Address = 0;
    do {
        len = sg_dma_len(sgl) >> PAGE_SHIFT;
	/*
	 * The fd passed by user space gets sg through dma_buf_map_attachment,
	 * so dma_address can be use here.
	 */
        Address = sg_dma_address(sgl);

        for(i=0; i<len; i++) {
            if (mapped_size + i >= pageCount) {
                break_flag = 1;
                break;
            }
            Memory[mapped_size + i] = (uint32_t)(Address + (i << PAGE_SHIFT));
        }
        if (break_flag)
            break;
        mapped_size += len;
        sg_num += 1;
    }
    while((sgl = sg_next(sgl)) && (mapped_size < pageCount) && (sg_num < sg->nents));

    return 0;
}

static int rga2_mmu_flush_cache(struct rga2_reg *reg, struct rga2_req *req)
{
	int DstMemSize;
	unsigned long DstStart, DstPageCount;
	uint32_t *MMU_Base, *MMU_Base_phys;
	int ret;
	int status;
	struct page **pages = NULL;

	MMU_Base = NULL;
	DstMemSize  = 0;
	DstPageCount = 0;
	DstStart = 0;

	if (reg->MMU_map != true) {
		status = -EINVAL;
		goto out;
	}

	/* cal dst buf mmu info */
	if (req->mmu_info.dst_mmu_flag & 1) {
		DstPageCount = rga2_buf_size_cal(req->dst.yrgb_addr,
						 req->dst.uv_addr,
						 req->dst.v_addr,
						 req->dst.format,
						 req->dst.vir_w,
						 req->dst.vir_h,
						 &DstStart);
		if (DstPageCount == 0)
			return -EINVAL;
	}
	/* Cal out the needed mem size */
	DstMemSize  = (DstPageCount + 15) & (~15);

	if (rga2_mmu_buf_get_try(&rga2_mmu_buf, DstMemSize)) {
		pr_err("RGA2 Get MMU mem failed\n");
		status = RGA2_MALLOC_ERROR;
		goto out;
	}
	pages = rga2_mmu_buf.pages;
	mutex_lock(&rga2_service.lock);
	MMU_Base = rga2_mmu_buf.buf_virtual +
		   (rga2_mmu_buf.front & (rga2_mmu_buf.size - 1));
	MMU_Base_phys = rga2_mmu_buf.buf +
			(rga2_mmu_buf.front & (rga2_mmu_buf.size - 1));

	mutex_unlock(&rga2_service.lock);
	if (DstMemSize) {
		if (req->sg_dst) {
			status = -EINVAL;
			goto out;
		} else {
			ret = rga2_MapUserMemory(&pages[0],
						 MMU_Base,
						 DstStart, DstPageCount, 1,
						 MMU_MAP_CLEAN | MMU_MAP_INVALID);
#if RGA2_DEBUGFS
			if (RGA2_CHECK_MODE)
				rga2_UserMemory_cheeck(&pages[0],
						       req->dst.vir_w,
						       req->dst.vir_h,
						       req->dst.format,
						       2);
#endif
		}
		if (ret < 0) {
			pr_err("rga2 unmap dst memory failed\n");
			status = ret;
			goto out;
		}
	}
	rga2_mmu_buf_get(&rga2_mmu_buf, DstMemSize);
	reg->MMU_len = DstMemSize;
	status = 0;
out:
	return status;
}

static int rga2_mmu_info_BitBlt_mode(struct rga2_reg *reg, struct rga2_req *req)
{
	int Src0MemSize, DstMemSize, Src1MemSize;
	unsigned long Src0Start, Src1Start, DstStart;
	unsigned long Src0PageCount, Src1PageCount, DstPageCount;
	uint32_t AllSize;
	uint32_t *MMU_Base, *MMU_Base_phys;
	int ret;
	int status;
	uint32_t uv_size, v_size;
	struct page **pages = NULL;
	MMU_Base = NULL;
	Src0MemSize = 0;
	Src1MemSize = 0;
	DstMemSize  = 0;
	Src0PageCount = 0;
	Src1PageCount = 0;
	DstPageCount = 0;
	Src0Start = 0;
	Src1Start = 0;
	DstStart = 0;

	/* cal src0 buf mmu info */
	if (req->mmu_info.src0_mmu_flag & 1) {
		Src0PageCount = rga2_buf_size_cal(req->src.yrgb_addr,
						  req->src.uv_addr,
						  req->src.v_addr,
						  req->src.format,
						  req->src.vir_w,
						  (req->src.vir_h),
						  &Src0Start);
		if (Src0PageCount == 0)
			return -EINVAL;
	}
	/* cal src1 buf mmu info */
	if (req->mmu_info.src1_mmu_flag & 1) {
		Src1PageCount = rga2_buf_size_cal(req->src1.yrgb_addr,
						  req->src1.uv_addr,
						  req->src1.v_addr,
						  req->src1.format,
						  req->src1.vir_w,
						  (req->src1.vir_h),
						  &Src1Start);
		if (Src1PageCount == 0)
			return -EINVAL;
	}
	/* cal dst buf mmu info */
	if (req->mmu_info.dst_mmu_flag & 1) {
		DstPageCount = rga2_buf_size_cal(req->dst.yrgb_addr,
						 req->dst.uv_addr,
						 req->dst.v_addr,
						 req->dst.format,
						 req->dst.vir_w,
						 req->dst.vir_h,
						 &DstStart);
		if (DstPageCount == 0)
			return -EINVAL;
	}
	/* Cal out the needed mem size */
	Src0MemSize = (Src0PageCount + 15) & (~15);
	Src1MemSize = (Src1PageCount + 15) & (~15);
	DstMemSize  = (DstPageCount + 15) & (~15);
	AllSize = Src0MemSize + Src1MemSize + DstMemSize;

	if (rga2_mmu_buf_get_try(&rga2_mmu_buf, AllSize)) {
		pr_err("RGA2 Get MMU mem failed\n");
		status = RGA2_MALLOC_ERROR;
		goto out;
	}

	pages = rga2_mmu_buf.pages;
	if(pages == NULL) {
		pr_err("RGA MMU malloc pages mem failed\n");
		return -EINVAL;
	}

	mutex_lock(&rga2_service.lock);
	MMU_Base = rga2_mmu_buf.buf_virtual + rga2_mmu_buf.front;
	MMU_Base_phys = rga2_mmu_buf.buf + rga2_mmu_buf.front;
	mutex_unlock(&rga2_service.lock);

        if (Src0MemSize) {
		if (req->sg_src0) {
			ret = rga2_MapION(req->sg_src0,
					  &MMU_Base[0], Src0MemSize);
		} else {
			ret = rga2_MapUserMemory(&pages[0], &MMU_Base[0],
						 Src0Start, Src0PageCount,
						 0, MMU_MAP_CLEAN);
#if RGA2_DEBUGFS
			if (RGA2_CHECK_MODE)
				rga2_UserMemory_cheeck(&pages[0],
						       req->src.vir_w,
						       req->src.vir_h,
						       req->src.format,
						       1);
#endif

			/* Save pagetable to unmap. */
			reg->MMU_src0_base = MMU_Base;
			reg->MMU_src0_count = Src0PageCount;
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
		if (req->sg_src1) {
			ret = rga2_MapION(req->sg_src1,
					MMU_Base + Src0MemSize, Src1MemSize);
		} else {
			ret = rga2_MapUserMemory(&pages[0],
						 MMU_Base + Src0MemSize,
						 Src1Start, Src1PageCount,
						 0, MMU_MAP_CLEAN);

			/* Save pagetable to unmap. */
			reg->MMU_src1_base = MMU_Base + Src0MemSize;
			reg->MMU_src1_count = Src1PageCount;
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
		if (req->sg_dst) {
			ret = rga2_MapION(req->sg_dst, MMU_Base + Src0MemSize
					  + Src1MemSize, DstMemSize);
		} else if (req->alpha_mode_0 != 0 && req->bitblt_mode == 0) {
			/* The blend mode of src + dst => dst requires clean and invalidate */
			ret = rga2_MapUserMemory(&pages[0], MMU_Base
						 + Src0MemSize + Src1MemSize,
						 DstStart, DstPageCount, 1,
						 MMU_MAP_CLEAN | MMU_MAP_INVALID);
#if RGA2_DEBUGFS
			if (RGA2_CHECK_MODE)
				rga2_UserMemory_cheeck(&pages[0],
						       req->dst.vir_w,
						       req->dst.vir_h,
						       req->dst.format,
						       2);
#endif

			/* Save pagetable to invalid cache and unmap. */
			reg->MMU_dst_base = MMU_Base + Src0MemSize + Src1MemSize;
			reg->MMU_dst_count = DstPageCount;
		} else {
			ret = rga2_MapUserMemory(&pages[0], MMU_Base
						 + Src0MemSize + Src1MemSize,
						 DstStart, DstPageCount,
						 1, MMU_MAP_INVALID);
#if RGA2_DEBUGFS
			if (RGA2_CHECK_MODE)
				rga2_UserMemory_cheeck(&pages[0],
						       req->dst.vir_w,
						       req->dst.vir_h,
						       req->dst.format,
						       2);
#endif

			/* Save pagetable to invalid cache and unmap. */
			reg->MMU_dst_base = MMU_Base + Src0MemSize + Src1MemSize;
			reg->MMU_dst_count = DstPageCount;
		}

		if (ret < 0) {
			pr_err("rga2 map dst memory failed\n");
			status = ret;
			goto out;
		}
		/* change the buf address in req struct */
		req->mmu_info.dst_base_addr  = ((unsigned long)(MMU_Base_phys
					+ Src0MemSize + Src1MemSize));
		req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK));
		uv_size = (req->dst.uv_addr
			   - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;
		v_size = (req->dst.v_addr
			  - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;
		req->dst.uv_addr = (req->dst.uv_addr & (~PAGE_MASK)) |
						   ((uv_size) << PAGE_SHIFT);
		req->dst.v_addr = (req->dst.v_addr & (~PAGE_MASK)) |
			((v_size) << PAGE_SHIFT);

		if (((req->alpha_rop_flag & 1) == 1) && (req->bitblt_mode == 0)) {
			req->mmu_info.src1_base_addr = req->mmu_info.dst_base_addr;
			req->mmu_info.src1_mmu_flag  = req->mmu_info.dst_mmu_flag;
		}
	}

	/* flush data to DDR */
	rga2_dma_flush_range(MMU_Base, (MMU_Base + AllSize));
	rga2_mmu_buf_get(&rga2_mmu_buf, AllSize);
	reg->MMU_len = AllSize;
	status = 0;
out:
	return status;
}

static int rga2_mmu_info_color_palette_mode(struct rga2_reg *reg, struct rga2_req *req)
{
    int SrcMemSize, DstMemSize;
    unsigned long SrcStart, DstStart;
    unsigned long SrcPageCount, DstPageCount;
    struct page **pages = NULL;
    uint32_t uv_size, v_size;
    uint32_t AllSize;
    uint32_t *MMU_Base = NULL, *MMU_Base_phys;
    int ret, status;
    uint32_t stride;

    uint8_t shift;
    uint32_t sw, byte_num;

    shift = 3 - (req->palette_mode & 3);
    sw = req->src.vir_w*req->src.vir_h;
    byte_num = sw >> shift;
    stride = (byte_num + 3) & (~3);

    SrcStart = 0;
    DstStart = 0;
    SrcPageCount = 0;
    DstPageCount = 0;
    SrcMemSize = 0;
    DstMemSize = 0;

    do {
        if (req->mmu_info.src0_mmu_flag) {
            if (req->mmu_info.els_mmu_flag & 1) {
                req->mmu_info.src0_mmu_flag = 0;
                req->mmu_info.src1_mmu_flag = 0;
            } else {
                req->mmu_info.els_mmu_flag = req->mmu_info.src0_mmu_flag;
                req->mmu_info.src0_mmu_flag = 0;
            }

            SrcPageCount = rga2_mem_size_cal(req->src.yrgb_addr, stride, &SrcStart);
            if(SrcPageCount == 0) {
                return -EINVAL;
            }
        }

        if (req->mmu_info.dst_mmu_flag) {
            DstPageCount = rga2_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                            req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                            &DstStart);
            if(DstPageCount == 0) {
                return -EINVAL;
            }
        }

        SrcMemSize = (SrcPageCount + 15) & (~15);
        DstMemSize = (DstPageCount + 15) & (~15);

        AllSize = SrcMemSize + DstMemSize;

        if (rga2_mmu_buf_get_try(&rga2_mmu_buf, AllSize)) {
            pr_err("RGA2 Get MMU mem failed\n");
            status = RGA2_MALLOC_ERROR;
            break;
        }

        pages = rga2_mmu_buf.pages;
        if(pages == NULL) {
            pr_err("RGA MMU malloc pages mem failed\n");
            return -EINVAL;
        }

        mutex_lock(&rga2_service.lock);
        MMU_Base = rga2_mmu_buf.buf_virtual + rga2_mmu_buf.front;
        MMU_Base_phys = rga2_mmu_buf.buf + rga2_mmu_buf.front;
        mutex_unlock(&rga2_service.lock);

        if(SrcMemSize) {
            if (req->sg_src0) {
                ret = rga2_MapION(req->sg_src0,
                &MMU_Base[0], SrcMemSize);
            } else {
                ret = rga2_MapUserMemory(&pages[0], &MMU_Base[0],
                SrcStart, SrcPageCount, 0, MMU_MAP_CLEAN);
#if RGA2_DEBUGFS
                if (RGA2_CHECK_MODE)
                rga2_UserMemory_cheeck(&pages[0], req->src.vir_w,
                req->src.vir_h, req->src.format,
                1);
#endif
            }
            if (ret < 0) {
                pr_err("rga2 map src0 memory failed\n");
                status = ret;
                break;
            }

            /* change the buf address in req struct */
            req->mmu_info.els_base_addr = (((unsigned long)MMU_Base_phys));
	    /*
	     *The color palette mode will not have YUV format as input,
	     *so UV component address is not needed
	     */
            req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK));
        }

        if(DstMemSize) {
            if (req->sg_dst) {
                ret = rga2_MapION(req->sg_dst,
                MMU_Base + SrcMemSize, DstMemSize);
            } else {
                ret = rga2_MapUserMemory(&pages[0], MMU_Base + SrcMemSize,
                DstStart, DstPageCount, 1, MMU_MAP_INVALID);
#if RGA2_DEBUGFS
                if (RGA2_CHECK_MODE)
                rga2_UserMemory_cheeck(&pages[0], req->dst.vir_w,
                req->dst.vir_h, req->dst.format,
                1);
#endif
            }
            if (ret < 0) {
                pr_err("rga2 map dst memory failed\n");
                status = ret;
                break;
            }
            /* change the buf address in req struct */
            req->mmu_info.dst_base_addr  = ((unsigned long)(MMU_Base_phys + SrcMemSize));
            req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK));

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
        rga2_dma_flush_range(MMU_Base, (MMU_Base + AllSize));
        rga2_mmu_buf_get(&rga2_mmu_buf, AllSize);
        reg->MMU_len = AllSize;

        return 0;
    }
    while(0);

    return 0;
}

static int rga2_mmu_info_color_fill_mode(struct rga2_reg *reg, struct rga2_req *req)
{
    int DstMemSize;
    unsigned long DstStart;
    unsigned long DstPageCount;
    struct page **pages = NULL;
    uint32_t uv_size, v_size;
    uint32_t AllSize;
    uint32_t *MMU_Base, *MMU_Base_phys;
    int ret;
    int status;

    DstMemSize = 0;
    DstPageCount = 0;
    DstStart = 0;
    MMU_Base = NULL;

    do {
        if(req->mmu_info.dst_mmu_flag & 1) {
            DstPageCount = rga2_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                        req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                        &DstStart);
            if(DstPageCount == 0) {
                return -EINVAL;
            }
        }

        DstMemSize = (DstPageCount + 15) & (~15);
	AllSize = DstMemSize;

        if(rga2_mmu_buf_get_try(&rga2_mmu_buf, AllSize)) {
           pr_err("RGA2 Get MMU mem failed\n");
           status = RGA2_MALLOC_ERROR;
           break;
        }

        pages = rga2_mmu_buf.pages;
        if(pages == NULL) {
            pr_err("RGA MMU malloc pages mem failed\n");
            return -EINVAL;
        }

        mutex_lock(&rga2_service.lock);
        MMU_Base_phys = rga2_mmu_buf.buf + rga2_mmu_buf.front;
        MMU_Base = rga2_mmu_buf.buf_virtual + rga2_mmu_buf.front;
        mutex_unlock(&rga2_service.lock);

        if (DstMemSize) {
            if (req->sg_dst) {
                ret = rga2_MapION(req->sg_dst, &MMU_Base[0], DstMemSize);
            }
            else {
		    ret = rga2_MapUserMemory(&pages[0], &MMU_Base[0],
					     DstStart, DstPageCount,
					     1, MMU_MAP_INVALID);
            }
            if (ret < 0) {
                pr_err("rga2 map dst memory failed\n");
                status = ret;
                break;
            }

            /* change the buf address in req struct */
            req->mmu_info.dst_base_addr = ((unsigned long)MMU_Base_phys);
            req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK));

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
        rga2_dma_flush_range(MMU_Base, (MMU_Base + AllSize + 1));
        rga2_mmu_buf_get(&rga2_mmu_buf, AllSize);
	reg->MMU_len = AllSize;

        return 0;
    }
    while(0);

    return status;
}


static int rga2_mmu_info_update_palette_table_mode(struct rga2_reg *reg, struct rga2_req *req)
{
    int LutMemSize;
    unsigned long LutStart;
    unsigned long LutPageCount;
    struct page **pages = NULL;
    uint32_t uv_size, v_size;
    uint32_t AllSize;
    uint32_t *MMU_Base, *MMU_Base_phys;
    int ret, status;

    MMU_Base = NULL;
    LutPageCount = 0;
    LutMemSize = 0;
    LutStart = 0;

    do {
        /* cal lut buf mmu info */
        if (req->mmu_info.els_mmu_flag & 1) {
            req->mmu_info.src0_mmu_flag = req->mmu_info.src0_mmu_flag == 1 ? 0 : req->mmu_info.src0_mmu_flag;
            req->mmu_info.src1_mmu_flag = req->mmu_info.src1_mmu_flag == 1 ? 0 : req->mmu_info.src1_mmu_flag;
            req->mmu_info.dst_mmu_flag = req->mmu_info.dst_mmu_flag == 1 ? 0 : req->mmu_info.dst_mmu_flag;

            LutPageCount = rga2_buf_size_cal(req->pat.yrgb_addr, req->pat.uv_addr, req->pat.v_addr,
                                            req->pat.format, req->pat.vir_w, req->pat.vir_h,
                                            &LutStart);
            if(LutPageCount == 0) {
                return -EINVAL;
            }
        }

        LutMemSize = (LutPageCount + 15) & (~15);
        AllSize = LutMemSize;

        if (rga2_mmu_buf_get_try(&rga2_mmu_buf, AllSize)) {
            pr_err("RGA2 Get MMU mem failed\n");
            status = RGA2_MALLOC_ERROR;
            break;
        }

        pages = rga2_mmu_buf.pages;
        if (pages == NULL) {
            pr_err("RGA MMU malloc pages mem failed\n");
            return -EINVAL;
        }

        mutex_lock(&rga2_service.lock);
        MMU_Base = rga2_mmu_buf.buf_virtual + rga2_mmu_buf.front;
        MMU_Base_phys = rga2_mmu_buf.buf + rga2_mmu_buf.front;
        mutex_unlock(&rga2_service.lock);

        if (LutMemSize) {
            if (req->sg_els) {
                ret = rga2_MapION(req->sg_els,
                &MMU_Base[0], LutMemSize);
            } else {
                ret = rga2_MapUserMemory(&pages[0], &MMU_Base[0],
                LutStart, LutPageCount, 0, MMU_MAP_CLEAN);
            }
            if (ret < 0) {
                pr_err("rga2 map palette memory failed\n");
                status = ret;
                break;
            }

            /* change the buf address in req struct */
            req->mmu_info.els_base_addr = (((unsigned long)MMU_Base_phys));

            req->pat.yrgb_addr = (req->pat.yrgb_addr & (~PAGE_MASK));

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
        rga2_dma_flush_range(MMU_Base, (MMU_Base + AllSize));
        rga2_mmu_buf_get(&rga2_mmu_buf, AllSize);
        reg->MMU_len = AllSize;

        return 0;
    }
    while(0);

    return status;
}

/*
 * yqw:
 * This function is currently not sure whether rga2 is used,
 * because invalidate/clean cache occupies the parameter
 * reg->MMU_base, so block this function first, and re-implement
 * this function if necessary.
 */
#if 0
static int rga2_mmu_info_update_patten_buff_mode(struct rga2_reg *reg, struct rga2_req *req)
{
    int SrcMemSize, CMDMemSize;
    unsigned long SrcStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base, *MMU_p;
    int ret, status;

    MMU_Base = MMU_p = 0;

    do {
        /* cal src buf mmu info */
        SrcMemSize = rga2_mem_size_cal(req->pat.yrgb_addr, req->pat.act_w * req->pat.act_h * 4, &SrcStart);
        if(SrcMemSize == 0) {
            return -EINVAL;
        }

        /* cal cmd buf mmu info */
        CMDMemSize = rga2_mem_size_cal((unsigned long)rga2_service.cmd_buff, RGA2_CMD_BUF_SIZE, &CMDStart);
        if(CMDMemSize == 0) {
            return -EINVAL;
        }

        AllSize = SrcMemSize + CMDMemSize;

        pages = rga2_mmu_buf.pages;

        MMU_Base = kzalloc(AllSize * sizeof(uint32_t), GFP_KERNEL);
	if (MMU_Base == NULL)
		return -EINVAL;

        for(i=0; i<CMDMemSize; i++) {
            MMU_Base[i] = virt_to_phys((uint32_t *)((CMDStart + i) << PAGE_SHIFT));
        }

        if (req->src.yrgb_addr < KERNEL_SPACE_VALID)
        {
		ret = rga2_MapUserMemory(&pages[CMDMemSize],
					 &MMU_Base[CMDMemSize],
					 SrcStart, SrcMemSize,
					 1, MMU_MAP_CLEAN);
            if (ret < 0) {
                pr_err("rga map src memory failed\n");
                status = ret;
                break;
            }
        }
        else
        {
            MMU_p = MMU_Base + CMDMemSize;

            for(i=0; i<SrcMemSize; i++)
            {
                MMU_p[i] = (uint32_t)virt_to_phys((uint32_t *)((SrcStart + i) << PAGE_SHIFT));
            }
        }

        /* zsq
         * change the buf address in req struct
         * for the reason of lie to MMU
         */
        req->mmu_info.src0_base_addr = (virt_to_phys(MMU_Base) >> 2);

        req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);

        /*record the malloc buf for the cmd end to release*/
        reg->MMU_base = MMU_Base;

        /* flush data to DDR */
        rga2_dma_flush_range(MMU_Base, (MMU_Base + AllSize));
        return 0;

    }
    while(0);

    return status;
}
#endif

int rga2_set_mmu_info(struct rga2_reg *reg, struct rga2_req *req)
{
    int ret;

    if (reg->MMU_map == true) {
        ret = rga2_mmu_flush_cache(reg, req);
        return ret;
    }

    switch (req->render_mode) {
        case bitblt_mode :
            ret = rga2_mmu_info_BitBlt_mode(reg, req);
            break;
        case color_palette_mode :
            ret = rga2_mmu_info_color_palette_mode(reg, req);
            break;
        case color_fill_mode :
            ret = rga2_mmu_info_color_fill_mode(reg, req);
            break;
        case update_palette_table_mode :
            ret = rga2_mmu_info_update_palette_table_mode(reg, req);
            break;
#if 0
        case update_patten_buff_mode :
            ret = rga2_mmu_info_update_patten_buff_mode(reg, req);
            break;
#endif
        default :
            ret = -1;
            break;
    }

    return ret;
}

