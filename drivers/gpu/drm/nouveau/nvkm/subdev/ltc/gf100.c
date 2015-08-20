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
gf100_ltc_cbc_clear(struct nvkm_ltc_priv *ltc, u32 start, u32 limit)
{
	nv_wr32(ltc, 0x17e8cc, start);
	nv_wr32(ltc, 0x17e8d0, limit);
	nv_wr32(ltc, 0x17e8c8, 0x00000004);
}

void
gf100_ltc_cbc_wait(struct nvkm_ltc_priv *ltc)
{
	int c, s;
	for (c = 0; c < ltc->ltc_nr; c++) {
		for (s = 0; s < ltc->lts_nr; s++)
			nv_wait(ltc, 0x1410c8 + c * 0x2000 + s * 0x400, ~0, 0);
	}
}

void
gf100_ltc_zbc_clear_color(struct nvkm_ltc_priv *ltc, int i, const u32 color[4])
{
	nv_mask(ltc, 0x17ea44, 0x0000000f, i);
	nv_wr32(ltc, 0x17ea48, color[0]);
	nv_wr32(ltc, 0x17ea4c, color[1]);
	nv_wr32(ltc, 0x17ea50, color[2]);
	nv_wr32(ltc, 0x17ea54, color[3]);
}

void
gf100_ltc_zbc_clear_depth(struct nvkm_ltc_priv *ltc, int i, const u32 depth)
{
	nv_mask(ltc, 0x17ea44, 0x0000000f, i);
	nv_wr32(ltc, 0x17ea58, depth);
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
gf100_ltc_lts_intr(struct nvkm_ltc_priv *ltc, int c, int s)
{
	u32 base = 0x141000 + (c * 0x2000) + (s * 0x400);
	u32 intr = nv_rd32(ltc, base + 0x020);
	u32 stat = intr & 0x0000ffff;

	if (stat) {
		nv_info(ltc, "LTC%d_LTS%d:", c, s);
		nvkm_bitfield_print(gf100_ltc_lts_intr_name, stat);
		pr_cont("\n");
	}

	nv_wr32(ltc, base + 0x020, intr);
}

void
gf100_ltc_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_ltc_priv *ltc = (void *)subdev;
	u32 mask;

	mask = nv_rd32(ltc, 0x00017c);
	while (mask) {
		u32 s, c = __ffs(mask);
		for (s = 0; s < ltc->lts_nr; s++)
			gf100_ltc_lts_intr(ltc, c, s);
		mask &= ~(1 << c);
	}
}

static int
gf100_ltc_init(struct nvkm_object *object)
{
	struct nvkm_ltc_priv *ltc = (void *)object;
	u32 lpg128 = !(nv_rd32(ltc, 0x100c80) & 0x00000001);
	int ret;

	ret = nvkm_ltc_init(ltc);
	if (ret)
		return ret;

	nv_mask(ltc, 0x17e820, 0x00100000, 0x00000000); /* INTR_EN &= ~0x10 */
	nv_wr32(ltc, 0x17e8d8, ltc->ltc_nr);
	nv_wr32(ltc, 0x17e8d4, ltc->tag_base);
	nv_mask(ltc, 0x17e8c0, 0x00000002, lpg128 ? 0x00000002 : 0x00000000);
	return 0;
}

void
gf100_ltc_dtor(struct nvkm_object *object)
{
	struct nvkm_fb *fb = nvkm_fb(object);
	struct nvkm_ltc_priv *ltc = (void *)object;

	nvkm_mm_fini(&ltc->tags);
	if (fb->ram)
		nvkm_mm_free(&fb->vram, &ltc->tag_ram);

	nvkm_ltc_destroy(ltc);
}

/* TODO: Figure out tag memory details and drop the over-cautious allocation.
 */
int
gf100_ltc_init_tag_ram(struct nvkm_fb *fb, struct nvkm_ltc_priv *ltc)
{
	u32 tag_size, tag_margin, tag_align;
	int ret;

	/* No VRAM, no tags for now. */
	if (!fb->ram) {
		ltc->num_tags = 0;
		goto mm_init;
	}

	/* tags for 1/4 of VRAM should be enough (8192/4 per GiB of VRAM) */
	ltc->num_tags = (fb->ram->size >> 17) / 4;
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

	ret = nvkm_mm_tail(&fb->vram, 1, 1, tag_size, tag_size, 1,
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
	ret = nvkm_mm_init(&ltc->tags, 0, ltc->num_tags, 1);
	return ret;
}

int
gf100_ltc_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_fb *fb = nvkm_fb(parent);
	struct nvkm_ltc_priv *ltc;
	u32 parts, mask;
	int ret, i;

	ret = nvkm_ltc_create(parent, engine, oclass, &ltc);
	*pobject = nv_object(ltc);
	if (ret)
		return ret;

	parts = nv_rd32(ltc, 0x022438);
	mask = nv_rd32(ltc, 0x022554);
	for (i = 0; i < parts; i++) {
		if (!(mask & (1 << i)))
			ltc->ltc_nr++;
	}
	ltc->lts_nr = nv_rd32(ltc, 0x17e8dc) >> 28;

	ret = gf100_ltc_init_tag_ram(fb, ltc);
	if (ret)
		return ret;

	nv_subdev(ltc)->intr = gf100_ltc_intr;
	return 0;
}

struct nvkm_oclass *
gf100_ltc_oclass = &(struct nvkm_ltc_impl) {
	.base.handle = NV_SUBDEV(LTC, 0xc0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_ltc_ctor,
		.dtor = gf100_ltc_dtor,
		.init = gf100_ltc_init,
		.fini = _nvkm_ltc_fini,
	},
	.intr = gf100_ltc_intr,
	.cbc_clear = gf100_ltc_cbc_clear,
	.cbc_wait = gf100_ltc_cbc_wait,
	.zbc = 16,
	.zbc_clear_color = gf100_ltc_zbc_clear_color,
	.zbc_clear_depth = gf100_ltc_zbc_clear_depth,
}.base;
