// SPDX-License-Identifier: GPL-2.0
/* Copyright 2021 Collabora ltd. */

#include <linux/err.h>
#include <linux/device.h>
#include <linux/devcoredump.h>
#include <linux/moduleparam.h>
#include <linux/iosys-map.h>
#include <drm/panfrost_drm.h>
#include <drm/drm_device.h>

#include "panfrost_job.h"
#include "panfrost_gem.h"
#include "panfrost_regs.h"
#include "panfrost_dump.h"
#include "panfrost_device.h"

static bool panfrost_dump_core = true;
module_param_named(dump_core, panfrost_dump_core, bool, 0600);

struct panfrost_dump_iterator {
	void *start;
	struct panfrost_dump_object_header *hdr;
	void *data;
};

static const unsigned short panfrost_dump_registers[] = {
	SHADER_READY_LO,
	SHADER_READY_HI,
	TILER_READY_LO,
	TILER_READY_HI,
	L2_READY_LO,
	L2_READY_HI,
	JOB_INT_MASK,
	JOB_INT_STAT,
	JS_HEAD_LO(0),
	JS_HEAD_HI(0),
	JS_TAIL_LO(0),
	JS_TAIL_HI(0),
	JS_AFFINITY_LO(0),
	JS_AFFINITY_HI(0),
	JS_CONFIG(0),
	JS_STATUS(0),
	JS_HEAD_NEXT_LO(0),
	JS_HEAD_NEXT_HI(0),
	JS_AFFINITY_NEXT_LO(0),
	JS_AFFINITY_NEXT_HI(0),
	JS_CONFIG_NEXT(0),
	MMU_INT_MASK,
	MMU_INT_STAT,
	AS_TRANSTAB_LO(0),
	AS_TRANSTAB_HI(0),
	AS_MEMATTR_LO(0),
	AS_MEMATTR_HI(0),
	AS_FAULTSTATUS(0),
	AS_FAULTADDRESS_LO(0),
	AS_FAULTADDRESS_HI(0),
	AS_STATUS(0),
};

static void panfrost_core_dump_header(struct panfrost_dump_iterator *iter,
				      u32 type, void *data_end)
{
	struct panfrost_dump_object_header *hdr = iter->hdr;

	hdr->magic = PANFROSTDUMP_MAGIC;
	hdr->type = type;
	hdr->file_offset = iter->data - iter->start;
	hdr->file_size = data_end - iter->data;

	iter->hdr++;
	iter->data += hdr->file_size;
}

static void
panfrost_core_dump_registers(struct panfrost_dump_iterator *iter,
			     struct panfrost_device *pfdev,
			     u32 as_nr, int slot)
{
	struct panfrost_dump_registers *dumpreg = iter->data;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(panfrost_dump_registers); i++, dumpreg++) {
		unsigned int js_as_offset = 0;
		unsigned int reg;

		if (panfrost_dump_registers[i] >= JS_BASE &&
		    panfrost_dump_registers[i] <= JS_BASE + JS_SLOT_STRIDE)
			js_as_offset = slot * JS_SLOT_STRIDE;
		else if (panfrost_dump_registers[i] >= MMU_BASE &&
			 panfrost_dump_registers[i] <= MMU_BASE + MMU_AS_STRIDE)
			js_as_offset = (as_nr << MMU_AS_SHIFT);

		reg = panfrost_dump_registers[i] + js_as_offset;

		dumpreg->reg = reg;
		dumpreg->value = gpu_read(pfdev, reg);
	}

	panfrost_core_dump_header(iter, PANFROSTDUMP_BUF_REG, dumpreg);
}

void panfrost_core_dump(struct panfrost_job *job)
{
	struct panfrost_device *pfdev = job->pfdev;
	struct panfrost_dump_iterator iter;
	struct drm_gem_object *dbo;
	unsigned int n_obj, n_bomap_pages;
	u64 *bomap, *bomap_start;
	size_t file_size;
	u32 as_nr;
	int slot;
	int ret, i;

	as_nr = job->mmu->as;
	slot = panfrost_job_get_slot(job);

	/* Only catch the first event, or when manually re-armed */
	if (!panfrost_dump_core)
		return;
	panfrost_dump_core = false;

	/* At least, we dump registers and end marker */
	n_obj = 2;
	n_bomap_pages = 0;
	file_size = ARRAY_SIZE(panfrost_dump_registers) *
			sizeof(struct panfrost_dump_registers);

	/* Add in the active buffer objects */
	for (i = 0; i < job->bo_count; i++) {
		/*
		 * Even though the CPU could be configured to use 16K or 64K pages, this
		 * is a very unusual situation for most kernel setups on SoCs that have
		 * a Panfrost device. Also many places across the driver make the somewhat
		 * arbitrary assumption that Panfrost's MMU page size is the same as the CPU's,
		 * so let's have a sanity check to ensure that's always the case
		 */
		dbo = job->bos[i];
		WARN_ON(!IS_ALIGNED(dbo->size, PAGE_SIZE));

		file_size += dbo->size;
		n_bomap_pages += dbo->size >> PAGE_SHIFT;
		n_obj++;
	}

	/* If we have any buffer objects, add a bomap object */
	if (n_bomap_pages) {
		file_size += n_bomap_pages * sizeof(*bomap);
		n_obj++;
	}

	/* Add the size of the headers */
	file_size += sizeof(*iter.hdr) * n_obj;

	/*
	 * Allocate the file in vmalloc memory, it's likely to be big.
	 * The reason behind these GFP flags is that we don't want to trigger the
	 * OOM killer in the event that not enough memory could be found for our
	 * dump file. We also don't want the allocator to do any error reporting,
	 * as the right behaviour is failing gracefully if a big enough buffer
	 * could not be allocated.
	 */
	iter.start = __vmalloc(file_size, GFP_KERNEL | __GFP_NOWARN |
			__GFP_NORETRY);
	if (!iter.start) {
		dev_warn(pfdev->dev, "failed to allocate devcoredump file\n");
		return;
	}

	/* Point the data member after the headers */
	iter.hdr = iter.start;
	iter.data = &iter.hdr[n_obj];

	memset(iter.hdr, 0, iter.data - iter.start);

	/*
	 * For now, we write the job identifier in the register dump header,
	 * so that we can decode the entire dump later with pandecode
	 */
	iter.hdr->reghdr.jc = job->jc;
	iter.hdr->reghdr.major = PANFROSTDUMP_MAJOR;
	iter.hdr->reghdr.minor = PANFROSTDUMP_MINOR;
	iter.hdr->reghdr.gpu_id = pfdev->features.id;
	iter.hdr->reghdr.nbos = job->bo_count;

	panfrost_core_dump_registers(&iter, pfdev, as_nr, slot);

	/* Reserve space for the bomap */
	if (job->bo_count) {
		bomap_start = bomap = iter.data;
		memset(bomap, 0, sizeof(*bomap) * n_bomap_pages);
		panfrost_core_dump_header(&iter, PANFROSTDUMP_BUF_BOMAP,
					  bomap + n_bomap_pages);
	}

	for (i = 0; i < job->bo_count; i++) {
		struct iosys_map map;
		struct panfrost_gem_mapping *mapping;
		struct panfrost_gem_object *bo;
		struct sg_page_iter page_iter;
		void *vaddr;

		bo = to_panfrost_bo(job->bos[i]);
		mapping = job->mappings[i];

		if (!bo->base.sgt) {
			dev_err(pfdev->dev, "Panfrost Dump: BO has no sgt, cannot dump\n");
			iter.hdr->bomap.valid = 0;
			goto dump_header;
		}

		ret = drm_gem_vmap_unlocked(&bo->base.base, &map);
		if (ret) {
			dev_err(pfdev->dev, "Panfrost Dump: couldn't map Buffer Object\n");
			iter.hdr->bomap.valid = 0;
			goto dump_header;
		}

		WARN_ON(!mapping->active);

		iter.hdr->bomap.data[0] = bomap - bomap_start;

		for_each_sgtable_page(bo->base.sgt, &page_iter, 0) {
			struct page *page = sg_page_iter_page(&page_iter);

			if (!IS_ERR(page)) {
				*bomap++ = page_to_phys(page);
			} else {
				dev_err(pfdev->dev, "Panfrost Dump: wrong page\n");
				*bomap++ = 0;
			}
		}

		iter.hdr->bomap.iova = mapping->mmnode.start << PAGE_SHIFT;

		vaddr = map.vaddr;
		memcpy(iter.data, vaddr, bo->base.base.size);

		drm_gem_vunmap_unlocked(&bo->base.base, &map);

		iter.hdr->bomap.valid = 1;

dump_header:	panfrost_core_dump_header(&iter, PANFROSTDUMP_BUF_BO, iter.data +
					  bo->base.base.size);
	}
	panfrost_core_dump_header(&iter, PANFROSTDUMP_BUF_TRAILER, iter.data);

	dev_coredumpv(pfdev->dev, iter.start, iter.data - iter.start, GFP_KERNEL);
}
