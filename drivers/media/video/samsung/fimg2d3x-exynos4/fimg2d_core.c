/* drivers/media/video/samsung/fimg2d3x/fimg2d_core.c
 *
 * Copyright  2010 Samsung Electronics Co, Ltd. All Rights Reserved. 
 *		      http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file implements fimg2d core functions.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <plat/s5p-sysmmu.h>
#include <linux/sched.h>

#if defined(CONFIG_S5P_MEM_CMA)
#include <linux/cma.h>
#elif defined(CONFIG_S5P_MEM_BOOTMEM)
#include <mach/media.h>
#include <plat/media.h>
#endif

#include "fimg2d.h"

int g2d_clk_enable(struct g2d_global *g2d_dev)
{
	if(!atomic_read(&g2d_dev->clk_enable_flag)) {
		clk_enable(g2d_dev->clock);
		atomic_set(&g2d_dev->clk_enable_flag, 1);
		return 0;
	}
	return -1;
}

int g2d_clk_disable(struct g2d_global *g2d_dev)
{
	if(atomic_read(&g2d_dev->clk_enable_flag)) {
		if(atomic_read(&g2d_dev->in_use) == 0) {
			clk_disable(g2d_dev->clock);
			atomic_set(&g2d_dev->clk_enable_flag, 0);
			return 0;
		} 
	}
	return -1;
}

void g2d_sysmmu_on(struct g2d_global *g2d_dev)
{
	g2d_clk_enable(g2d_dev);
	s5p_sysmmu_enable(g2d_dev->dev,
			(unsigned long)virt_to_phys((void *)init_mm.pgd));
	g2d_clk_disable(g2d_dev);
}

void g2d_sysmmu_off(struct g2d_global *g2d_dev)
{
	g2d_clk_enable(g2d_dev);
	s5p_sysmmu_disable(g2d_dev->dev);
	g2d_clk_disable(g2d_dev);
}

void g2d_fail_debug(g2d_params *params)
{
	FIMG2D_ERROR("src : %d, %d, %d, %d / %d, %d / 0x%x, %d, 0x%x)\n",
			params->src_rect.x,
			params->src_rect.y,
			params->src_rect.w,
			params->src_rect.h,
			params->src_rect.full_w,
			params->src_rect.full_h,
			params->src_rect.color_format,
			params->src_rect.bytes_per_pixel,
			(u32)params->src_rect.addr);
	FIMG2D_ERROR("dst : %d, %d, %d, %d / %d, %d / 0x%x, %d, 0x%x)\n",
			params->dst_rect.x,
			params->dst_rect.y,
			params->dst_rect.w,
			params->dst_rect.h,
			params->dst_rect.full_w,
			params->dst_rect.full_h,
			params->dst_rect.color_format,
			params->dst_rect.bytes_per_pixel,
			(u32)params->dst_rect.addr);
	FIMG2D_ERROR("clip: %d, %d, %d, %d\n",
			params->clip.t,
			params->clip.b,
			params->clip.l,
			params->clip.r);
	FIMG2D_ERROR("flag: %d, %d, %d, %d / %d, %d, %d, %d / %d, %d, %d, %d\n",
			params->flag.rotate_val,
			params->flag.alpha_val,
			params->flag.blue_screen_mode,
			params->flag.color_key_val,
			params->flag.color_switch_val,
			params->flag.src_color,
			params->flag.third_op_mode,
			params->flag.rop_mode,
			params->flag.mask_mode,
			params->flag.render_mode,
			params->flag.potterduff_mode,
			params->flag.memory_type);
}

int g2d_init_regs(struct g2d_global *g2d_dev, g2d_params *params)
{
	u32 blt_cmd = 0;

	g2d_rect * src_rect = &params->src_rect;
	g2d_rect * dst_rect = &params->dst_rect;
	g2d_clip * clip     = &params->clip;
	g2d_flag * flag     = &params->flag;

	if (g2d_check_params(params) < 0)
		return -1;

	g2d_reset(g2d_dev);

	/* source image */	
	blt_cmd |= g2d_set_src_img(g2d_dev, src_rect, flag);    

	/* destination image */		
	blt_cmd |= g2d_set_dst_img(g2d_dev, dst_rect);

	/* rotation */
	blt_cmd |= g2d_set_rotation(g2d_dev, flag);

	/* clipping */
	blt_cmd |= g2d_set_clip_win(g2d_dev, clip);

	/* color key */
	blt_cmd |= g2d_set_color_key(g2d_dev, flag);

	/* pattern */	
	blt_cmd |= g2d_set_pattern(g2d_dev, src_rect, flag);

	/* rop & alpha blending */
	blt_cmd |= g2d_set_alpha(g2d_dev, flag);

	/* command */
	g2d_set_bitblt_cmd(g2d_dev, src_rect, dst_rect, clip, blt_cmd);

	return 0;
}

int g2d_check_overlap(g2d_rect src_rect, g2d_rect dst_rect, g2d_clip clip)
{
	unsigned int src_start_addr;
	unsigned int src_end_addr;
	unsigned int dst_start_addr;
	unsigned int dst_end_addr;

	src_start_addr = (unsigned int)GET_START_ADDR(src_rect);
	src_end_addr = src_start_addr + (unsigned int)GET_RECT_SIZE(src_rect);
	dst_start_addr = (unsigned int)GET_START_ADDR_C(dst_rect, clip);
	dst_end_addr = dst_start_addr + (unsigned int)GET_RECT_SIZE_C(dst_rect, clip);

	if ((dst_start_addr >= src_start_addr) && (dst_start_addr <= src_end_addr))
		return true;
	if ((dst_end_addr >= src_start_addr) && (dst_end_addr <= src_end_addr))
		return true;
	if ((src_start_addr >= dst_start_addr) && (src_end_addr <= dst_end_addr))
		return true;

	return false;
}

int g2d_do_blit(struct g2d_global *g2d_dev, g2d_params *params)
{
	unsigned long 	pgd;
	int need_dst_clean = true;

	if ((params->src_rect.addr == NULL) 
		|| (params->dst_rect.addr == NULL)) {
		FIMG2D_ERROR("error : addr Null\n");
		return false;
	}		

	if (params->flag.memory_type == G2D_MEMORY_KERNEL) {
#if defined(CONFIG_S5P_MEM_CMA)
		if (!cma_is_registered_region((unsigned int)params->src_rect.addr,
				GET_RECT_SIZE(params->src_rect))) {
			printk(KERN_ERR "[%s] SRC Surface is not included in CMA region\n", __func__);
			return -1;
		}
		if (!cma_is_registered_region((unsigned int)params->dst_rect.addr,
				GET_RECT_SIZE(params->dst_rect))) {
			printk(KERN_ERR "[%s] DST Surface is not included in CMA region\n", __func__);
			return -1;
		}
#endif
		params->src_rect.addr = (unsigned char *)phys_to_virt((unsigned long)params->src_rect.addr);
		params->dst_rect.addr = (unsigned char *)phys_to_virt((unsigned long)params->dst_rect.addr);
		pgd = (unsigned long)init_mm.pgd;
	} else {
		pgd = (unsigned long)current->mm->pgd;
	}

	if (params->flag.memory_type == G2D_MEMORY_USER)
	{
		g2d_clip clip_src;
		g2d_clip_for_src(&params->src_rect, &params->dst_rect, &params->clip, &clip_src);

		if (g2d_check_overlap(params->src_rect, params->dst_rect, params->clip))
			return false;

		g2d_dev->src_attribute =
			g2d_check_pagetable((unsigned char *)GET_START_ADDR(params->src_rect),
				(unsigned int)GET_RECT_SIZE(params->src_rect) + 8,
					(u32)virt_to_phys((void *)pgd));
		if (g2d_dev->src_attribute == G2D_PT_NOTVALID) {
			FIMG2D_DEBUG("Src is not in valid pagetable\n");
			return false;
		}

		g2d_dev->dst_attribute = 
			g2d_check_pagetable((unsigned char *)GET_START_ADDR_C(params->dst_rect, params->clip),
				(unsigned int)GET_RECT_SIZE_C(params->dst_rect, params->clip),
					(u32)virt_to_phys((void *)pgd));
		if (g2d_dev->dst_attribute == G2D_PT_NOTVALID) {
			FIMG2D_DEBUG("Dst is not in valid pagetable\n");
			return false;
		}

		g2d_pagetable_clean((unsigned char *)GET_START_ADDR(params->src_rect),
				(u32)GET_RECT_SIZE(params->src_rect) + 8,
				(u32)virt_to_phys((void *)pgd));
		g2d_pagetable_clean((unsigned char *)GET_START_ADDR_C(params->dst_rect, params->clip),
				(u32)GET_RECT_SIZE_C(params->dst_rect, params->clip),
				(u32)virt_to_phys((void *)pgd));

		if (params->flag.render_mode & G2D_CACHE_OP) {
			/*g2d_mem_cache_oneshot((void *)GET_START_ADDR(params->src_rect), 
				(void *)GET_START_ADDR(params->dst_rect),
				(unsigned int)GET_REAL_SIZE(params->src_rect), 
				(unsigned int)GET_REAL_SIZE(params->dst_rect));*/
		//	need_dst_clean = g2d_check_need_dst_cache_clean(params);
			g2d_mem_inner_cache(params);
			g2d_mem_outer_cache(g2d_dev, params, &need_dst_clean);
		}
	}

	s5p_sysmmu_set_tablebase_pgd(g2d_dev->dev,
					(u32)virt_to_phys((void *)pgd));

	if(g2d_init_regs(g2d_dev, params) < 0) {
		return false;
	}

	/* Do bitblit */
	g2d_start_bitblt(g2d_dev, params);

	if (!need_dst_clean)
		g2d_mem_outer_cache_inv(params);

	return true;
}

int g2d_wait_for_finish(struct g2d_global *g2d_dev, g2d_params *params)
{
	if(atomic_read(&g2d_dev->is_mmu_faulted) == 1) {
		FIMG2D_ERROR("error : sysmmu_faulted early\n");
		FIMG2D_ERROR("faulted addr: 0x%x\n", g2d_dev->faulted_addr);
		g2d_fail_debug(params);
		atomic_set(&g2d_dev->is_mmu_faulted, 0);
		return false;
	}

	if (params->flag.render_mode & G2D_POLLING) {
		g2d_check_fifo_state_wait(g2d_dev);
	} else {
		if(wait_event_interruptible_timeout(g2d_dev->waitq,
					g2d_dev->irq_handled == 1,
					msecs_to_jiffies(G2D_TIMEOUT)) == 0) {
			if(atomic_read(&g2d_dev->is_mmu_faulted) == 1) {
				FIMG2D_ERROR("error : sysmmu_faulted\n");
				FIMG2D_ERROR("faulted addr: 0x%x\n", g2d_dev->faulted_addr);
			} else {
				g2d_reset(g2d_dev);
				FIMG2D_ERROR("error : waiting for interrupt is timeout\n");
			}
			atomic_set(&g2d_dev->is_mmu_faulted, 0);
			g2d_fail_debug(params);
			return false;
		} else if(atomic_read(&g2d_dev->is_mmu_faulted) == 1) {
			FIMG2D_ERROR("error : sysmmu_faulted but auto recoveried\n");
			FIMG2D_ERROR("faulted addr: 0x%x\n", g2d_dev->faulted_addr);
			g2d_fail_debug(params);
			atomic_set(&g2d_dev->is_mmu_faulted, 0);
			return false;
		}
	}
	return true;
}

int g2d_init_mem(struct device *dev, unsigned int *base, unsigned int *size)
{
#ifdef CONFIG_S5P_MEM_CMA
	struct cma_info mem_info;
	int err;
	char cma_name[8];
#endif

#ifdef CONFIG_S5P_MEM_CMA
	/* CMA */
	sprintf(cma_name, "fimg2d");
	err = cma_info(&mem_info, dev, 0);
	FIMG2D_DEBUG("[cma_info] start_addr : 0x%x, end_addr : 0x%x, "
			"total_size : 0x%x, free_size : 0x%x\n",
			mem_info.lower_bound, mem_info.upper_bound,
			mem_info.total_size, mem_info.free_size);
	if (err) {
		FIMG2D_ERROR("%s: get cma info failed\n", __func__);
		return -1;
	}
	*size = mem_info.total_size;
	*base = (dma_addr_t)cma_alloc
		(dev, cma_name, (size_t)(*size), 0);

	FIMG2D_DEBUG("size = 0x%x\n", *size);
	FIMG2D_DEBUG("*base phys= 0x%x\n", *base);
	FIMG2D_DEBUG("*base virt = 0x%x\n", (u32)phys_to_virt(*base));

#else
	*base = s5p_get_media_memory_bank(S5P_MDEV_FIMG2D, 0);
#endif
	return 0;
}

