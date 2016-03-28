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

#include <core/enum.h>
#include <subdev/fb.h>
#include <subdev/timer.h>

void
gf100_ltc_cbc_clear(struct nvkm_ltc *ltc, u32 start, u32 limit)
{
	struct nvkm_device *device = ltc->subdev.device;
	nvkm_wr32(device, 0x17e8cc, start);
	nvkm_wr32(device, 0x17e8d0, limit);
	nvkm_wr32(device, 0x17e8c8, 0x00000004);
}

void
gf100_ltc_cbc_wait(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	int c, s;
	for (c = 0; c < ltc->ltc_nr; c++) {
		for (s = 0; s < ltc->lts_nr; s++) {
			const u32 addr = 0x1410c8 + (c * 0x2000) + (s * 0x400);
			nvkm_msec(device, 2000,
				if (!nvkm_rd32(device, addr))
					break;
			);
		}
	}
}

void
gf100_ltc_zbc_clear_color(struct nvkm_ltc *ltc, int i, const u32 color[4])
{
	struct nvkm_device *device = ltc->subdev.device;
	nvkm_mask(device, 0x17ea44, 0x0000000f, i);
	nvkm_wr32(device, 0x17ea48, color[0]);
	nvkm_wr32(device, 0x17ea4c, color[1]);
	nvkm_wr32(device, 0x17ea50, color[2]);
	nvkm_wr32(device, 0x17ea54, color[3]);
}

void
gf100_ltc_zbc_clear_depth(struct nvkm_ltc *ltc, int i, const u32 depth)
{
	struct nvkm_device *device = ltc->subdev.device;
	nvkm_mask(device, 0x17ea44, 0x0000000f, i);
	nvkm_wr32(device, 0x17ea58, depth);
}

static const struct nvkm_bitfield
gf100_ltc_lts_intr_name[] = {
	{ 0x00000001, "IDLE_ERROR_IQ" },
	{ 0x00000002, "IDLE_ERROR_CBC" },
	{ 0x00000004, "IDLE_ERROR_TSTG" },
	{ 0x00000008, "IDLE_ERROR_DSTG" },
	{ 0x00000010, "EVICTED_CB" },
	{ 0x00000020, "ILLEGAL_COMPSTAT" },
	{ 0x00000040, "BLOCKLINEAR_CB" },
	{ 0x00000100, "ECC_SEC_ERROR" },
	{ 0x00000200, "ECC_DED_ERROR" },
	{ 0x00000400, "DEBUG" },
	{ 0x00000800, "ATOMIC_TO_Z" },
	{ 0x00001000, "ILLEGAL_ATOMIC" },
	{ 0x00002000, "BLKACTIVITY_ERR" },
	{}
};

static void
gf100_ltc_lts_intr(struct nvkm_ltc *ltc, int c, int s)
{
	struct nvkm_subdev *subdev = &ltc->subdev;
	struct nvkm_device *device = subdev->device;
	u32 base = 0x141000 + (c * 0x2000) + (s * 0x400);
	u32 intr = nvkm_rd32(device, base + 0x020);
	u32 stat = intr & 0x0000ffff;
	char msg[128];

	if (stat) {
		nvkm_snprintbf(msg, sizeof(msg), gf100_ltc_lts_intr_name, stat);
		nvkm_error(subdev, "LTC%d_LTS%d: %08x [%s]\n", c, s, stat, msg);
	}

	nvkm_wr32(device, base + 0x020, intr);
}

void
gf100_ltc_intr(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	u32 mask;

	mask = nvkm_rd32(device, 0x00017c);
	while (mask) {
		u32 s, c = __ffs(mask);
		for (s = 0; s < ltc->lts_nr; s++)
			gf100_ltc_lts_intr(ltc, c, s);
		mask &= ~(1 << c);
	}
}

void
gf100_ltc_invalidate(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	s64 taken;

	nvkm_wr32(device, 0x70004, 0x00000001);
	taken = nvkm_wait_msec(device, 2000, 0x70004, 0x00000003, 0x00000000);

	if (taken > 0)
		nvkm_debug(&ltc->subdev, "LTC invalidate took %lld ns\n", taken);
}

void
gf100_ltc_flush(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	s64 taken;

	nvkm_wr32(device, 0x70010, 0x00000001);
	taken = nvkm_wait_msec(device, 2000, 0x70010, 0x00000003, 0x00000000);

	if (taken > 0)
		nvkm_debug(&ltc->subdev, "LTC flush took %lld ns\n", taken);
}

/* TODO: Figure out tag memory details and drop the over-cautious allocation.
 */
int
gf100_ltc_oneinit_tag_ram(struct nvkm_ltc *ltc)
{
	struct nvkm_ram *ram = ltc->subdev.device->fb->ram;
	u32 tag_size, tag_margin, tag_align;
	int ret;

	/* No VRAM, no tags for now. */
	if (!ram) {
		ltc->num_tags = 0;
		goto mm_init;
	}

	/* tags for 1/4 of VRAM should be enough (8192/4 per GiB of VRAM) */
	ltc->num_tags = (ram->size >> 17) / 4;
	if (ltc->num_tags > (1 << 17))
		ltc->num_tags = 1 << 17; /* we have 17 bits in PTE */
	ltc->num_tags = (ltc->num_tags + 63) & ~63; /* round up to 64 */

	tag_align = ltc->ltc_nr * 0x800;
	tag_margin = (tag_align < 0x6000) ? 0x6000 : tag_align;

	/* 4 part 4 sub: 0x2000 bytes for 56 tags */
	/* 3 part 4 sub: 0x6000 bytes for 168 tags */
	/*
	 * About 147 bytes per tag. Let's be safe and allocate x2, which makes
	 * 0x4980 bytes for 64 tags, and round up to 0x6000 bytes for 64 tags.
	 *
	 * For 4 GiB of memory we'll have 8192 tags which makes 3 MiB, < 0.1 %.
	 */
	tag_size  = (ltc->num_tags / 64) * 0x6000 + tag_margin;
	tag_size += tag_align;
	tag_size  = (tag_size + 0xfff) >> 12; /* round up */

	ret = nvkm_mm_tail(&ram->vram, 1, 1, tag_size, tag_size, 1,
			   &ltc->tag_ram);
	if (ret) {
		ltc->num_tags = 0;
	} else {
		u64 tag_base = ((u64)ltc->tag_ram->offset << 12) + tag_margin;

		tag_base += tag_align - 1;
		do_div(tag_base, tag_align);

		ltc->tag_base = tag_base;
	}

mm_init:
	return nvkm_mm_init(&ltc->tags, 0, ltc->num_tags, 1);
}

int
gf100_ltc_oneinit(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	const u32 parts = nvkm_rd32(device, 0x022438);
	const u32  mask = nvkm_rd32(device, 0x022554);
	const u32 slice = nvkm_rd32(device, 0x17e8dc) >> 28;
	int i;

	for (i = 0; i < parts; i++) {
		if (!(mask & (1 << i)))
			ltc->ltc_nr++;
	}
	ltc->lts_nr = slice;

	return gf100_ltc_oneinit_tag_ram(ltc);
}

static void
gf100_ltc_init(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	u32 lpg128 = !(nvkm_rd32(device, 0x100c80) & 0x00000001);

	nvkm_mask(device, 0x17e820, 0x00100000, 0x00000000); /* INTR_EN &= ~0x10 */
	nvkm_wr32(device, 0x17e8d8, ltc->ltc_nr);
	nvkm_wr32(device, 0x17e8d4, ltc->tag_base);
	nvkm_mask(device, 0x17e8c0, 0x00000002, lpg128 ? 0x00000002 : 0x00000000);
}

static const struct nvkm_ltc_func
gf100_ltc = {
	.oneinit = gf100_ltc_oneinit,
	.init = gf100_ltc_init,
	.intr = gf100_ltc_intr,
	.cbc_clear = gf100_ltc_cbc_clear,
	.cbc_wait = gf100_ltc_cbc_wait,
	.zbc = 16,
	.zbc_clear_color = gf100_ltc_zbc_clear_color,
	.zbc_clear_depth = gf100_ltc_zbc_clear_depth,
	.invalidate = gf100_ltc_invalidate,
	.flush = gf100_ltc_flush,
};

int
gf100_ltc_new(struct nvkm_device *device, int index, struct nvkm_ltc **pltc)
{
	return nvkm_ltc_new_(&gf100_ltc, device, index, pltc);
}
