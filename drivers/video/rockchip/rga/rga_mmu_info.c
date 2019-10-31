/* SPDX-License-Identifier: GPL-2.0 */


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
#include <asm/memory.h>
#include <asm/atomic.h>
#include <asm/cacheflush.h>
#include "rga_mmu_info.h"
#include <linux/delay.h>

extern rga_service_info rga_service;
extern struct rga_mmu_buf_t rga_mmu_buf;

#if RGA_DEBUGFS
extern int RGA_CHECK_MODE;
#endif

#define KERNEL_SPACE_VALID    0xc0000000

static int rga_mmu_buf_get(struct rga_mmu_buf_t *t, uint32_t size)
{
    mutex_lock(&rga_service.lock);
    t->front += size;
    mutex_unlock(&rga_service.lock);

    return 0;
}

static int rga_mmu_buf_get_try(struct rga_mmu_buf_t *t, uint32_t size)
{
	int ret = 0;

	mutex_lock(&rga_service.lock);
	if ((t->back - t->front) > t->size) {
		if(t->front + size > t->back - t->size) {
			ret = -ENOMEM;
			goto out;
		}
	} else {
		if ((t->front + size) > t->back) {
			ret = -ENOMEM;
			goto out;
		}
		if (t->front + size > t->size) {
			if (size > (t->back - t->size)) {
				ret = -ENOMEM;
				goto out;
			}
			t->front = 0;
		}
	}

out:
	mutex_unlock(&rga_service.lock);
	return ret;
}

static int rga_mem_size_cal(unsigned long Mem, uint32_t MemSize, unsigned long *StartAddr)
{
    unsigned long start, end;
    uint32_t pageCount;

    end = (Mem + (MemSize + PAGE_SIZE - 1)) >> PAGE_SHIFT;
    start = Mem >> PAGE_SHIFT;
    pageCount = end - start;
    *StartAddr = start;
    return pageCount;
}

static int rga_buf_size_cal(unsigned long yrgb_addr, unsigned long uv_addr, unsigned long v_addr,
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
        case RK_FORMAT_RGBA_8888 :
            stride = (w * 4 + 3) & (~3);
            size_yrgb = stride*h;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = (size_yrgb + PAGE_SIZE - 1) >> PAGE_SHIFT;
            break;
        case RK_FORMAT_RGBX_8888 :
            stride = (w * 4 + 3) & (~3);
            size_yrgb = stride*h;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = (size_yrgb + PAGE_SIZE - 1) >> PAGE_SHIFT;
            break;
        case RK_FORMAT_RGB_888 :
            stride = (w * 3 + 3) & (~3);
            size_yrgb = stride*h;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = (size_yrgb + PAGE_SIZE - 1) >> PAGE_SHIFT;
            break;
        case RK_FORMAT_BGRA_8888 :
            size_yrgb = w*h*4;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = (size_yrgb + PAGE_SIZE - 1) >> PAGE_SHIFT;
            break;
        case RK_FORMAT_RGB_565 :
            stride = (w*2 + 3) & (~3);
            size_yrgb = stride * h;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = (size_yrgb + PAGE_SIZE - 1) >> PAGE_SHIFT;
            break;
        case RK_FORMAT_RGBA_5551 :
            stride = (w*2 + 3) & (~3);
            size_yrgb = stride * h;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = (size_yrgb + PAGE_SIZE - 1) >> PAGE_SHIFT;
            break;
        case RK_FORMAT_RGBA_4444 :
            stride = (w*2 + 3) & (~3);
            size_yrgb = stride * h;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = (size_yrgb + PAGE_SIZE - 1) >> PAGE_SHIFT;
            break;
        case RK_FORMAT_BGR_888 :
            stride = (w*3 + 3) & (~3);
            size_yrgb = stride * h;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = (size_yrgb + PAGE_SIZE - 1) >> PAGE_SHIFT;
            break;

        /* YUV FORMAT */
        case RK_FORMAT_YCbCr_422_SP :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = stride * h;
            start = MIN(yrgb_addr, uv_addr);

            start >>= PAGE_SHIFT;
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_YCbCr_422_P :
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
        case RK_FORMAT_YCbCr_420_SP :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = (stride * (h >> 1));
            start = MIN(yrgb_addr, uv_addr);
            start >>= PAGE_SHIFT;
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_YCbCr_420_P :
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

        case RK_FORMAT_YCrCb_422_SP :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = stride * h;
            start = MIN(yrgb_addr, uv_addr);
            start >>= PAGE_SHIFT;
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_YCrCb_422_P :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = ((stride >> 1) * h);
            size_v = ((stride >> 1) * h);
            start = MIN(MIN(yrgb_addr, uv_addr), v_addr);
            start >>= PAGE_SHIFT;
            end = MAX(MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv)), (v_addr + size_v));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;

        case RK_FORMAT_YCrCb_420_SP :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = (stride * (h >> 1));
            start = MIN(yrgb_addr, uv_addr);
            start >>= PAGE_SHIFT;
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_YCrCb_420_P :
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
        default :
            pageCount = 0;
            start = 0;
            break;
    }

    *StartAddr = start;
    return pageCount;
}

#if RGA_DEBUGFS
static int rga_usermemory_cheeck(struct page **pages, u32 w, u32 h, u32 format, int flag)
{
	int bits;
	void *vaddr = NULL;
	int taipage_num;
	int taidata_num;
	int *tai_vaddr = NULL;

	switch (format) {
	case RK_FORMAT_RGBA_8888:
	case RK_FORMAT_RGBX_8888:
	case RK_FORMAT_BGRA_8888:
		bits = 32;
		break;
	case RK_FORMAT_RGB_888:
	case RK_FORMAT_BGR_888:
		bits = 24;
		break;
	case RK_FORMAT_RGB_565:
	case RK_FORMAT_RGBA_5551:
	case RK_FORMAT_RGBA_4444:
	case RK_FORMAT_YCbCr_422_SP:
	case RK_FORMAT_YCbCr_422_P:
	case RK_FORMAT_YCrCb_422_SP:
	case RK_FORMAT_YCrCb_422_P:
		bits = 16;
		break;
	case RK_FORMAT_YCbCr_420_SP:
	case RK_FORMAT_YCbCr_420_P:
	case RK_FORMAT_YCrCb_420_SP:
	case RK_FORMAT_YCrCb_420_P:
		bits = 12;
		break;
	case RK_FORMAT_YCbCr_420_SP_10B:
	case RK_FORMAT_YCrCb_420_SP_10B:
		bits = 15;
		break;
	default:
		printk(KERN_DEBUG "un know format\n");
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
		printk(KERN_DEBUG "src user memory check\n");
		printk(KERN_DEBUG "tai data is %d\n", *tai_vaddr);
	} else {
		printk(KERN_DEBUG "dst user memory check\n");
		printk(KERN_DEBUG "tai data is %d\n", *tai_vaddr);
	}
	if (taidata_num == 0)
		kunmap(pages[taipage_num - 1]);
	else
		kunmap(pages[taipage_num]);
	return 0;
}
#endif

static int rga_MapUserMemory(struct page **pages,
                                            uint32_t *pageTable,
                                            unsigned long Memory,
                                            uint32_t pageCount)
{
    int32_t result;
    uint32_t i;
    uint32_t status;
    unsigned long Address;

    status = 0;
    Address = 0;

    do {
        down_read(&current->mm->mmap_sem);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
        result = get_user_pages(current, current->mm,
            Memory << PAGE_SHIFT, pageCount, 1, 0,
            pages, NULL);
#else
		result = get_user_pages_remote(current, current->mm,
			Memory << PAGE_SHIFT, pageCount, 1, pages, NULL, NULL);
#endif
        up_read(&current->mm->mmap_sem);

        #if 0
        if(result <= 0 || result < pageCount)
        {
            status = 0;

            for(i=0; i<pageCount; i++)
            {
                temp = armv7_va_to_pa((Memory + i) << PAGE_SHIFT);
                if (temp == 0xffffffff)
                {
                    printk("rga find mmu phy ddr error\n ");
                    status = RGA_OUT_OF_RESOURCES;
                    break;
                }

                pageTable[i] = temp;
            }

            return status;
        }
        #else
        if(result <= 0 || result < pageCount)
        {
            struct vm_area_struct *vma;

            if (result>0) {
			    down_read(&current->mm->mmap_sem);
			    for (i = 0; i < result; i++)
				    put_page(pages[i]);
			    up_read(&current->mm->mmap_sem);
		    }

            for(i=0; i<pageCount; i++)
            {
                vma = find_vma(current->mm, (Memory + i) << PAGE_SHIFT);

                if (vma)//&& (vma->vm_flags & VM_PFNMAP) )
                {
                    do
                    {
                        pte_t       * pte;
                        spinlock_t  * ptl;
                        unsigned long pfn;
                        pgd_t * pgd;
                        pud_t * pud;

                        pgd = pgd_offset(current->mm, (Memory + i) << PAGE_SHIFT);

                        if(pgd_val(*pgd) == 0)
                        {
                            //printk("rga pgd value is zero \n");
                            break;
                        }

                        pud = pud_offset(pgd, (Memory + i) << PAGE_SHIFT);
                        if (pud)
                        {
                            pmd_t * pmd = pmd_offset(pud, (Memory + i) << PAGE_SHIFT);
                            if (pmd)
                            {
                                pte = pte_offset_map_lock(current->mm, pmd, (Memory + i) << PAGE_SHIFT, &ptl);
                                if (!pte)
                                {
                                    pte_unmap_unlock(pte, ptl);
                                    break;
                                }
                            }
                            else
                            {
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }

                        pfn = pte_pfn(*pte);
                        Address = ((pfn << PAGE_SHIFT) | (((unsigned long)((Memory + i) << PAGE_SHIFT)) & ~PAGE_MASK));
                        pte_unmap_unlock(pte, ptl);
                    }
                    while (0);

                    pageTable[i] = Address;
                }
                else
                {
                    status = RGA_OUT_OF_RESOURCES;
                    break;
                }
            }

            return status;
        }
        #endif

        /* Fill the page table. */
        for(i=0; i<pageCount; i++)
        {
            /* Get the physical address from page struct. */
            pageTable[i] = page_to_phys(pages[i]);
        }

        down_read(&current->mm->mmap_sem);
		for (i = 0; i < result; i++)
			put_page(pages[i]);
		up_read(&current->mm->mmap_sem);

        return 0;
    }
    while(0);

    return status;
}

static int rga_MapION(struct sg_table *sg,
                               uint32_t *Memory,
                               int32_t  pageCount,
                               uint32_t offset)
{
    uint32_t i;
    uint32_t status;
    unsigned long Address;
    uint32_t mapped_size = 0;
    uint32_t len = 0;
    struct scatterlist *sgl = sg->sgl;
    uint32_t sg_num = 0;

    status = 0;
    Address = 0;
    offset = offset >> PAGE_SHIFT;
    if (offset != 0) {
        do {
            len += (sg_dma_len(sgl) >> PAGE_SHIFT);
	        if (len == offset) {
	    	    sg_num += 1;
		    break;
    	    }
    	    else {
                if (len > offset)
                     break;
    	    }
                sg_num += 1;
        }
        while((sgl = sg_next(sgl)) && (mapped_size < pageCount) && (sg_num < sg->nents));

        sgl = sg->sgl;
    	len = 0;
        do {
            len += (sg_dma_len(sgl) >> PAGE_SHIFT);
            sgl = sg_next(sgl);
        }
        while(--sg_num);

        offset -= len;

        len = sg_dma_len(sgl) >> PAGE_SHIFT;
        Address = sg_phys(sgl);
    	Address += offset;

        for(i=offset; i<len; i++) {
             Memory[i - offset] = Address + (i << PAGE_SHIFT);
        }
        mapped_size += (len - offset);
        sg_num = 1;
        sgl = sg_next(sgl);
        do {
            len = sg_dma_len(sgl) >> PAGE_SHIFT;
            Address = sg_phys(sgl);

            for(i=0; i<len; i++) {
                Memory[mapped_size + i] = Address + (i << PAGE_SHIFT);
            }

            mapped_size += len;
            sg_num += 1;
        }
        while((sgl = sg_next(sgl)) && (mapped_size < pageCount) && (sg_num < sg->nents));
    }
    else {
        do {
            len = sg_dma_len(sgl) >> PAGE_SHIFT;
            Address = sg_phys(sgl);
            for(i=0; i<len; i++) {
                Memory[mapped_size + i] = Address + (i << PAGE_SHIFT);
            }
            mapped_size += len;
            sg_num += 1;
        }
        while((sgl = sg_next(sgl)) && (mapped_size < pageCount) && (sg_num < sg->nents));
    }
    return 0;
}


static int rga_mmu_info_BitBlt_mode(struct rga_reg *reg, struct rga_req *req)
{
    int SrcMemSize, DstMemSize;
    unsigned long SrcStart, DstStart;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base, *MMU_p, *MMU_Base_phys;
    int ret;
    int status;
    uint32_t uv_size, v_size;

    struct page **pages = NULL;

    MMU_Base = NULL;

    SrcMemSize = 0;
    DstMemSize = 0;

    do {
        /* cal src buf mmu info */
        SrcMemSize = rga_buf_size_cal(req->src.yrgb_addr, req->src.uv_addr, req->src.v_addr,
                                        req->src.format, req->src.vir_w, req->src.act_h + req->src.y_offset,
                                        &SrcStart);
        if(SrcMemSize == 0) {
            return -EINVAL;
        }

        /* cal dst buf mmu info */

        DstMemSize = rga_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                        req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                        &DstStart);
        if(DstMemSize == 0)
            return -EINVAL;

        /* Cal out the needed mem size */
        SrcMemSize = (SrcMemSize + 15) & (~15);
        DstMemSize = (DstMemSize + 15) & (~15);
        AllSize = SrcMemSize + DstMemSize;

        if (rga_mmu_buf_get_try(&rga_mmu_buf, AllSize + 16)) {
            pr_err("RGA Get MMU mem failed\n");
            status = RGA_MALLOC_ERROR;
            break;
        }

        mutex_lock(&rga_service.lock);
        MMU_Base = rga_mmu_buf.buf_virtual + (rga_mmu_buf.front & (rga_mmu_buf.size - 1));
        MMU_Base_phys = rga_mmu_buf.buf + (rga_mmu_buf.front & (rga_mmu_buf.size - 1));
        mutex_unlock(&rga_service.lock);

        pages = rga_mmu_buf.pages;

        if((req->mmu_info.mmu_flag >> 8) & 1) {
            if (req->sg_src) {
                ret = rga_MapION(req->sg_src, &MMU_Base[0], SrcMemSize, req->line_draw_info.flag);
            }
            else {
                ret = rga_MapUserMemory(&pages[0], &MMU_Base[0], SrcStart, SrcMemSize);
                if (ret < 0) {
                    pr_err("rga map src memory failed\n");
                    status = ret;
                    break;
                }

#if RGA_DEBUGFS
	if (RGA_CHECK_MODE)
		rga_usermemory_cheeck(&pages[0], req->src.vir_w,
				      req->src.vir_h, req->src.format, 1);
#endif
            }
        }
        else {
            MMU_p = MMU_Base;

            if(req->src.yrgb_addr == (unsigned long)rga_service.pre_scale_buf) {
                for(i=0; i<SrcMemSize; i++)
                    MMU_p[i] = rga_service.pre_scale_buf[i];
            }
            else {
                for(i=0; i<SrcMemSize; i++)
                    MMU_p[i] = (uint32_t)((SrcStart + i) << PAGE_SHIFT);
            }
        }

        if ((req->mmu_info.mmu_flag >> 10) & 1) {
            if (req->sg_dst) {
                ret = rga_MapION(req->sg_dst, &MMU_Base[SrcMemSize], DstMemSize, req->line_draw_info.line_width);
            }
            else {
                ret = rga_MapUserMemory(&pages[SrcMemSize], &MMU_Base[SrcMemSize], DstStart, DstMemSize);
                if (ret < 0) {
                    pr_err("rga map dst memory failed\n");
                    status = ret;
                    break;
                }

#if RGA_DEBUGFS
	if (RGA_CHECK_MODE)
		rga_usermemory_cheeck(&pages[0], req->src.vir_w,
				      req->src.vir_h, req->src.format, 2);
#endif
            }
        }
        else {
            MMU_p = MMU_Base + SrcMemSize;
            for(i=0; i<DstMemSize; i++)
                MMU_p[i] = (uint32_t)((DstStart + i) << PAGE_SHIFT);
        }

        MMU_Base[AllSize] = MMU_Base[AllSize-1];

        /* zsq
         * change the buf address in req struct
         */

        req->mmu_info.base_addr = (unsigned long)MMU_Base_phys >> 2;

        uv_size = (req->src.uv_addr - (SrcStart << PAGE_SHIFT)) >> PAGE_SHIFT;
        v_size = (req->src.v_addr - (SrcStart << PAGE_SHIFT)) >> PAGE_SHIFT;

        req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK));
        req->src.uv_addr = (req->src.uv_addr & (~PAGE_MASK)) | (uv_size << PAGE_SHIFT);
        req->src.v_addr = (req->src.v_addr & (~PAGE_MASK)) | (v_size << PAGE_SHIFT);

        uv_size = (req->dst.uv_addr - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;

        req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK)) | (SrcMemSize << PAGE_SHIFT);
        req->dst.uv_addr = (req->dst.uv_addr & (~PAGE_MASK)) | ((SrcMemSize + uv_size) << PAGE_SHIFT);

        /* flush data to DDR */
        #ifdef CONFIG_ARM
        dmac_flush_range(MMU_Base, (MMU_Base + AllSize + 1));
        outer_flush_range(virt_to_phys(MMU_Base), virt_to_phys(MMU_Base + AllSize + 1));
        #elif defined(CONFIG_ARM64)
        __dma_flush_range(MMU_Base, (MMU_Base + AllSize + 1));
        #endif

        rga_mmu_buf_get(&rga_mmu_buf, AllSize + 16);
        reg->MMU_len = AllSize + 16;

        status = 0;

        return status;
    }
    while(0);

    return status;
}

static int rga_mmu_info_color_palette_mode(struct rga_reg *reg, struct rga_req *req)
{
    int SrcMemSize, DstMemSize, CMDMemSize;
    unsigned long SrcStart, DstStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base = NULL, *MMU_Base_phys = NULL;
    uint32_t *MMU_p;
    int ret, status = 0;
    uint32_t stride;

    uint8_t shift;
    uint16_t sw, byte_num;

    shift = 3 - (req->palette_mode & 3);
    sw = req->src.vir_w;
    byte_num = sw >> shift;
    stride = (byte_num + 3) & (~3);

    do {
        SrcMemSize = rga_mem_size_cal(req->src.yrgb_addr, stride, &SrcStart);
        if(SrcMemSize == 0) {
            return -EINVAL;
        }

        DstMemSize = rga_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                        req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                        &DstStart);
        if(DstMemSize == 0) {
            return -EINVAL;
        }

        CMDMemSize = rga_mem_size_cal((unsigned long)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
        if(CMDMemSize == 0) {
            return -EINVAL;
        }

        SrcMemSize = (SrcMemSize + 15) & (~15);
        DstMemSize = (DstMemSize + 15) & (~15);
        CMDMemSize = (CMDMemSize + 15) & (~15);

        AllSize = SrcMemSize + DstMemSize + CMDMemSize;

        if (rga_mmu_buf_get_try(&rga_mmu_buf, AllSize + 16)) {
            pr_err("RGA Get MMU mem failed\n");
            status = RGA_MALLOC_ERROR;
            break;
        }

        mutex_lock(&rga_service.lock);
        MMU_Base = rga_mmu_buf.buf_virtual + (rga_mmu_buf.front & (rga_mmu_buf.size - 1));
        MMU_Base_phys = rga_mmu_buf.buf + (rga_mmu_buf.front & (rga_mmu_buf.size - 1));
        mutex_unlock(&rga_service.lock);

        pages = rga_mmu_buf.pages;

        /* map CMD addr */
        for(i=0; i<CMDMemSize; i++) {
            MMU_Base[i] = (uint32_t)virt_to_phys((uint32_t *)((CMDStart + i)<<PAGE_SHIFT));
        }

        /* map src addr */
        if (req->src.yrgb_addr < KERNEL_SPACE_VALID) {
            ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], SrcStart, SrcMemSize);
            if (ret < 0) {
                pr_err("rga map src memory failed\n");
                status = ret;
                break;
            }
        }
        else {
            MMU_p = MMU_Base + CMDMemSize;

            for(i=0; i<SrcMemSize; i++)
            {
                MMU_p[i] = (uint32_t)virt_to_phys((uint32_t *)((SrcStart + i) << PAGE_SHIFT));
            }
        }

        /* map dst addr */
        if (req->src.yrgb_addr < KERNEL_SPACE_VALID) {
            ret = rga_MapUserMemory(&pages[CMDMemSize + SrcMemSize], &MMU_Base[CMDMemSize + SrcMemSize], DstStart, DstMemSize);
            if (ret < 0) {
                pr_err("rga map dst memory failed\n");
                status = ret;
                break;
            }
        }
        else {
            MMU_p = MMU_Base + CMDMemSize + SrcMemSize;
            for(i=0; i<DstMemSize; i++)
                MMU_p[i] = (uint32_t)virt_to_phys((uint32_t *)((DstStart + i) << PAGE_SHIFT));
        }


        /* zsq
         * change the buf address in req struct
         * for the reason of lie to MMU
         */
        req->mmu_info.base_addr = (virt_to_phys(MMU_Base)>>2);
        req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);
        req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK)) | ((CMDMemSize + SrcMemSize) << PAGE_SHIFT);

        /*record the malloc buf for the cmd end to release*/
        reg->MMU_base = MMU_Base;

        /* flush data to DDR */
        #ifdef CONFIG_ARM
        dmac_flush_range(MMU_Base, (MMU_Base + AllSize + 1));
        outer_flush_range(virt_to_phys(MMU_Base),virt_to_phys(MMU_Base + AllSize + 1));
        #elif defined(CONFIG_ARM64)
        __dma_flush_range(MMU_Base, (MMU_Base + AllSize + 1));
        #endif

        rga_mmu_buf_get(&rga_mmu_buf, AllSize + 16);
        reg->MMU_len = AllSize + 16;

        return status;

    }
    while(0);

    return 0;
}

static int rga_mmu_info_color_fill_mode(struct rga_reg *reg, struct rga_req *req)
{
    int DstMemSize;
    unsigned long DstStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base, *MMU_p, *MMU_Base_phys;
    int ret;
    int status;

    MMU_Base = NULL;

    do {
        DstMemSize = rga_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                        req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                        &DstStart);
        if(DstMemSize == 0) {
            return -EINVAL;
        }

        AllSize = (DstMemSize + 15) & (~15);

        pages = rga_mmu_buf.pages;

        if (rga_mmu_buf_get_try(&rga_mmu_buf, AllSize + 16)) {
            pr_err("RGA Get MMU mem failed\n");
            status = RGA_MALLOC_ERROR;
            break;
        }

        mutex_lock(&rga_service.lock);
        MMU_Base = rga_mmu_buf.buf_virtual + (rga_mmu_buf.front & (rga_mmu_buf.size - 1));
        MMU_Base_phys = rga_mmu_buf.buf + (rga_mmu_buf.front & (rga_mmu_buf.size - 1));
        mutex_unlock(&rga_service.lock);

        if (req->dst.yrgb_addr < KERNEL_SPACE_VALID) {
            if (req->sg_dst) {
                ret = rga_MapION(req->sg_dst, &MMU_Base[0], DstMemSize, req->line_draw_info.line_width);
            }
            else {
                ret = rga_MapUserMemory(&pages[0], &MMU_Base[0], DstStart, DstMemSize);
                if (ret < 0) {
                    pr_err("rga map dst memory failed\n");
                    status = ret;
                    break;
                }
            }
        }
        else {
            MMU_p = MMU_Base;
            for(i=0; i<DstMemSize; i++)
                MMU_p[i] = (uint32_t)((DstStart + i) << PAGE_SHIFT);
        }

        MMU_Base[AllSize] = MMU_Base[AllSize - 1];

        /* zsq
         * change the buf address in req struct
         */

        req->mmu_info.base_addr = ((unsigned long)(MMU_Base_phys)>>2);
        req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK));

        /*record the malloc buf for the cmd end to release*/
        reg->MMU_base = MMU_Base;

        /* flush data to DDR */
        #ifdef CONFIG_ARM
        dmac_flush_range(MMU_Base, (MMU_Base + AllSize + 1));
        outer_flush_range(virt_to_phys(MMU_Base),virt_to_phys(MMU_Base + AllSize + 1));
        #elif defined(CONFIG_ARM64)
        __dma_flush_range(MMU_Base, (MMU_Base + AllSize + 1));
        #endif

        rga_mmu_buf_get(&rga_mmu_buf, AllSize + 16);
        reg->MMU_len = AllSize + 16;

        return 0;
    }
    while(0);

    return status;
}


static int rga_mmu_info_line_point_drawing_mode(struct rga_reg *reg, struct rga_req *req)
{
    return 0;
}

static int rga_mmu_info_blur_sharp_filter_mode(struct rga_reg *reg, struct rga_req *req)
{
    return 0;
}



static int rga_mmu_info_pre_scale_mode(struct rga_reg *reg, struct rga_req *req)
{
    int SrcMemSize, DstMemSize;
    unsigned long SrcStart, DstStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base, *MMU_p, *MMU_Base_phys;
    int ret;
    int status;
    uint32_t uv_size, v_size;

    MMU_Base = NULL;

    do {
        /* cal src buf mmu info */
        SrcMemSize = rga_buf_size_cal(req->src.yrgb_addr, req->src.uv_addr, req->src.v_addr,
                                        req->src.format, req->src.vir_w, req->src.vir_h,
                                        &SrcStart);
        if(SrcMemSize == 0) {
            return -EINVAL;
        }

        /* cal dst buf mmu info */
        DstMemSize = rga_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                        req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                        &DstStart);
        if(DstMemSize == 0) {
            return -EINVAL;
        }

	    SrcMemSize = (SrcMemSize + 15) & (~15);
	    DstMemSize = (DstMemSize + 15) & (~15);

        AllSize = SrcMemSize + DstMemSize;

        pages = rga_mmu_buf.pages;

        if (rga_mmu_buf_get_try(&rga_mmu_buf, AllSize + 16)) {
            pr_err("RGA Get MMU mem failed\n");
            status = RGA_MALLOC_ERROR;
            break;
        }

        mutex_lock(&rga_service.lock);
        MMU_Base = rga_mmu_buf.buf_virtual + (rga_mmu_buf.front & (rga_mmu_buf.size - 1));
        MMU_Base_phys = rga_mmu_buf.buf + (rga_mmu_buf.front & (rga_mmu_buf.size - 1));
        mutex_unlock(&rga_service.lock);

        /* map src pages */
        if ((req->mmu_info.mmu_flag >> 8) & 1) {
            if (req->sg_src) {
                ret = rga_MapION(req->sg_src, &MMU_Base[0], SrcMemSize,req->line_draw_info.flag);
            }
            else {
                ret = rga_MapUserMemory(&pages[0], &MMU_Base[0], SrcStart, SrcMemSize);
                if (ret < 0) {
                    pr_err("rga map src memory failed\n");
                    status = ret;
                    break;
                }
            }
        }
        else {
            MMU_p = MMU_Base;

            for(i=0; i<SrcMemSize; i++)
                MMU_p[i] = (uint32_t)((SrcStart + i) << PAGE_SHIFT);
        }

        if((req->mmu_info.mmu_flag >> 10) & 1) {
            if (req->sg_dst) {
                ret = rga_MapION(req->sg_dst, &MMU_Base[SrcMemSize], DstMemSize, req->line_draw_info.line_width);
            }
            else {
                ret = rga_MapUserMemory(&pages[SrcMemSize], &MMU_Base[SrcMemSize], DstStart, DstMemSize);
                if (ret < 0) {
                    pr_err("rga map dst memory failed\n");
                    status = ret;
                    break;
                }
            }
        }
        else
        {
            /* kernel space */
            MMU_p = MMU_Base + SrcMemSize;

            if(req->dst.yrgb_addr == (unsigned long)rga_service.pre_scale_buf) {
                for(i=0; i<DstMemSize; i++)
                    MMU_p[i] = rga_service.pre_scale_buf[i];
            }
            else {
                for(i=0; i<DstMemSize; i++)
                    MMU_p[i] = (uint32_t)((DstStart + i) << PAGE_SHIFT);
            }
        }

        MMU_Base[AllSize] = MMU_Base[AllSize];

        /* zsq
         * change the buf address in req struct
         * for the reason of lie to MMU
         */

        req->mmu_info.base_addr = ((unsigned long)(MMU_Base_phys)>>2);

        uv_size = (req->src.uv_addr - (SrcStart << PAGE_SHIFT)) >> PAGE_SHIFT;
        v_size = (req->src.v_addr - (SrcStart << PAGE_SHIFT)) >> PAGE_SHIFT;

        req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK));
        req->src.uv_addr = (req->src.uv_addr & (~PAGE_MASK)) | (uv_size << PAGE_SHIFT);
        req->src.v_addr = (req->src.v_addr & (~PAGE_MASK)) | (v_size << PAGE_SHIFT);

        uv_size = (req->dst.uv_addr - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;
        v_size = (req->dst.v_addr - (DstStart << PAGE_SHIFT)) >> PAGE_SHIFT;

        req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK)) | ((SrcMemSize) << PAGE_SHIFT);
        req->dst.uv_addr = (req->dst.uv_addr & (~PAGE_MASK)) | ((SrcMemSize + uv_size) << PAGE_SHIFT);
        req->dst.v_addr = (req->dst.v_addr & (~PAGE_MASK)) | ((SrcMemSize + v_size) << PAGE_SHIFT);

        /*record the malloc buf for the cmd end to release*/
        reg->MMU_base = MMU_Base;

        /* flush data to DDR */
        #ifdef CONFIG_ARM
        dmac_flush_range(MMU_Base, (MMU_Base + AllSize + 1));
        outer_flush_range(virt_to_phys(MMU_Base),virt_to_phys(MMU_Base + AllSize + 1));
        #elif defined(CONFIG_ARM64)
        __dma_flush_range(MMU_Base, (MMU_Base + AllSize + 1));
        #endif

	    rga_mmu_buf_get(&rga_mmu_buf, AllSize + 16);
        reg->MMU_len = AllSize + 16;

        return 0;
    }
    while(0);

    return status;
}


static int rga_mmu_info_update_palette_table_mode(struct rga_reg *reg, struct rga_req *req)
{
    int SrcMemSize, CMDMemSize;
    unsigned long SrcStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base, *MMU_p;
    int ret, status;

    MMU_Base = NULL;

    do {
        /* cal src buf mmu info */
        SrcMemSize = rga_mem_size_cal(req->src.yrgb_addr, req->src.vir_w * req->src.vir_h, &SrcStart);
        if(SrcMemSize == 0) {
            return -EINVAL;
        }

        /* cal cmd buf mmu info */
        CMDMemSize = rga_mem_size_cal((unsigned long)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
        if(CMDMemSize == 0) {
            return -EINVAL;
        }

        AllSize = SrcMemSize + CMDMemSize;

        pages = kzalloc(AllSize * sizeof(struct page *), GFP_KERNEL);
        if(pages == NULL) {
            pr_err("RGA MMU malloc pages mem failed\n");
            status = RGA_MALLOC_ERROR;
            break;
        }

        MMU_Base = kzalloc((AllSize + 1)* sizeof(uint32_t), GFP_KERNEL);
        if(pages == NULL) {
            pr_err("RGA MMU malloc MMU_Base point failed\n");
            status = RGA_MALLOC_ERROR;
            break;
        }

        for(i=0; i<CMDMemSize; i++) {
            MMU_Base[i] = (uint32_t)virt_to_phys((uint32_t *)((CMDStart + i) << PAGE_SHIFT));
        }

        if (req->src.yrgb_addr < KERNEL_SPACE_VALID)
        {
            ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], SrcStart, SrcMemSize);
            if (ret < 0) {
                pr_err("rga map src memory failed\n");
                return -EINVAL;
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
        req->mmu_info.base_addr = (virt_to_phys(MMU_Base) >> 2);

        req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);

        /*record the malloc buf for the cmd end to release*/
        reg->MMU_base = MMU_Base;

        /* flush data to DDR */
        #ifdef CONFIG_ARM
        dmac_flush_range(MMU_Base, (MMU_Base + AllSize));
        outer_flush_range(virt_to_phys(MMU_Base),virt_to_phys(MMU_Base + AllSize));
        #elif defined(CONFIG_ARM64)
        __dma_flush_range(MMU_Base, (MMU_Base + AllSize));
        #endif


        if (pages != NULL) {
            /* Free the page table */
            kfree(pages);
        }

        return 0;
    }
    while(0);

    if (pages != NULL)
        kfree(pages);

    if (MMU_Base != NULL)
        kfree(MMU_Base);

    return status;
}

static int rga_mmu_info_update_patten_buff_mode(struct rga_reg *reg, struct rga_req *req)
{
    int SrcMemSize, CMDMemSize;
    unsigned long SrcStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base, *MMU_p;
    int ret, status;

    MMU_Base = MMU_p = 0;

    do
    {

        /* cal src buf mmu info */
        SrcMemSize = rga_mem_size_cal(req->pat.yrgb_addr, req->pat.vir_w * req->pat.vir_h * 4, &SrcStart);
        if(SrcMemSize == 0) {
            return -EINVAL;
        }

        /* cal cmd buf mmu info */
        CMDMemSize = rga_mem_size_cal((unsigned long)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
        if(CMDMemSize == 0) {
            return -EINVAL;
        }

        AllSize = SrcMemSize + CMDMemSize;

        pages = kzalloc(AllSize * sizeof(struct page *), GFP_KERNEL);
        if(pages == NULL) {
            pr_err("RGA MMU malloc pages mem failed\n");
            status = RGA_MALLOC_ERROR;
            break;
        }

        MMU_Base = kzalloc(AllSize * sizeof(uint32_t), GFP_KERNEL);
        if(MMU_Base == NULL) {
            pr_err("RGA MMU malloc MMU_Base point failed\n");
            status = RGA_MALLOC_ERROR;
            break;
        }

        for(i=0; i<CMDMemSize; i++) {
            MMU_Base[i] = virt_to_phys((uint32_t *)((CMDStart + i) << PAGE_SHIFT));
        }

        if (req->src.yrgb_addr < KERNEL_SPACE_VALID)
        {
            ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], SrcStart, SrcMemSize);
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
        req->mmu_info.base_addr = (virt_to_phys(MMU_Base) >> 2);

        req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);

        /*record the malloc buf for the cmd end to release*/
        reg->MMU_base = MMU_Base;

        /* flush data to DDR */
        #ifdef CONFIG_ARM
        dmac_flush_range(MMU_Base, (MMU_Base + AllSize));
        outer_flush_range(virt_to_phys(MMU_Base),virt_to_phys(MMU_Base + AllSize));
        #elif defined(CONFIG_ARM64)
        __dma_flush_range(MMU_Base, (MMU_Base + AllSize));
        #endif

        if (pages != NULL) {
            /* Free the page table */
            kfree(pages);
        }

        return 0;

    }
    while(0);

    if (pages != NULL)
        kfree(pages);

    if (MMU_Base != NULL)
        kfree(MMU_Base);

    return status;
}

int rga_set_mmu_info(struct rga_reg *reg, struct rga_req *req)
{
    int ret;

    switch (req->render_mode) {
        case bitblt_mode :
            ret = rga_mmu_info_BitBlt_mode(reg, req);
            break;
        case color_palette_mode :
            ret = rga_mmu_info_color_palette_mode(reg, req);
            break;
        case color_fill_mode :
            ret = rga_mmu_info_color_fill_mode(reg, req);
            break;
        case line_point_drawing_mode :
            ret = rga_mmu_info_line_point_drawing_mode(reg, req);
            break;
        case blur_sharp_filter_mode :
            ret = rga_mmu_info_blur_sharp_filter_mode(reg, req);
            break;
        case pre_scaling_mode :
            ret = rga_mmu_info_pre_scale_mode(reg, req);
            break;
        case update_palette_table_mode :
            ret = rga_mmu_info_update_palette_table_mode(reg, req);
            break;
        case update_patten_buff_mode :
            ret = rga_mmu_info_update_patten_buff_mode(reg, req);
            break;
        default :
            ret = -1;
            break;
    }

    return ret;
}

