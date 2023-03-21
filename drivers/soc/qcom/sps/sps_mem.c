// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011, 2013, 2015, 2017-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/**
 * Pipe-Memory allocation/free management.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/genalloc.h>
#include <linux/errno.h>

#include "sps_bam.h"
#include "spsi.h"

static phys_addr_t iomem_phys;
static void *iomem_virt;
static u32 iomem_size;
static u32 iomem_offset;
static struct gen_pool *pool;
static u32 nid = NUMA_NO_NODE;

/* Debug */
static u32 total_alloc;
static u32 total_free;

/**
 * Translate physical to virtual address
 *
 */
void *spsi_get_mem_ptr(phys_addr_t phys_addr)
{
	void *virt = NULL;

	if ((phys_addr >= iomem_phys) &&
	    (phys_addr < (iomem_phys + iomem_size))) {
		virt = (u8 *) iomem_virt + (phys_addr - iomem_phys);
	} else {
		virt = phys_to_virt(phys_addr);
		SPS_ERR(sps, "sps:%s.invalid phys addr=0x%pa\n",
			__func__, &phys_addr);
	}
	return virt;
}

/**
 * Allocate I/O (pipe) memory
 *
 */
phys_addr_t sps_mem_alloc_io(u32 bytes)
{
	phys_addr_t phys_addr = SPS_ADDR_INVALID;
	unsigned long virt_addr = 0;

	virt_addr = gen_pool_alloc(pool, bytes);
	if (virt_addr) {
		iomem_offset = virt_addr - (uintptr_t) iomem_virt;
		phys_addr = iomem_phys + iomem_offset;
		total_alloc += bytes;
	} else {
		SPS_ERR(sps, "sps:gen_pool_alloc %d bytes fail\n", bytes);
		return SPS_ADDR_INVALID;
	}

	SPS_DBG3(sps, "sps:%s.phys=%pa.virt=0x%pK.size=0x%x\n",
		__func__, &phys_addr, (void *)virt_addr, bytes);

	return phys_addr;
}

/**
 * Free I/O memory
 *
 */
void sps_mem_free_io(phys_addr_t phys_addr, u32 bytes)
{
	unsigned long virt_addr = 0;

	iomem_offset = phys_addr - iomem_phys;
	virt_addr = (uintptr_t) iomem_virt + iomem_offset;

	SPS_DBG3(sps, "sps:%s.phys=%pa.virt=0x%pK.size=0x%x\n",
		__func__, &phys_addr, (void *)virt_addr, bytes);

	gen_pool_free(pool, virt_addr, bytes);
	total_free += bytes;
}

/**
 * Initialize driver memory module
 *
 */
int sps_mem_init(phys_addr_t pipemem_phys_base, u32 pipemem_size)
{
	int res;

	/* 2^8=128. The desc-fifo and data-fifo minimal allocation. */
	int min_alloc_order = 8;

	if ((d_type == 0) || (d_type == 2) || imem) {
		iomem_phys = pipemem_phys_base;
		iomem_size = pipemem_size;

		if (iomem_phys == 0) {
			SPS_ERR(sps, "sps:%s:Invalid Pipe-Mem address\n",
				__func__);
			return SPS_ERROR;
		}
		iomem_virt = ioremap(iomem_phys, iomem_size);
		if (!iomem_virt) {
			SPS_ERR(sps,
				"sps:%s:Failed to IO map pipe memory\n",
					__func__);
			return -ENOMEM;
		}

		iomem_offset = 0;
		SPS_DBG(sps,
			"sps:%s.iomem_phys=%pa,iomem_virt=0x%pK\n",
			__func__, &iomem_phys, iomem_virt);
	}

	pool = gen_pool_create(min_alloc_order, nid);

	if (!pool) {
		SPS_ERR(sps, "sps:%s:Failed to create a new memory pool\n",
								__func__);
		return -ENOMEM;
	}

	if ((d_type == 0) || (d_type == 2) || imem) {
		res = gen_pool_add(pool, (uintptr_t)iomem_virt,
				iomem_size, nid);
		if (res)
			return res;
	}

	return 0;
}

/**
 * De-initialize driver memory module
 *
 */
int sps_mem_de_init(void)
{
	if (iomem_virt != NULL) {
		gen_pool_destroy(pool);
		pool = NULL;
		iounmap(iomem_virt);
		iomem_virt = NULL;
	}

	if (total_alloc == total_free)
		return 0;

	SPS_ERR(sps, "sps:%s:some memory not free\n", __func__);
	return SPS_ERROR;
}
