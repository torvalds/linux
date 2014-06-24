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

#include <subdev/fb.h>
#include <subdev/timer.h>

#include "gf100.h"

static void
gf100_ltcg_lts_isr(struct gf100_ltcg_priv *priv, int ltc, int lts)
{
	u32 base = 0x141000 + (ltc * 0x2000) + (lts * 0x400);
	u32 stat = nv_rd32(priv, base + 0x020);

	if (stat) {
		nv_info(priv, "LTC%d_LTS%d: 0x%08x\n", ltc, lts, stat);
		nv_wr32(priv, base + 0x020, stat);
	}
}

static void
gf100_ltcg_intr(struct nouveau_subdev *subdev)
{
	struct gf100_ltcg_priv *priv = (void *)subdev;
	u32 mask;

	mask = nv_rd32(priv, 0x00017c);
	while (mask) {
		u32 lts, ltc = __ffs(mask);
		for (lts = 0; lts < priv->lts_nr; lts++)
			gf100_ltcg_lts_isr(priv, ltc, lts);
		mask &= ~(1 << ltc);
	}

	/* we do something horribly wrong and upset PMFB a lot, so mask off
	 * interrupts from it after the first one until it's fixed
	 */
	nv_mask(priv, 0x000640, 0x02000000, 0x00000000);
}

int
gf100_ltcg_tags_alloc(struct nouveau_ltcg *ltcg, u32 n,
		     struct nouveau_mm_node **pnode)
{
	struct gf100_ltcg_priv *priv = (struct gf100_ltcg_priv *)ltcg;
	int ret;

	ret = nouveau_mm_head(&priv->tags, 1, n, n, 1, pnode);
	if (ret)
		*pnode = NULL;

	return ret;
}

void
gf100_ltcg_tags_free(struct nouveau_ltcg *ltcg, struct nouveau_mm_node **pnode)
{
	struct gf100_ltcg_priv *priv = (struct gf100_ltcg_priv *)ltcg;

	nouveau_mm_free(&priv->tags, pnode);
}

static void
gf100_ltcg_tags_clear(struct nouveau_ltcg *ltcg, u32 first, u32 count)
{
	struct gf100_ltcg_priv *priv = (struct gf100_ltcg_priv *)ltcg;
	u32 last = first + count - 1;
	int p, i;

	BUG_ON((first > last) || (last >= priv->num_tags));

	nv_wr32(priv, 0x17e8cc, first);
	nv_wr32(priv, 0x17e8d0, last);
	nv_wr32(priv, 0x17e8c8, 0x4); /* trigger clear */

	/* wait until it's finished with clearing */
	for (p = 0; p < priv->ltc_nr; ++p) {
		for (i = 0; i < priv->lts_nr; ++i)
			nv_wait(priv, 0x1410c8 + p * 0x2000 + i * 0x400, ~0, 0);
	}
}

/* TODO: Figure out tag memory details and drop the over-cautious allocation.
 */
int
gf100_ltcg_init_tag_ram(struct nouveau_fb *pfb, struct gf100_ltcg_priv *priv)
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

	ret = nouveau_mm_tail(&pfb->vram, 1, tag_size, tag_size, 1,
	                      &priv->tag_ram);
	if (ret) {
		priv->num_tags = 0;
	} else {
		u64 tag_base = (priv->tag_ram->offset << 12) + tag_margin;

		tag_base += tag_align - 1;
		ret = do_div(tag_base, tag_align);

		priv->tag_base = tag_base;
	}
	ret = nouveau_mm_init(&priv->tags, 0, priv->num_tags, 1);

	return ret;
}

static int
gf100_ltcg_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct gf100_ltcg_priv *priv;
	struct nouveau_fb *pfb = nouveau_fb(parent);
	u32 parts, mask;
	int ret, i;

	ret = nouveau_ltcg_create(parent, engine, oclass, &priv);
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

	ret = gf100_ltcg_init_tag_ram(pfb, priv);
	if (ret)
		return ret;

	priv->base.tags_alloc = gf100_ltcg_tags_alloc;
	priv->base.tags_free  = gf100_ltcg_tags_free;
	priv->base.tags_clear = gf100_ltcg_tags_clear;

	nv_subdev(priv)->intr = gf100_ltcg_intr;
	return 0;
}

void
gf100_ltcg_dtor(struct nouveau_object *object)
{
	struct nouveau_ltcg *ltcg = (struct nouveau_ltcg *)object;
	struct gf100_ltcg_priv *priv = (struct gf100_ltcg_priv *)ltcg;
	struct nouveau_fb *pfb = nouveau_fb(ltcg->base.base.parent);

	nouveau_mm_fini(&priv->tags);
	nouveau_mm_free(&pfb->vram, &priv->tag_ram);

	nouveau_ltcg_destroy(ltcg);
}

static int
gf100_ltcg_init(struct nouveau_object *object)
{
	struct nouveau_ltcg *ltcg = (struct nouveau_ltcg *)object;
	struct gf100_ltcg_priv *priv = (struct gf100_ltcg_priv *)ltcg;
	int ret;

	ret = nouveau_ltcg_init(ltcg);
	if (ret)
		return ret;

	nv_mask(priv, 0x17e820, 0x00100000, 0x00000000); /* INTR_EN &= ~0x10 */
	nv_wr32(priv, 0x17e8d8, priv->ltc_nr);
	if (nv_device(ltcg)->card_type >= NV_E0)
		nv_wr32(priv, 0x17e000, priv->ltc_nr);
	nv_wr32(priv, 0x17e8d4, priv->tag_base);
	return 0;
}

struct nouveau_oclass *
gf100_ltcg_oclass = &(struct nouveau_oclass) {
	.handle = NV_SUBDEV(LTCG, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = gf100_ltcg_ctor,
		.dtor = gf100_ltcg_dtor,
		.init = gf100_ltcg_init,
		.fini = _nouveau_ltcg_fini,
	},
};
