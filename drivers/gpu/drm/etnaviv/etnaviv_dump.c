// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2018 Etnaviv Project
 */

#include <linux/devcoredump.h>
#include "etnaviv_cmdbuf.h"
#include "etnaviv_dump.h"
#include "etnaviv_gem.h"
#include "etnaviv_gpu.h"
#include "etnaviv_mmu.h"
#include "etnaviv_sched.h"
#include "state.xml.h"
#include "state_hi.xml.h"

static bool etnaviv_dump_core = true;
module_param_named(dump_core, etnaviv_dump_core, bool, 0600);

struct core_dump_iterator {
	void *start;
	struct etnaviv_dump_object_header *hdr;
	void *data;
};

static const unsigned short etnaviv_dump_registers[] = {
	VIVS_HI_AXI_STATUS,
	VIVS_HI_CLOCK_CONTROL,
	VIVS_HI_IDLE_STATE,
	VIVS_HI_AXI_CONFIG,
	VIVS_HI_INTR_ENBL,
	VIVS_HI_CHIP_IDENTITY,
	VIVS_HI_CHIP_FEATURE,
	VIVS_HI_CHIP_MODEL,
	VIVS_HI_CHIP_REV,
	VIVS_HI_CHIP_DATE,
	VIVS_HI_CHIP_TIME,
	VIVS_HI_CHIP_MINOR_FEATURE_0,
	VIVS_HI_CACHE_CONTROL,
	VIVS_HI_AXI_CONTROL,
	VIVS_PM_POWER_CONTROLS,
	VIVS_PM_MODULE_CONTROLS,
	VIVS_PM_MODULE_STATUS,
	VIVS_PM_PULSE_EATER,
	VIVS_MC_MMU_FE_PAGE_TABLE,
	VIVS_MC_MMU_TX_PAGE_TABLE,
	VIVS_MC_MMU_PE_PAGE_TABLE,
	VIVS_MC_MMU_PEZ_PAGE_TABLE,
	VIVS_MC_MMU_RA_PAGE_TABLE,
	VIVS_MC_DEBUG_MEMORY,
	VIVS_MC_MEMORY_BASE_ADDR_RA,
	VIVS_MC_MEMORY_BASE_ADDR_FE,
	VIVS_MC_MEMORY_BASE_ADDR_TX,
	VIVS_MC_MEMORY_BASE_ADDR_PEZ,
	VIVS_MC_MEMORY_BASE_ADDR_PE,
	VIVS_MC_MEMORY_TIMING_CONTROL,
	VIVS_MC_BUS_CONFIG,
	VIVS_FE_DMA_STATUS,
	VIVS_FE_DMA_DEBUG_STATE,
	VIVS_FE_DMA_ADDRESS,
	VIVS_FE_DMA_LOW,
	VIVS_FE_DMA_HIGH,
	VIVS_FE_AUTO_FLUSH,
};

static void etnaviv_core_dump_header(struct core_dump_iterator *iter,
	u32 type, void *data_end)
{
	struct etnaviv_dump_object_header *hdr = iter->hdr;

	hdr->magic = cpu_to_le32(ETDUMP_MAGIC);
	hdr->type = cpu_to_le32(type);
	hdr->file_offset = cpu_to_le32(iter->data - iter->start);
	hdr->file_size = cpu_to_le32(data_end - iter->data);

	iter->hdr++;
	iter->data += hdr->file_size;
}

static void etnaviv_core_dump_registers(struct core_dump_iterator *iter,
	struct etnaviv_gpu *gpu)
{
	struct etnaviv_dump_registers *reg = iter->data;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(etnaviv_dump_registers); i++, reg++) {
		reg->reg = etnaviv_dump_registers[i];
		reg->value = gpu_read(gpu, etnaviv_dump_registers[i]);
	}

	etnaviv_core_dump_header(iter, ETDUMP_BUF_REG, reg);
}

static void etnaviv_core_dump_mmu(struct core_dump_iterator *iter,
	struct etnaviv_gpu *gpu, size_t mmu_size)
{
	etnaviv_iommu_dump(gpu->mmu, iter->data);

	etnaviv_core_dump_header(iter, ETDUMP_BUF_MMU, iter->data + mmu_size);
}

static void etnaviv_core_dump_mem(struct core_dump_iterator *iter, u32 type,
	void *ptr, size_t size, u64 iova)
{
	memcpy(iter->data, ptr, size);

	iter->hdr->iova = cpu_to_le64(iova);

	etnaviv_core_dump_header(iter, type, iter->data + size);
}

void etnaviv_core_dump(struct etnaviv_gpu *gpu)
{
	struct core_dump_iterator iter;
	struct etnaviv_vram_mapping *vram;
	struct etnaviv_gem_object *obj;
	struct etnaviv_gem_submit *submit;
	struct drm_sched_job *s_job;
	unsigned int n_obj, n_bomap_pages;
	size_t file_size, mmu_size;
	__le64 *bomap, *bomap_start;
	unsigned long flags;

	/* Only catch the first event, or when manually re-armed */
	if (!etnaviv_dump_core)
		return;
	etnaviv_dump_core = false;

	mutex_lock(&gpu->mmu->lock);

	mmu_size = etnaviv_iommu_dump_size(gpu->mmu);

	/* We always dump registers, mmu, ring and end marker */
	n_obj = 4;
	n_bomap_pages = 0;
	file_size = ARRAY_SIZE(etnaviv_dump_registers) *
			sizeof(struct etnaviv_dump_registers) +
		    mmu_size + gpu->buffer.size;

	/* Add in the active command buffers */
	spin_lock_irqsave(&gpu->sched.job_list_lock, flags);
	list_for_each_entry(s_job, &gpu->sched.ring_mirror_list, node) {
		submit = to_etnaviv_submit(s_job);
		file_size += submit->cmdbuf.size;
		n_obj++;
	}
	spin_unlock_irqrestore(&gpu->sched.job_list_lock, flags);

	/* Add in the active buffer objects */
	list_for_each_entry(vram, &gpu->mmu->mappings, mmu_node) {
		if (!vram->use)
			continue;

		obj = vram->object;
		file_size += obj->base.size;
		n_bomap_pages += obj->base.size >> PAGE_SHIFT;
		n_obj++;
	}

	/* If we have any buffer objects, add a bomap object */
	if (n_bomap_pages) {
		file_size += n_bomap_pages * sizeof(__le64);
		n_obj++;
	}

	/* Add the size of the headers */
	file_size += sizeof(*iter.hdr) * n_obj;

	/* Allocate the file in vmalloc memory, it's likely to be big */
	iter.start = __vmalloc(file_size, GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY,
			       PAGE_KERNEL);
	if (!iter.start) {
		mutex_unlock(&gpu->mmu->lock);
		dev_warn(gpu->dev, "failed to allocate devcoredump file\n");
		return;
	}

	/* Point the data member after the headers */
	iter.hdr = iter.start;
	iter.data = &iter.hdr[n_obj];

	memset(iter.hdr, 0, iter.data - iter.start);

	etnaviv_core_dump_registers(&iter, gpu);
	etnaviv_core_dump_mmu(&iter, gpu, mmu_size);
	etnaviv_core_dump_mem(&iter, ETDUMP_BUF_RING, gpu->buffer.vaddr,
			      gpu->buffer.size,
			      etnaviv_cmdbuf_get_va(&gpu->buffer));

	spin_lock_irqsave(&gpu->sched.job_list_lock, flags);
	list_for_each_entry(s_job, &gpu->sched.ring_mirror_list, node) {
		submit = to_etnaviv_submit(s_job);
		etnaviv_core_dump_mem(&iter, ETDUMP_BUF_CMD,
				      submit->cmdbuf.vaddr, submit->cmdbuf.size,
				      etnaviv_cmdbuf_get_va(&submit->cmdbuf));
	}
	spin_unlock_irqrestore(&gpu->sched.job_list_lock, flags);

	/* Reserve space for the bomap */
	if (n_bomap_pages) {
		bomap_start = bomap = iter.data;
		memset(bomap, 0, sizeof(*bomap) * n_bomap_pages);
		etnaviv_core_dump_header(&iter, ETDUMP_BUF_BOMAP,
					 bomap + n_bomap_pages);
	} else {
		/* Silence warning */
		bomap_start = bomap = NULL;
	}

	list_for_each_entry(vram, &gpu->mmu->mappings, mmu_node) {
		struct page **pages;
		void *vaddr;

		if (vram->use == 0)
			continue;

		obj = vram->object;

		mutex_lock(&obj->lock);
		pages = etnaviv_gem_get_pages(obj);
		mutex_unlock(&obj->lock);
		if (!IS_ERR(pages)) {
			int j;

			iter.hdr->data[0] = bomap - bomap_start;

			for (j = 0; j < obj->base.size >> PAGE_SHIFT; j++)
				*bomap++ = cpu_to_le64(page_to_phys(*pages++));
		}

		iter.hdr->iova = cpu_to_le64(vram->iova);

		vaddr = etnaviv_gem_vmap(&obj->base);
		if (vaddr)
			memcpy(iter.data, vaddr, obj->base.size);

		etnaviv_core_dump_header(&iter, ETDUMP_BUF_BO, iter.data +
					 obj->base.size);
	}

	mutex_unlock(&gpu->mmu->lock);

	etnaviv_core_dump_header(&iter, ETDUMP_BUF_END, iter.data);

	dev_coredumpv(gpu->dev, iter.start, iter.data - iter.start, GFP_KERNEL);
}
