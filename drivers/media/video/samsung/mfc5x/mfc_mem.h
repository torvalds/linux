/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_mem.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Memory manager for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MFC_MEM_H_
#define __MFC_MEM_H_ __FILE__

#include "mfc.h"
#include "mfc_dev.h"

#ifdef CONFIG_VIDEO_MFC_VCM_UMP
#include <plat/s5p-vcm.h>
#endif

#ifdef CONFIG_S5P_VMEM
#include <linux/dma-mapping.h>

extern unsigned int s5p_vmem_vmemmap(size_t size, unsigned long va_start,
		unsigned long va_end);
extern void s5p_vfree(unsigned int cookie);
extern unsigned int s5p_getcookie(void *addr);
extern void *s5p_getaddress(unsigned int cookie);
extern void s5p_vmem_dmac_map_area(const void *start_addr,
		unsigned long size, int dir);
#endif

#ifdef CONFIG_VIDEO_MFC_VCM_UMP
struct vcm_res;

struct vcm_mmu_res {
	struct vcm_res			res;
	struct list_head		bound;
};
#endif

int mfc_mem_count(void);
unsigned long mfc_mem_base(int port);
unsigned char *mfc_mem_addr(int port);
unsigned long mfc_mem_data_base(int port);
unsigned int mfc_mem_data_size(int port);
#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
unsigned int mfc_mem_hole_size(void);
#endif
unsigned long mfc_mem_data_ofs(unsigned long addr, int contig);
unsigned long mfc_mem_base_ofs(unsigned long addr);
unsigned long mfc_mem_addr_ofs(unsigned long ofs, int port);

void mfc_mem_cache_clean(const void *start_addr, unsigned long size);
void mfc_mem_cache_inv(const void *start_addr, unsigned long size);

int mfc_init_mem_mgr(struct mfc_dev *dev);
void mfc_final_mem_mgr(struct mfc_dev *dev);

#ifdef CONFIG_VIDEO_MFC_VCM_UMP
void mfc_vcm_dump_res(struct vcm_res *res);
struct vcm_mmu_res *mfc_vcm_bind(unsigned int addr, unsigned int size);
void mfc_vcm_unbind(struct vcm_mmu_res *s_res, int flag);
struct vcm_res *mfc_vcm_map(struct vcm_phys *phys);
void mfc_vcm_unmap(struct vcm_res *res);
void *mfc_ump_map(struct vcm_phys *phys, unsigned long vcminfo);
void mfc_ump_unmap(void *handle);
unsigned int mfc_ump_get_id(void *handle);
unsigned long mfc_ump_get_virt(unsigned int secure_id);
#endif

#endif /* __MFC_MEM_H_ */
