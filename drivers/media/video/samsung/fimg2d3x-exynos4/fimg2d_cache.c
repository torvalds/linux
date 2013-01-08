/* drivers/media/video/samsung/fimg2d3x/fimg2d3x_cache.c
 *
 * Copyright  2010 Samsung Electronics Co, Ltd. All Rights Reserved. 
 *		      http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file implements fimg2d cache control functions.
 */

#include <linux/kernel.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <linux/sched.h>
#include <linux/poll.h>

#include "fimg2d.h"

#define L1_ALL_THRESHOLD_SIZE SZ_64K
#define L2_ALL_THRESHOLD_SIZE SZ_1M

#define L2_CACHE_SKIP_MARK 256*4

void g2d_pagetable_clean(const void *start_addr, unsigned long size, unsigned long pgd)
{
	void *l1d_vir, *l1d_phy, *l2d_phy;
	void *cur_addr, *end_addr;
	size = ALIGN(size, SZ_1M);
	cur_addr = (void *)((unsigned long)start_addr & ~(SZ_1M-1));
	end_addr = cur_addr + size + SZ_1M;
    
	l1d_phy = (void *)((pgd & 0xffffc000) | (((u32)(cur_addr) & 0xfff00000)>>18)); 
	
	if (l1d_phy) {
		l1d_vir = phys_to_virt((u32)l1d_phy);
		dmac_map_area(l1d_vir, (size/SZ_1M)*4, DMA_TO_DEVICE);
	}
	
	while (cur_addr < end_addr) {
		outer_clean_range((u32)l1d_phy, (u32)l1d_phy + 4);

		if (l1d_phy) {			
			l2d_phy = (void *)((readl(phys_to_virt((u32)l1d_phy)) & 0xfffffc00) | 
				(((u32)cur_addr & 0x000ff000) >> 10));
			if (l2d_phy)
				dmac_map_area(phys_to_virt((u32)l2d_phy), SZ_1K, DMA_TO_DEVICE);  
			outer_clean_range((u32)l2d_phy, (u32)l2d_phy + SZ_1K);
		}
		cur_addr += SZ_1M; 
		l1d_phy = (void *)((pgd & 0xffffc000) | (((u32)(cur_addr) & 0xfff00000)>>18));
	}  
}


static unsigned long virt2phys(unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	if(!current->mm) {
		current->mm = &init_mm;
	}

	pgd = pgd_offset(current->mm, addr);

	if ((pgd_val(*pgd) & 0x1) != 0x1) {
		return 0;
	}
	
	pmd = pmd_offset(pgd, addr);
	pte = pte_offset_map(pmd, addr);

	return (addr & 0xfff) | (pte_val(*pte) & 0xfffff000);
}

u32 g2d_check_pagetable(void * vaddr, unsigned int size, unsigned long pgd)
{
    unsigned int level_one_phy, level_two_phy;
    unsigned int level_one_value, level_two_value; 
    
    for (;;) {
	level_one_phy = (pgd & 0xffffc000) | (((u32)vaddr & 0xfff00000)>>18);
	if ((int)phys_to_virt(level_one_phy) < 0xc0000000) {
		FIMG2D_ERROR("Level1 page table mapping missed, missed address = %p", phys_to_virt(level_one_phy));
		return G2D_PT_NOTVALID;
	}
	level_one_value = readl(phys_to_virt(level_one_phy));

	level_two_phy = (level_one_value & 0xfffffc00) | (((u32)vaddr & 0x000ff000) >> 10);
	if ((int)phys_to_virt(level_two_phy) < 0xc0000000) {
		FIMG2D_ERROR("Level2 page table mapping missed, missed address = %p", phys_to_virt(level_two_phy));
		return G2D_PT_NOTVALID;
	}
	level_two_value = readl(phys_to_virt(level_two_phy));

	if (((level_one_value & 0x3) != 0x1) || ((level_two_value & 0x3) != 0x3)) {
		FIMG2D_DEBUG("Surface memory mapping fail [L1: 0x%x, L2: 0x%x]\n",
			level_one_value, level_two_value);
		return G2D_PT_NOTVALID;
	}
	if (size == 0) {
		if  ((level_two_value & 0x08) != 0x08)
			return G2D_PT_UNCACHED;
		return G2D_PT_CACHED;
	}
    
	if (size <= PAGE_SIZE) {
		vaddr += (size-1);
		size = 0;
	} else {
		vaddr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
    }
}

void g2d_clip_for_src(g2d_rect *src_rect, g2d_rect *dst_rect, g2d_clip *clip, g2d_clip *src_clip)
{
	if ((src_rect->w == dst_rect->w) && (src_rect->h == dst_rect->h)) {
		src_clip->t = src_rect->y + (clip->t - dst_rect->y);
		src_clip->l = src_rect->x + (clip->l - dst_rect->x);
		src_clip->b = src_clip->t + (clip->b - clip->t);
		src_clip->r = src_clip->l + (clip->r - clip->l);
	} else {
		src_clip->t = src_rect->y;
		src_clip->l = src_rect->x;
		src_clip->b = src_clip->t + src_rect->h;
		src_clip->r = src_clip->l + src_rect->w;
	}
}

void g2d_mem_inner_cache(g2d_params * params)
{
	void *src_addr, *dst_addr;
	unsigned long src_size, dst_size;
	g2d_clip clip_src;
	g2d_clip_for_src(&params->src_rect, &params->dst_rect, &params->clip, &clip_src);

	src_addr = (void *)GET_START_ADDR_C(params->src_rect, clip_src); 
	dst_addr = (void *)GET_START_ADDR_C(params->dst_rect, params->clip);
	src_size = (unsigned long)GET_RECT_SIZE_C(params->src_rect, clip_src); 
	dst_size = (unsigned long)GET_RECT_SIZE_C(params->dst_rect, params->clip);

	if((src_size + dst_size) < L1_ALL_THRESHOLD_SIZE) {
		dmac_map_area(src_addr, src_size, DMA_TO_DEVICE);
		dmac_flush_range(dst_addr, dst_addr + dst_size);
	} else {
		flush_all_cpu_caches();
	}
}

void g2d_mem_outer_cache(struct g2d_global *g2d_dev, g2d_params * params, int *need_dst_clean)
{
 	unsigned long start_paddr, end_paddr;
	unsigned long cur_addr, end_addr;
	unsigned long width_bytes;
	unsigned long stride;
	unsigned long src_size, dst_size;

#if 0
	if (((GET_RECT_SIZE(params->src_rect) + GET_RECT_SIZE(params->dst_rect)) > L2_ALL_THRESHOLD_SIZE)
		&& ((*need_dst_clean == true) || ( GET_RECT_SIZE(params->src_rect) > 384*640*4))) {
		outer_flush_all();
		*need_dst_clean = true;
		return;
	}	
#endif

	g2d_clip clip_src;
	g2d_clip_for_src(&params->src_rect, &params->dst_rect, &params->clip, &clip_src);

	src_size = GET_RECT_SIZE_C(params->src_rect, clip_src);
	dst_size = GET_RECT_SIZE_C(params->dst_rect, params->clip);

	if ((src_size + dst_size) >= L2_ALL_THRESHOLD_SIZE) {
		outer_flush_all();
		*need_dst_clean = true;
		return;
	}

	if((GET_SPARE_BYTES(params->src_rect) < L2_CACHE_SKIP_MARK) 
		|| ((params->src_rect.w * params->src_rect.bytes_per_pixel) >= PAGE_SIZE)) {
		g2d_mem_outer_cache_clean((void *)GET_START_ADDR_C(params->src_rect, clip_src), 
			(unsigned int)GET_RECT_SIZE_C(params->src_rect, clip_src));
	} else {
		stride = GET_STRIDE(params->src_rect);
		width_bytes = params->src_rect.w * params->src_rect.bytes_per_pixel;
		cur_addr = (unsigned long)GET_REAL_START_ADDR_C(params->src_rect, clip_src);
		end_addr = (unsigned long)GET_REAL_END_ADDR_C(params->src_rect, clip_src);

		while (cur_addr <= end_addr) {
			start_paddr = virt2phys((unsigned long)cur_addr);
			end_paddr = virt2phys((unsigned long)cur_addr + width_bytes);
			
			if (((end_paddr - start_paddr) > 0) && ((end_paddr -start_paddr) < PAGE_SIZE)) {
				outer_clean_range(start_paddr, end_paddr);
			} else {
				outer_clean_range(start_paddr, ((start_paddr + PAGE_SIZE) & PAGE_MASK) - 1);
				outer_clean_range(end_paddr & PAGE_MASK, end_paddr);			
			}
			cur_addr += stride;
		}
	}

	if (*need_dst_clean) {
		if ((GET_SPARE_BYTES(params->dst_rect) < L2_CACHE_SKIP_MARK)
			|| ((params->dst_rect.w * params->src_rect.bytes_per_pixel) >= PAGE_SIZE)) {		
			g2d_mem_outer_cache_flush((void *)GET_START_ADDR_C(params->dst_rect, params->clip), 
				(unsigned int)GET_RECT_SIZE_C(params->dst_rect, params->clip));
		} else {
			stride = GET_STRIDE(params->dst_rect);
			width_bytes = (params->clip.r - params->clip.l) * params->dst_rect.bytes_per_pixel;
			
			cur_addr = (unsigned long)GET_REAL_START_ADDR_C(params->dst_rect, params->clip);
			end_addr = (unsigned long)GET_REAL_END_ADDR_C(params->dst_rect, params->clip);

			while (cur_addr <= end_addr) {
				start_paddr = virt2phys((unsigned long)cur_addr);
				end_paddr = virt2phys((unsigned long)cur_addr + width_bytes);
				
				if (((end_paddr - start_paddr) > 0) && ((end_paddr -start_paddr) < PAGE_SIZE)) {
					outer_flush_range(start_paddr, end_paddr);
				} else {
					outer_flush_range(start_paddr, ((start_paddr + PAGE_SIZE) & PAGE_MASK) - 1);
					outer_flush_range(end_paddr & PAGE_MASK, end_paddr);
				}
				cur_addr += stride;
			}	
		}
	}
}

void g2d_mem_cache_oneshot(void *src_addr,  void *dst_addr, unsigned long src_size, unsigned long dst_size)
{
 	unsigned long paddr;
        	void *cur_addr, *end_addr;
	unsigned long full_size;

	full_size = src_size + dst_size;

	if(full_size < L1_ALL_THRESHOLD_SIZE)
		dmac_map_area(src_addr, src_size, DMA_TO_DEVICE);
	else
		flush_all_cpu_caches();

	if(full_size > L2_ALL_THRESHOLD_SIZE) {
		outer_flush_all();
		return;
	}

	cur_addr = (void *)((unsigned long)src_addr & PAGE_MASK);
	src_size = PAGE_ALIGN(src_size);
	end_addr = cur_addr + src_size + PAGE_SIZE;

	while (cur_addr < end_addr) {
		paddr = virt2phys((unsigned long)cur_addr);
		if (paddr) {            
			outer_clean_range(paddr, paddr + PAGE_SIZE);
		}
		cur_addr += PAGE_SIZE;
	}   

	if(full_size < L1_ALL_THRESHOLD_SIZE)
		dmac_flush_range(dst_addr, dst_addr + dst_size);

	cur_addr = (void *)((unsigned long)dst_addr & PAGE_MASK);
	dst_size = PAGE_ALIGN(dst_size);
	end_addr = cur_addr + dst_size + PAGE_SIZE;

	while (cur_addr < end_addr) {
		paddr = virt2phys((unsigned long)cur_addr);
		if (paddr) {            
			outer_flush_range(paddr, paddr + PAGE_SIZE);
		}
		cur_addr += PAGE_SIZE;
	}
}

u32 g2d_mem_cache_op(unsigned int cmd, void *addr, unsigned int size)
{
	switch(cmd) {
	case G2D_DMA_CACHE_CLEAN :
		g2d_mem_outer_cache_clean((void *)addr, size);
		break;	
	case G2D_DMA_CACHE_FLUSH :
		g2d_mem_outer_cache_flush((void *)addr, size);
		break;
	default :
		return false;
		break;		
	}	

	return true;
}

void g2d_mem_outer_cache_flush(void *start_addr, unsigned long size)
{
	unsigned long paddr;
	void *cur_addr, *end_addr;

	cur_addr = (void *)((unsigned long)start_addr & PAGE_MASK);
	size = PAGE_ALIGN(size);
	end_addr = cur_addr + size + PAGE_SIZE;

	while (cur_addr < end_addr) {
		paddr = virt2phys((unsigned long)cur_addr);
		if (paddr) {            
			outer_flush_range(paddr, paddr + PAGE_SIZE);
		}
	cur_addr += PAGE_SIZE;
	}   
}

void g2d_mem_outer_cache_clean(const void *start_addr, unsigned long size)
{
	unsigned long paddr;
	void *cur_addr, *end_addr;

	cur_addr = (void *)((unsigned long)start_addr & PAGE_MASK);
	size = PAGE_ALIGN(size);
	end_addr = cur_addr + size + PAGE_SIZE;

	while (cur_addr < end_addr) {
		paddr = virt2phys((unsigned long)cur_addr);
		if (paddr) {
			outer_clean_range(paddr, paddr + PAGE_SIZE);
		}
		cur_addr += PAGE_SIZE;
	}
}

void g2d_mem_outer_cache_inv(g2d_params *params)
{
 	unsigned long start_paddr, end_paddr;
	unsigned long cur_addr, end_addr;
	unsigned long stride;
	
	stride = GET_STRIDE(params->dst_rect);
	cur_addr = (unsigned long)GET_START_ADDR_C(params->dst_rect, params->clip);
	end_addr = cur_addr + (unsigned long)GET_RECT_SIZE_C(params->dst_rect, params->clip);

	start_paddr = virt2phys((unsigned long)cur_addr);
	outer_inv_range(start_paddr, (start_paddr & PAGE_MASK) + (PAGE_SIZE - 1));
	cur_addr = ((unsigned long)cur_addr & PAGE_MASK) + PAGE_SIZE;

	while (cur_addr < end_addr) {
		start_paddr = virt2phys((unsigned long)cur_addr);		
		if ((cur_addr + PAGE_SIZE) > end_addr) {		
			end_paddr = virt2phys((unsigned long)end_addr);
			outer_inv_range(start_paddr, end_paddr);
			break;
		}

		if (start_paddr) {
			outer_inv_range(start_paddr, start_paddr + PAGE_SIZE);
		}
		cur_addr += PAGE_SIZE;
	}
}

int g2d_check_need_dst_cache_clean(g2d_params * params)
{
	unsigned long cur_addr, end_addr;
	cur_addr = (unsigned long)GET_START_ADDR_C(params->dst_rect, params->clip);
	end_addr = cur_addr + (unsigned long)GET_RECT_SIZE_C(params->dst_rect, params->clip);
	
	if ((params->src_rect.color_format == G2D_RGB_565) &&
		(params->flag.alpha_val == G2D_ALPHA_BLENDING_OPAQUE) &&
		(params->dst_rect.full_w == (params->clip.r - params->clip.l)) &&
		(cur_addr % 32 == 0) && (end_addr % 32 == 0))   {
		return false;
	}
	
	return true;
}
