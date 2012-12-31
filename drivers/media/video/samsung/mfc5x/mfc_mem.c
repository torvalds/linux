/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_mem.c
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

#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#ifdef CONFIG_ARCH_EXYNOS4
#include <mach/media.h>
#endif
#include <plat/media.h>

#ifndef CONFIG_S5P_VMEM
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>

#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#endif

#ifdef CONFIG_S5P_MEM_CMA
#include <linux/cma.h>
#endif

#ifdef CONFIG_VIDEO_MFC_VCM_UMP
#include <plat/s5p-vcm.h>

#include "ump_kernel_interface.h"
#include "ump_kernel_interface_ref_drv.h"
#include "ump_kernel_interface_vcm.h"
#endif

#include "mfc_mem.h"
#include "mfc_buf.h"
#include "mfc_log.h"
#include "mfc_pm.h"

static int mem_ports = -1;
#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
static struct mfc_mem mem_infos[MFC_MAX_MEM_CHUNK_NUM];
#else
static struct mfc_mem mem_infos[MFC_MAX_MEM_PORT_NUM];
#endif

#ifdef CONFIG_VIDEO_MFC_VCM_UMP
static struct mfc_vcm vcm_info;
#endif

#ifndef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
static int mfc_mem_addr_port(unsigned long addr)
{
	int i;
	int port = -1;

	for (i = 0; i < mem_ports; i++) {
		if ((addr >= mem_infos[i].base)
		 && (addr < (mem_infos[i].base + mem_infos[i].size))) {
			port = i;
			break;
		}
	}

	return port;
}
#endif

int mfc_mem_count(void)
{
	return mem_ports;
}

unsigned long mfc_mem_base(int port)
{
	if ((port < 0) || (port >= mem_ports))
		return 0;

	return mem_infos[port].base;
}

unsigned char *mfc_mem_addr(int port)
{
	if ((port < 0) || (port >= mem_ports))
		return 0;

	return mem_infos[port].addr;
}

unsigned long mfc_mem_data_base(int port)
{
	unsigned long addr;

#ifndef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	if ((port < 0) || (port >= mem_ports))
		return 0;
#endif
	if (port == 0)
		addr = mem_infos[port].base + MFC_FW_SYSTEM_SIZE;
	else
		addr = mem_infos[port].base;

	return addr;
}

unsigned int mfc_mem_data_size(int port)
{
	unsigned int size;

#ifndef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	if ((port < 0) || (port >= mem_ports))
		return 0;
#endif
	if (port == 0)
		size = mem_infos[port].size - MFC_FW_SYSTEM_SIZE;
	else
		size = mem_infos[port].size;

	return size;
}

#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
unsigned int mfc_mem_hole_size(void)
{
	if (mfc_mem_data_size(1))
		return mfc_mem_data_base(1) -
			(mfc_mem_data_base(0) + mfc_mem_data_size(0));
	else
		return 0;
}
#endif

unsigned long mfc_mem_data_ofs(unsigned long addr, int contig)
{
	unsigned int offset;
	int i;
	int port;

#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	port = 0;
#else
	port = mfc_mem_addr_port(addr);
#endif
	if (port < 0)
		return 0;

	offset = addr - mfc_mem_data_base(port);

	if (contig) {
		for (i = 0; i < port; i++)
			offset += mfc_mem_data_size(i);
	}

	return offset;
}

unsigned long mfc_mem_base_ofs(unsigned long addr)
{
	int port;

#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	port = 0;
#else
	port = mfc_mem_addr_port(addr);
#endif
	if (port < 0)
		return 0;

	return addr - mem_infos[port].base;
}

unsigned long mfc_mem_addr_ofs(unsigned long ofs, int port)
{
	/* FIXME: right position? */
	if (port > (mfc_mem_count() - 1))
		port = mfc_mem_count() - 1;

	return mem_infos[port].base + ofs;
}

#ifdef SYSMMU_MFC_ON
#ifdef CONFIG_S5P_VMEM
void mfc_mem_cache_clean(const void *start_addr, unsigned long size)
{
	s5p_vmem_dmac_map_area(start_addr, size, DMA_TO_DEVICE);
}

void mfc_mem_cache_inv(const void *start_addr, unsigned long size)
{
	s5p_vmem_dmac_map_area(start_addr, size, DMA_FROM_DEVICE);
}
#else /* CONFIG_VIDEO_MFC_VCM_UMP or kernel virtual memory allocator */
void mfc_mem_cache_clean(const void *start_addr, unsigned long size)
{
	unsigned long paddr;
	void *cur_addr, *end_addr;

	dmac_map_area(start_addr, size, DMA_TO_DEVICE);

	cur_addr = (void *)((unsigned long)start_addr & PAGE_MASK);
	end_addr = cur_addr + PAGE_ALIGN(size);

	while (cur_addr < end_addr) {
		paddr = page_to_pfn(vmalloc_to_page(cur_addr));
		paddr <<= PAGE_SHIFT;
		if (paddr)
			outer_clean_range(paddr, paddr + PAGE_SIZE);
		cur_addr += PAGE_SIZE;
	}

	/* FIXME: L2 operation optimization */
	/*
	unsigned long start, end, unitsize;
	unsigned long cur_addr, remain;

	dmac_map_area(start_addr, size, DMA_TO_DEVICE);

	cur_addr = (unsigned long)start_addr;
	remain = size;

	start = page_to_pfn(vmalloc_to_page(cur_addr));
	start <<= PAGE_SHIFT;
	if (start & PAGE_MASK) {
		unitsize = min((start | PAGE_MASK) - start + 1, remain);
		end = start + unitsize;
		outer_clean_range(start, end);
		remain -= unitsize;
		cur_addr += unitsize;
	}

	while (remain >= PAGE_SIZE) {
		start = page_to_pfn(vmalloc_to_page(cur_addr));
		start <<= PAGE_SHIFT;
		end = start + PAGE_SIZE;
		outer_clean_range(start, end);
		remain -= PAGE_SIZE;
		cur_addr += PAGE_SIZE;
	}

	if (remain) {
		start = page_to_pfn(vmalloc_to_page(cur_addr));
		start <<= PAGE_SHIFT;
		end = start + remain;
		outer_clean_range(start, end);
	}
	*/

}

void mfc_mem_cache_inv(const void *start_addr, unsigned long size)
{
	unsigned long paddr;
	void *cur_addr, *end_addr;

	cur_addr = (void *)((unsigned long)start_addr & PAGE_MASK);
	end_addr = cur_addr + PAGE_ALIGN(size);

	while (cur_addr < end_addr) {
		paddr = page_to_pfn(vmalloc_to_page(cur_addr));
		paddr <<= PAGE_SHIFT;
		if (paddr)
			outer_inv_range(paddr, paddr + PAGE_SIZE);
		cur_addr += PAGE_SIZE;
	}

	dmac_unmap_area(start_addr, size, DMA_FROM_DEVICE);

	/* FIXME: L2 operation optimization */
	/*
	unsigned long start, end, unitsize;
	unsigned long cur_addr, remain;

	cur_addr = (unsigned long)start_addr;
	remain = size;

	start = page_to_pfn(vmalloc_to_page(cur_addr));
	start <<= PAGE_SHIFT;
	if (start & PAGE_MASK) {
		unitsize = min((start | PAGE_MASK) - start + 1, remain);
		end = start + unitsize;
		outer_inv_range(start, end);
		remain -= unitsize;
		cur_addr += unitsize;
	}

	while (remain >= PAGE_SIZE) {
		start = page_to_pfn(vmalloc_to_page(cur_addr));
		start <<= PAGE_SHIFT;
		end = start + PAGE_SIZE;
		outer_inv_range(start, end);
		remain -= PAGE_SIZE;
		cur_addr += PAGE_SIZE;
	}

	if (remain) {
		start = page_to_pfn(vmalloc_to_page(cur_addr));
		start <<= PAGE_SHIFT;
		end = start + remain;
		outer_inv_range(start, end);
	}

	dmac_unmap_area(start_addr, size, DMA_FROM_DEVICE);
	*/
}
#endif	/* end of CONFIG_S5P_VMEM */
#else	/* not SYSMMU_MFC_ON */
	/* early allocator */
	/* CMA or bootmem(memblock) */
void mfc_mem_cache_clean(const void *start_addr, unsigned long size)
{
	unsigned long paddr;

	dmac_map_area(start_addr, size, DMA_TO_DEVICE);
	/*
	 * virtual & phsical addrees mapped directly, so we can convert
	 * the address just using offset
	 */
	paddr = __pa((unsigned long)start_addr);
	outer_clean_range(paddr, paddr + size);

	/* OPT#1: kernel provide below function */
	/*
	dma_map_single(NULL, (void *)start_addr, size, DMA_TO_DEVICE);
	*/
}

void mfc_mem_cache_inv(const void *start_addr, unsigned long size)
{
	unsigned long paddr;

	paddr = __pa((unsigned long)start_addr);
	outer_inv_range(paddr, paddr + size);
	dmac_unmap_area(start_addr, size, DMA_FROM_DEVICE);

	/* OPT#1: kernel provide below function */
	/*
	dma_unmap_single(NULL, (void *)start_addr, size, DMA_FROM_DEVICE);
	*/
}
#endif /* end of SYSMMU_MFC_ON */

#ifdef CONFIG_VIDEO_MFC_VCM_UMP
static void mfc_tlb_invalidate(enum vcm_dev_id id)
{
	if (mfc_power_chk()) {
		mfc_clock_on();

		s5p_sysmmu_tlb_invalidate(NULL);

		mfc_clock_off();
	}
}

static void mfc_set_pagetable(enum vcm_dev_id id, unsigned long base)
{
	if (mfc_power_chk()) {
		mfc_clock_on();

		s5p_sysmmu_set_tablebase_pgd(NULL, base);

		mfc_clock_off();
	}
}

const static struct s5p_vcm_driver mfc_vcm_driver = {
	.tlb_invalidator = &mfc_tlb_invalidate,
	.pgd_base_specifier = &mfc_set_pagetable,
	.phys_alloc = NULL,
	.phys_free = NULL,
};
#endif

#define MAX_ALLOCATION	3
int mfc_init_mem_mgr(struct mfc_dev *dev)
{
	int i;
#if !defined(CONFIG_VIDEO_MFC_VCM_UMP)
	dma_addr_t base[MAX_ALLOCATION];
#else
	/* FIXME: for support user-side allocation. it's temporary solution */
	struct vcm_res	*hole;
#endif
#ifndef SYSMMU_MFC_ON
	size_t size;
#endif
#ifdef CONFIG_S5P_MEM_CMA
	struct cma_info cma_infos[2];
#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	size_t bound_size;
	size_t available_size;
	size_t hole_size;
#else
	int cma_index = 0;
#endif
#else
	unsigned int align_margin;
#endif

	dev->mem_ports = MFC_MAX_MEM_PORT_NUM;
	memset(dev->mem_infos, 0, sizeof(dev->mem_infos));

#ifdef SYSMMU_MFC_ON
#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
	dev->vcm_info.sysmmu_vcm = vcm_create_unified(
		SZ_256M * dev->mem_ports,
			VCM_DEV_MFC,
			&mfc_vcm_driver);

	memcpy(&vcm_info, &dev->vcm_info, sizeof(struct mfc_vcm));

	dev->mem_infos[0].vcm_s = vcm_reserve(dev->vcm_info.sysmmu_vcm,
		MFC_MEMSIZE_PORT_A, 0);

	if (IS_ERR(dev->mem_infos[0].vcm_s))
		return PTR_ERR(dev->mem_infos[0].vcm_s);

	dev->mem_infos[0].base = ALIGN(dev->mem_infos[0].vcm_s->start,
		ALIGN_128KB);
	align_margin = dev->mem_infos[0].base - dev->mem_infos[0].vcm_s->start;
	/* FIXME: for offset operation. it's temporary solution */
	/*
	dev->mem_infos[0].size = MFC_MEMSIZE_PORT_A - align_margin;
	*/
	dev->mem_infos[0].size = SZ_256M - align_margin;
	dev->mem_infos[0].addr = NULL;

	/* FIXME: for support user-side allocation. it's temporary solution */
	if (MFC_MEMSIZE_PORT_A < SZ_256M)
		hole = vcm_reserve(dev->vcm_info.sysmmu_vcm,
			SZ_256M - MFC_MEMSIZE_PORT_A, 0);

	if (dev->mem_ports == 2) {
		dev->mem_infos[1].vcm_s = vcm_reserve(dev->vcm_info.sysmmu_vcm,
			MFC_MEMSIZE_PORT_B, 0);

		if (IS_ERR(dev->mem_infos[1].vcm_s)) {
			vcm_unreserve(dev->mem_infos[0].vcm_s);
			return PTR_ERR(dev->mem_infos[1].vcm_s);
		}

		dev->mem_infos[1].base = ALIGN(dev->mem_infos[1].vcm_s->start,
			ALIGN_128KB);
		align_margin = dev->mem_infos[1].base - dev->mem_infos[1].vcm_s->start;
		dev->mem_infos[1].size = MFC_MEMSIZE_PORT_B - align_margin;
		dev->mem_infos[1].addr = NULL;
	}

	/* FIXME: for support user-side allocation. it's temporary solution */
	vcm_unreserve(hole);

	dev->fw.vcm_s = mfc_vcm_bind(dev->mem_infos[0].base, MFC_FW_SYSTEM_SIZE);
	if (IS_ERR(dev->fw.vcm_s))
		return PTR_ERR(dev->fw.vcm_s);

	dev->fw.vcm_k = mfc_vcm_map(dev->fw.vcm_s->res.phys);
	if (IS_ERR(dev->fw.vcm_k)) {
		mfc_vcm_unbind(dev->fw.vcm_s, 0);
		return PTR_ERR(dev->fw.vcm_k);
	}

	/* FIXME: it's very tricky! MUST BE FIX */
	dev->mem_infos[0].addr = (unsigned char *)dev->fw.vcm_k->start;
#elif defined(CONFIG_S5P_VMEM)
	base[0] = MFC_FREEBASE;

	dev->mem_infos[0].base = ALIGN(base[0], ALIGN_128KB);
	align_margin = dev->mem_infos[0].base - base[0];
	dev->mem_infos[0].size = MFC_MEMSIZE_PORT_A - align_margin;
	dev->mem_infos[0].addr = (unsigned char *)dev->mem_infos[0].base;

	if (dev->mem_ports == 2) {
		base[1] = dev->mem_infos[0].base + dev->mem_infos[0].size;
		dev->mem_infos[1].base = ALIGN(base[1], ALIGN_128KB);
		align_margin = dev->mem_infos[1].base - base[1];
		dev->mem_infos[1].size = MFC_MEMSIZE_PORT_B - align_margin;
		dev->mem_infos[1].addr = (unsigned char *)dev->mem_infos[1].base;
	}

	dev->fw.vmem_cookie = s5p_vmem_vmemmap(MFC_FW_SYSTEM_SIZE,
		dev->mem_infos[0].base,
		dev->mem_infos[0].base + MFC_FW_SYSTEM_SIZE);

	if (!dev->fw.vmem_cookie)
		return -ENOMEM;
#else	/* not CONFIG_VIDEO_MFC_VCM_UMP && not CONFIG_S5P_VMEM */
	/* kernel virtual memory allocator */

	dev->mem_infos[0].vmalloc_addr = vmalloc(MFC_MEMSIZE_PORT_A);
	if (dev->mem_infos[0].vmalloc_addr == NULL)
		return -ENOMEM;

	base[0] = (unsigned long)dev->mem_infos[0].vmalloc_addr;
	dev->mem_infos[0].base = ALIGN(base[0], ALIGN_128KB);
	align_margin = dev->mem_infos[0].base - base[0];
	dev->mem_infos[0].size = MFC_MEMSIZE_PORT_A - align_margin;
	dev->mem_infos[0].addr = (unsigned char *)dev->mem_infos[0].base;

	if (dev->mem_ports == 2) {
		dev->mem_infos[1].vmalloc_addr = vmalloc(MFC_MEMSIZE_PORT_B);
		if (dev->mem_infos[1].vmalloc_addr == NULL) {
			vfree(dev->mem_infos[0].vmalloc_addr);
			return -ENOMEM;
		}

		base[1] = (unsigned long)dev->mem_infos[1].vmalloc_addr;
		dev->mem_infos[1].base = ALIGN(base[1], ALIGN_128KB);
		align_margin = dev->mem_infos[1].base - base[1];
		dev->mem_infos[1].size = MFC_MEMSIZE_PORT_B - align_margin;
		dev->mem_infos[1].addr = (unsigned char *)dev->mem_infos[1].base;
	}
#endif	/* end of CONFIG_VIDEO_MFC_VCM_UMP */
#else	/* not SYSMMU_MFC_ON */
	/* early allocator */
#if defined(CONFIG_S5P_MEM_CMA)
#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	if (cma_info(&cma_infos[0], dev->device, "A")) {
		mfc_info("failed to get CMA info of 'mfc-secure'\n");
		return -ENOMEM;
	}

	if (cma_info(&cma_infos[1], dev->device, "B")) {
		mfc_info("failed to get CMA info of 'mfc-normal'\n");
		return -ENOMEM;
	}

	if (cma_infos[0].lower_bound > cma_infos[1].lower_bound) {
		mfc_info("'mfc-secure' region must be lower than 'mfc-normal' region\n");
		return -ENOMEM;
	}

	/*
	 * available = secure + normal
	 * bound = secure + hole + normal
	 * hole = bound - available
	 */
	available_size = cma_infos[0].free_size + cma_infos[1].free_size;
	bound_size = cma_infos[1].upper_bound - cma_infos[0].lower_bound;
	hole_size = bound_size - available_size;
	mfc_dbg("avail: 0x%08x, bound: 0x%08x offset: 0x%08x, hole: 0x%08x\n",
		available_size, bound_size, MAX_MEM_OFFSET, hole_size);

	/* re-assign actually available size */
	if (bound_size > MAX_MEM_OFFSET) {
		if (cma_infos[0].free_size > MAX_MEM_OFFSET)
			/* it will be return error */
			available_size = MAX_MEM_OFFSET;
		else if ((cma_infos[0].free_size + hole_size) >= MAX_MEM_OFFSET)
			/* it will be return error */
			available_size = cma_infos[0].free_size;
		else
			available_size -= (bound_size - MAX_MEM_OFFSET);
	}
	mfc_dbg("avail: 0x%08x\n", available_size);

	size = cma_infos[0].free_size;
	if (size > available_size) {
		mfc_info("'mfc-secure' region is too large (%d:%d)",
			size >> 10,
			MAX_MEM_OFFSET >> 10);
		return -ENOMEM;
	}

	base[0] = cma_alloc(dev->device, "A", size, ALIGN_128KB);
	if (IS_ERR_VALUE(base[0])) {
		mfc_err("failed to get rsv. memory from CMA on mfc-secure");
		return -ENOMEM;
	}

	dev->mem_infos[0].base = base[0];
	dev->mem_infos[0].size = size;
	dev->mem_infos[0].addr = cma_get_virt(base[0], size, 0);

	available_size -= dev->mem_infos[0].size;
	mfc_dbg("avail: 0x%08x\n", available_size);

	size = MFC_MEMSIZE_DRM;
	if (size > available_size) {
		mfc_info("failed to allocate DRM shared area (%d:%d)\n",
			size >> 10, available_size >> 10);
		return -ENOMEM;
	}

	base[1] = cma_alloc(dev->device, "B", size, 0);
	if (IS_ERR_VALUE(base[1])) {
		mfc_err("failed to get rsv. memory from CMA for DRM on mfc-normal");
		cma_free(base[0]);
		return -ENOMEM;
	}

	dev->drm_info.base = base[1];
	dev->drm_info.size = size;
	dev->drm_info.addr = cma_get_virt(base[1], size, 0);

	available_size -= dev->drm_info.size;
	mfc_dbg("avail: 0x%08x\n", available_size);

	if (available_size > 0) {
		size = cma_infos[1].free_size - MFC_MEMSIZE_DRM;
		if (size > available_size) {
			mfc_warn("<Warning> large hole between reserved memory, "
				"'mfc-normal' size will be shrink (%d:%d)\n",
					size >> 10,
					available_size >> 10);
			size = available_size;
		}

		base[2] = cma_alloc(dev->device, "B", size, ALIGN_128KB);
		if (IS_ERR_VALUE(base[2])) {
			mfc_err("failed to get rsv. memory from CMA on mfc-normal");
			cma_free(base[1]);
			cma_free(base[0]);
			return -ENOMEM;
		}

		dev->mem_infos[1].base = base[2];
		dev->mem_infos[1].size = size;
		dev->mem_infos[1].addr = cma_get_virt(base[2], size, 0);
	}
#else
	if (dev->mem_ports == 1) {
		if (cma_info(&cma_infos[0], dev->device, "AB")) {
			mfc_info("failed to get CMA info of 'mfc'\n");
			return -ENOMEM;
		}

		size = cma_infos[0].free_size;
		if (size > MAX_MEM_OFFSET) {
			mfc_warn("<Warning> too large 'mfc' reserved memory, "
				"size will be shrink (%d:%d)\n",
				size >> 10,
				MAX_MEM_OFFSET >> 10);
			size = MAX_MEM_OFFSET;
		}

		base[0] = cma_alloc(dev->device, "AB", size, ALIGN_128KB);
		if (IS_ERR_VALUE(base[0])) {
			mfc_err("failed to get rsv. memory from CMA");
			return -ENOMEM;
		}

		dev->mem_infos[0].base = base[0];
		dev->mem_infos[0].size = size;
		dev->mem_infos[0].addr = cma_get_virt(base[0], size, 0);
	} else if (dev->mem_ports == 2) {
		if (cma_info(&cma_infos[0], dev->device, "A")) {
			mfc_info("failed to get CMA info of 'mfc0'\n");
			return -ENOMEM;
		}

		if (cma_info(&cma_infos[1], dev->device, "B")) {
			mfc_info("failed to get CMA info of 'mfc1'\n");
			return -ENOMEM;
		}

		if (cma_infos[0].lower_bound > cma_infos[1].lower_bound)
			cma_index = 1;

		size = cma_infos[cma_index].free_size;
		if (size > MAX_MEM_OFFSET) {
			mfc_warn("<Warning> too large 'mfc%d' reserved memory, "
				"size will be shrink (%d:%d)\n",
				cma_index, size >> 10,
				MAX_MEM_OFFSET >> 10);
			size = MAX_MEM_OFFSET;
		}

		base[0] = cma_alloc(dev->device, cma_index ? "B" : "A", size, ALIGN_128KB);
		if (IS_ERR_VALUE(base[0])) {
			mfc_err("failed to get rsv. memory from CMA on port #0");
			return -ENOMEM;
		}

		dev->mem_infos[0].base = base[0];
		dev->mem_infos[0].size = size;
		dev->mem_infos[0].addr = cma_get_virt(base[0], size, 0);

		/* swap CMA index */
		cma_index = !cma_index;

		size = cma_infos[cma_index].free_size;
		if (size > MAX_MEM_OFFSET) {
			mfc_warn("<Warning> too large 'mfc%d' reserved memory, "
				"size will be shrink (%d:%d)\n",
					cma_index, size >> 10,
					MAX_MEM_OFFSET >> 10);
			size = MAX_MEM_OFFSET;
		}

		base[1] = cma_alloc(dev->device, cma_index ? "B" : "A", size, ALIGN_128KB);
		if (IS_ERR_VALUE(base[1])) {
			mfc_err("failed to get rsv. memory from CMA on port #1");
			cma_free(base[0]);
			return -ENOMEM;
		}

		dev->mem_infos[1].base = base[1];
		dev->mem_infos[1].size = size;
		dev->mem_infos[1].addr = cma_get_virt(base[1], size, 0);
	} else {
		mfc_err("failed to get reserved memory from CMA");
		return -EPERM;
	}
#endif
#elif defined(CONFIG_S5P_MEM_BOOTMEM)
	for (i = 0; i < dev->mem_ports; i++) {
#ifdef CONFIG_ARCH_EXYNOS4
		base[i] = s5p_get_media_memory_bank(S5P_MDEV_MFC, i);
#else
		base[i] = s3c_get_media_memory_bank(S3C_MDEV_MFC, i);
#endif
		if (base[i] == 0) {
			mfc_err("failed to get rsv. memory from bootmem on port #%d", i);
			return -EPERM;
		}

#ifdef CONFIG_ARCH_EXYNOS4
		size = s5p_get_media_memsize_bank(S5P_MDEV_MFC, i);
#else
		size = s3c_get_media_memsize_bank(S3C_MDEV_MFC, i);
#endif
		if (size == 0) {
			mfc_err("failed to get rsv. size from bootmem on port #%d", i);
			return -EPERM;
		}

		dev->mem_infos[i].base = ALIGN(base[i], ALIGN_128KB);
		align_margin = dev->mem_infos[i].base - base[i];
		dev->mem_infos[i].size = size - align_margin;
		/* kernel direct mapped memory address */
		dev->mem_infos[i].addr = phys_to_virt(dev->mem_infos[i].base);
	}
#else
	mfc_err("failed to find valid memory allocator for MFC");
	return -EPERM;
#endif	/* end of CONFIG_S5P_MEM_CMA */
#endif	/* end of SYSMMU_MFC_ON */

	mem_ports = dev->mem_ports;
#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	for (i = 0; i < MFC_MAX_MEM_CHUNK_NUM; i++)
		memcpy(&mem_infos[i], &dev->mem_infos[i], sizeof(struct mfc_mem));
#else
	for (i = 0; i < mem_ports; i++)
		memcpy(&mem_infos[i], &dev->mem_infos[i], sizeof(struct mfc_mem));
#endif
	return 0;
}

void mfc_final_mem_mgr(struct mfc_dev *dev)
{
#ifdef SYSMMU_MFC_ON
#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
	vcm_unreserve(dev->mem_infos[0].vcm_s);
	if (dev->mem_ports == 2)
		vcm_unreserve(dev->mem_infos[1].vcm_s);

	vcm_destroy(dev->vcm_info.sysmmu_vcm);
#elif defined(CONFIG_S5P_VMEM)
	s5p_vfree(dev->fw.vmem_cookie);
#else
	vfree(dev->mem_infos[0].vmalloc_addr);
	if (dev->mem_ports == 2)
		vfree(dev->mem_infos[1].vmalloc_addr);
#endif	/* CONFIG_VIDEO_MFC_VCM_UMP */
#else
	/* no action */
#endif /* SYSMMU_MFC_ON */
}

#ifdef CONFIG_VIDEO_MFC_VCM_UMP
void mfc_vcm_dump_res(struct vcm_res *res)
{
	mfc_dbg("vcm_res -\n");
	mfc_dbg("\tstart: 0x%08x, res_size  : 0x%08x\n", (unsigned int)res->start, (unsigned int)res->res_size);
	mfc_dbg("\tphys : 0x%08x, bound_size: 0x%08x\n", (unsigned int)res->phys, (unsigned int)res->bound_size);
}

struct vcm_mmu_res *mfc_vcm_bind(unsigned long addr, unsigned int size)
{
	struct vcm_mmu_res *s_res;
	struct vcm_phys *phys;
	int ret;

	int i;

	s_res = kzalloc(sizeof(struct vcm_mmu_res), GFP_KERNEL);
	if (unlikely(s_res == NULL)) {
		mfc_err("no more kernel memory");
		return ERR_PTR(-ENOMEM);
	}

	s_res->res.start = addr;
	s_res->res.res_size = size;
	s_res->res.vcm = vcm_info.sysmmu_vcm;
	INIT_LIST_HEAD(&s_res->bound);

	phys = vcm_alloc(vcm_info.sysmmu_vcm, size, 0);
	if (IS_ERR(phys))
		return ERR_PTR(PTR_ERR(phys));

	mfc_dbg("phys->size: 0x%08x\n", phys->size);
	for (i = 0; i < phys->count; i++)
		mfc_dbg("start 0x%08x, size: 0x%08x\n",
			(unsigned int)phys->parts[i].start,
			(unsigned int)phys->parts[i].size);

	ret = vcm_bind(&s_res->res, phys);
	if (ret < 0)
		return ERR_PTR(ret);

	mfc_vcm_dump_res(&s_res->res);

	return s_res;
}

void mfc_vcm_unbind(struct vcm_mmu_res *s_res, int flag)
{
	struct vcm_phys *phys;

	phys = vcm_unbind(&s_res->res);

	/* Flag means...
	 * 0 : allocated by MFC
	 * 1 : allocated by other IP */
	if (flag == 0)
	vcm_free(phys);

	kfree(s_res);
}

struct vcm_res *mfc_vcm_map(struct vcm_phys *phys)
{
	struct vcm_res *res;

	res = vcm_map(vcm_vmm, phys, 0);

	mfc_vcm_dump_res(res);

	return res;
}

void mfc_vcm_unmap(struct vcm_res *res)
{
	vcm_unmap(res);
}

void *mfc_ump_map(struct vcm_phys *phys, unsigned long vcminfo)
{
	struct vcm_phys_part *part = phys->parts;
	int num_blocks = phys->count;
	ump_dd_physical_block *blocks;
	ump_dd_handle handle;
	int i;

	blocks = (ump_dd_physical_block *)vmalloc(sizeof(ump_dd_physical_block) * num_blocks);

	for(i = 0; i < num_blocks; i++) {
		blocks[i].addr = part->start;
		blocks[i].size = part->size;
		++part;

		mfc_dbg("\tblock 0x%08lx, size: 0x%08lx\n", blocks[i].addr, blocks[i].size);
	}

	handle = ump_dd_handle_create_from_phys_blocks(blocks, num_blocks);
	/*
	ump_dd_reference_add(handle);
	*/

	vfree(blocks);

	if (handle == UMP_DD_HANDLE_INVALID)
		return ERR_PTR(-ENOMEM);

	if (ump_dd_meminfo_set(handle, (void*)vcminfo) != UMP_DD_SUCCESS)
		return ERR_PTR(-ENOMEM);

	return (void *)handle;
}

void mfc_ump_unmap(void *handle)
{
	ump_dd_reference_release(handle);
}

unsigned int mfc_ump_get_id(void *handle)
{
	return ump_dd_secure_id_get(handle);
}

unsigned long mfc_ump_get_virt(unsigned int secure_id)
{
	struct vcm_res *res = (struct vcm_res *)
		ump_dd_meminfo_get(secure_id, (void*)VCM_DEV_MFC);

	if (res) {
		return res->start;
	} else {
		mfc_err("failed to get device virtual, id: %d",
			(unsigned int)secure_id);

		return 0;
	}
}
#endif /* CONFIG_VIDEO_MFC_VCM_UMP */

