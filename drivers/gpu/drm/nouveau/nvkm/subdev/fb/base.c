/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#include "priv.h"
#include "ram.h"

#include <core/memory.h>
#include <core/option.h>
#include <subdev/bios.h>
#include <subdev/bios/M0203.h>
#include <engine/gr.h>
#include <engine/mpeg.h>

void
nvkm_fb_tile_fini(struct nvkm_fb *fb, int region, struct nvkm_fb_tile *tile)
{
	fb->func->tile.fini(fb, region, tile);
}

void
nvkm_fb_tile_init(struct nvkm_fb *fb, int region, u32 addr, u32 size,
		  u32 pitch, u32 flags, struct nvkm_fb_tile *tile)
{
	fb->func->tile.init(fb, region, addr, size, pitch, flags, tile);
}

void
nvkm_fb_tile_prog(struct nvkm_fb *fb, int region, struct nvkm_fb_tile *tile)
{
	struct nvkm_device *device = fb->subdev.device;
	if (fb->func->tile.prog) {
		fb->func->tile.prog(fb, region, tile);
		if (device->gr)
			nvkm_engine_tile(&device->gr->engine, region);
		if (device->mpeg)
			nvkm_engine_tile(device->mpeg, region);
	}
}

static void
nvkm_fb_sysmem_flush_page_init(struct nvkm_device *device)
{
	struct nvkm_fb *fb = device->fb;

	if (fb->func->sysmem.flush_page_init)
		fb->func->sysmem.flush_page_init(fb);
}

int
nvkm_fb_bios_memtype(struct nvkm_bios *bios)
{
	struct nvkm_subdev *subdev = &bios->subdev;
	struct nvkm_device *device = subdev->device;
	const u8 ramcfg = (nvkm_rd32(device, 0x101000) & 0x0000003c) >> 2;
	struct nvbios_M0203E M0203E;
	u8 ver, hdr;

	if (nvbios_M0203Em(bios, ramcfg, &ver, &hdr, &M0203E)) {
		switch (M0203E.type) {
		case M0203E_TYPE_DDR2  : return NVKM_RAM_TYPE_DDR2;
		case M0203E_TYPE_DDR3  : return NVKM_RAM_TYPE_DDR3;
		case M0203E_TYPE_GDDR3 : return NVKM_RAM_TYPE_GDDR3;
		case M0203E_TYPE_GDDR5 : return NVKM_RAM_TYPE_GDDR5;
		case M0203E_TYPE_GDDR5X: return NVKM_RAM_TYPE_GDDR5X;
		case M0203E_TYPE_GDDR6 : return NVKM_RAM_TYPE_GDDR6;
		case M0203E_TYPE_HBM2  : return NVKM_RAM_TYPE_HBM2;
		default:
			nvkm_warn(subdev, "M0203E type %02x\n", M0203E.type);
			return NVKM_RAM_TYPE_UNKNOWN;
		}
	}

	nvkm_warn(subdev, "M0203E not matched!\n");
	return NVKM_RAM_TYPE_UNKNOWN;
}

static void
nvkm_fb_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_fb *fb = nvkm_fb(subdev);
	if (fb->func->intr)
		fb->func->intr(fb);
}

static int
nvkm_fb_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_fb *fb = nvkm_fb(subdev);
	u32 tags = 0;

	if (fb->func->ram_new) {
		int ret = fb->func->ram_new(fb, &fb->ram);
		if (ret) {
			nvkm_error(subdev, "vram setup failed, %d\n", ret);
			return ret;
		}
	}

	if (fb->func->oneinit) {
		int ret = fb->func->oneinit(fb);
		if (ret)
			return ret;
	}

	/* Initialise compression tag allocator.
	 *
	 * LTC oneinit() will override this on Fermi and newer.
	 */
	if (fb->func->tags) {
		tags = fb->func->tags(fb);
		nvkm_debug(subdev, "%d comptags\n", tags);
	}

	return nvkm_mm_init(&fb->tags.mm, 0, 0, tags, 1);
}

int
nvkm_fb_mem_unlock(struct nvkm_fb *fb)
{
	struct nvkm_subdev *subdev = &fb->subdev;
	int ret;

	if (!fb->func->vpr.scrub_required)
		return 0;

	if (!fb->func->vpr.scrub_required(fb)) {
		nvkm_debug(subdev, "VPR not locked\n");
		return 0;
	}

	nvkm_debug(subdev, "VPR locked, running scrubber binary\n");

	if (!fb->vpr_scrubber.size) {
		nvkm_warn(subdev, "VPR locked, but no scrubber binary!\n");
		return 0;
	}

	ret = fb->func->vpr.scrub(fb);
	if (ret) {
		nvkm_error(subdev, "VPR scrubber binary failed\n");
		return ret;
	}

	if (fb->func->vpr.scrub_required(fb)) {
		nvkm_error(subdev, "VPR still locked after scrub!\n");
		return -EIO;
	}

	nvkm_debug(subdev, "VPR scrubber binary successful\n");
	return 0;
}

static int
nvkm_fb_init(struct nvkm_subdev *subdev)
{
	struct nvkm_fb *fb = nvkm_fb(subdev);
	int ret, i;

	if (fb->ram) {
		ret = nvkm_ram_init(fb->ram);
		if (ret)
			return ret;
	}

	for (i = 0; i < fb->tile.regions; i++)
		fb->func->tile.prog(fb, i, &fb->tile.region[i]);

	nvkm_fb_sysmem_flush_page_init(subdev->device);

	if (fb->func->init)
		fb->func->init(fb);

	if (fb->func->init_remapper)
		fb->func->init_remapper(fb);

	if (fb->func->init_page) {
		ret = fb->func->init_page(fb);
		if (WARN_ON(ret))
			return ret;
	}

	if (fb->func->init_unkn)
		fb->func->init_unkn(fb);

	return 0;
}

static int
nvkm_fb_preinit(struct nvkm_subdev *subdev)
{
	nvkm_fb_sysmem_flush_page_init(subdev->device);
	return 0;
}

static void *
nvkm_fb_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_fb *fb = nvkm_fb(subdev);
	int i;

	nvkm_memory_unref(&fb->mmu_wr);
	nvkm_memory_unref(&fb->mmu_rd);

	for (i = 0; i < fb->tile.regions; i++)
		fb->func->tile.fini(fb, i, &fb->tile.region[i]);

	nvkm_mm_fini(&fb->tags.mm);
	mutex_destroy(&fb->tags.mutex);

	nvkm_ram_del(&fb->ram);

	nvkm_blob_dtor(&fb->vpr_scrubber);

	if (fb->sysmem.flush_page) {
		dma_unmap_page(subdev->device->dev, fb->sysmem.flush_page_addr,
			       PAGE_SIZE, DMA_BIDIRECTIONAL);
		__free_page(fb->sysmem.flush_page);
	}

	if (fb->func->dtor)
		return fb->func->dtor(fb);

	return fb;
}

static const struct nvkm_subdev_func
nvkm_fb = {
	.dtor = nvkm_fb_dtor,
	.preinit = nvkm_fb_preinit,
	.oneinit = nvkm_fb_oneinit,
	.init = nvkm_fb_init,
	.intr = nvkm_fb_intr,
};

int
nvkm_fb_ctor(const struct nvkm_fb_func *func, struct nvkm_device *device,
	     enum nvkm_subdev_type type, int inst, struct nvkm_fb *fb)
{
	nvkm_subdev_ctor(&nvkm_fb, device, type, inst, &fb->subdev);
	fb->func = func;
	fb->tile.regions = fb->func->tile.regions;
	fb->page = nvkm_longopt(device->cfgopt, "NvFbBigPage", fb->func->default_bigpage);
	mutex_init(&fb->tags.mutex);

	if (func->sysmem.flush_page_init) {
		fb->sysmem.flush_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!fb->sysmem.flush_page)
			return -ENOMEM;

		fb->sysmem.flush_page_addr = dma_map_page(device->dev, fb->sysmem.flush_page,
							  0, PAGE_SIZE, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(device->dev, fb->sysmem.flush_page_addr))
			return -EFAULT;
	}

	return 0;
}

int
nvkm_fb_new_(const struct nvkm_fb_func *func, struct nvkm_device *device,
	     enum nvkm_subdev_type type, int inst, struct nvkm_fb **pfb)
{
	if (!(*pfb = kzalloc(sizeof(**pfb), GFP_KERNEL)))
		return -ENOMEM;
	return nvkm_fb_ctor(func, device, type, inst, *pfb);
}
