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
gf100_ltc_cbc_clear(struct nvkm_ltc_priv *priv, u32 start, u32 limit)
{
	nv_wr32(priv, 0x17e8cc, start);
	nv_wr32(priv, 0x17e8d0, limit);
	nv_wr32(priv, 0x17e8c8, 0x00000004);
}

void
gf100_ltc_cbc_wait(struct nvkm_ltc_priv *priv)
{
	int c, s;
	for (c = 0; c < priv->ltc_nr; c++) {
		for (s = 0; s < priv->lts_nr; s++)
			nv_wait(priv, 0x1410c8 + c * 0x2000 + s * 0x400, ~0, 0);
	}
}

void
gf100_ltc_zbc_clear_color(struct nvkm_ltc_priv *priv, int i, const u32 color[4])
{
	nv_mask(priv, 0x17ea44, 0x0000000f, i);
	nv_wr32(priv, 0x17ea48, color[0]);
	nv_wr32(priv, 0x17ea4c, color[1]);
	nv_wr32(priv, 0x17ea50, color[2]);
	nv_wr32(priv, 0x17ea54, color[3]);
}

void
gf100_ltc_zbc_clear_depth(struct nvkm_ltc_priv *priv, int i, const u32 depth)
{
	nv_mask(priv, 0x17ea44, 0x0000000f, i);
	nv_wr32(priv, 0x17ea58, depth);
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
gf100_ltc_lts_intr(struct nvkm_ltc_priv *priv, int ltc, int lts)
{
	u32 base = 0x141000 + (ltc * 0x2000) + (lts * 0x400);
	u32 intr = nv_rd32(priv, base + 0x020);
	u32 stat = intr & 0x0000ffff;

	if (stat) {
		nv_info(priv, "LTC%d_LTS%d:", ltc, lts);
		nvkm_bitfield_print(gf100_ltc_lts_intr_name, stat);
		pr_cont("\n");
	}

	nv_wr32(priv, base + 0x020, intr);
}

void
gf100_ltc_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_ltc_priv *priv = (void *)subdev;
	u32 mask;

	mask = nv_rd32(priv, 0x00017c);
	while (mask) {
		u32 lts, ltc = __ffs(mask);
		for (lts = 0; lts < priv->lts_nr; lts++)
			gf100_ltc_lts_intr(priv, ltc, lts);
		mask &= ~(1 << ltc);
	}
}

static int
gf100_ltc_init(struct nvkm_object *object)
{
	struct nvkm_ltc_priv *priv = (void *)object;
	u32 lpg128 = !(nv_rd32(priv, 0x100c80) & 0x00000001);
	int ret;

	ret = nvkm_ltc_init(priv);
	if (ret)
		return ret;

	nv_mask(priv, 0x17e820, 0x00100000, 0x00000000); /* INTR_EN &= ~0x10 */
	nv_wr32(priv, 0x17e8d8, priv->ltc_nr);
	nv_wr32(priv, 0x17e8d4, priv->tag_base);
	nv_mask(priv, 0x17e8c0, 0x00000002, lpg128 ? 0x00000002 : 0x00000000);
	return 0;
}

void
gf100_ltc_dtor(struct nvkm_object *object)
{
	struct nvkm_fb *pfb = nvkm_fb(object);
	struct nvkm_ltc_priv *priv = (void *)object;

	nvkm_mm_fini(&priv->tags);
	nvkm_mm_free(&pfb->vram, &priv->tag_ram);

	nvkm_ltc_destroy(priv);
}

/* TODO: Figure out tag memory details and drop the over-cautious allocation.
 */
int
gf100_ltc_init_tag_ram(struct nvkm_fb *pfb, struct nvkm_ltc_priv *priv)
{
	u32 tag_size, tag_margin, tag_align;
	int ret;

	/* tags for 1/4 of VRAM should be enough (8192/4 per GiB of VRAM) */
	priv->num_tags = (pfb->ram->size >> 17) / 4;
	if (priv->num_tags > (1 << 17))
		priv->num_tags = 1 << 17; /* we have 17 bits in PTE */
	priv->num_tags = (priv->num_tags + 63) & ~63; /* round up to 64 */

	tag_align = priv->ltc_nr * 0x800;
	tag_margin = (tag_align < 0x6000) ? 0x6000 : tag_align;

	/* 4 part 4 sub: 0x2000 bytes for 56 tags */
	/* 3 part 4 sub: 0x6000 bytes for 168 tags */
	/*
	 * About 147 bytes per tag. Let's be safe and allocate x2, which makes
	 * 0x4980 bytes for 64 tags, and round up to 0x6000 bytes for 64 tags.
	 *
	 * For 4 GiB of memory we'll have 8192 tags which makes 3 MiB, < 0.1 %.
	 */
	tag_size  = (priv->num_tags / 64) * 0x6000 + tag_margin;
	tag_size += tag_align;
	tag_size  = (tag_size + 0xfff) >> 12; /* round up */

	ret = nvkm_mm_tail(&pfb->vram, 1, 1, tag_size, tag_size, 1,
			   &priv->tag_ram);
	if (ret) {
		priv->num_tags = 0;
	} else {
		u64 tag_base = ((u64)priv->tag_ram->offset << 12) + tag_margin;

		tag_base += tag_align - 1;
		ret = do_div(tag_base, tag_align);

		priv->tag_base = tag_base;
	}

	ret = nvkm_mm_init(&priv->tags, 0, priv->num_tags, 1);
	return ret;
}

int
gf100_ltc_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_fb *pfb = nvkm_fb(parent);
	struct nvkm_ltc_priv *priv;
	u32 parts, mask;
	int ret, i;

	ret = nvkm_ltc_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	parts = nv_rd32(priv, 0x022438);
	mask = nv_rd32(priv, 0x022554);
	for (i = 0; i < parts; i++) {
		if (!(mask & (1 << i)))
			priv->ltc_nr++;
	}
	priv->lts_nr = nv_rd32(priv, 0x17e8dc) >> 28;

	ret = gf100_ltc_init_tag_ram(pfb, priv);
	if (ret)
		return ret;

	nv_subdev(priv)->intr = gf100_ltc_intr;
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
