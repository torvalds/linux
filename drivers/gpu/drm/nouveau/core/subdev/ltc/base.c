/*
 * Copyright 2014 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include "priv.h"

static int
nvkm_ltc_tags_alloc(struct nouveau_ltc *ltc, u32 n,
		    struct nouveau_mm_node **pnode)
{
	struct nvkm_ltc_priv *priv = (void *)ltc;
	int ret;

	ret = nouveau_mm_head(&priv->tags, 1, n, n, 1, pnode);
	if (ret)
		*pnode = NULL;

	return ret;
}

static void
nvkm_ltc_tags_free(struct nouveau_ltc *ltc, struct nouveau_mm_node **pnode)
{
	struct nvkm_ltc_priv *priv = (void *)ltc;
	nouveau_mm_free(&priv->tags, pnode);
}

static void
nvkm_ltc_tags_clear(struct nouveau_ltc *ltc, u32 first, u32 count)
{
	const struct nvkm_ltc_impl *impl = (void *)nv_oclass(ltc);
	struct nvkm_ltc_priv *priv = (void *)ltc;
	const u32 limit = first + count - 1;

	BUG_ON((first > limit) || (limit >= priv->num_tags));

	impl->cbc_clear(priv, first, limit);
	impl->cbc_wait(priv);
}

int
nvkm_ltc_create_(struct nouveau_object *parent, struct nouveau_object *engine,
		 struct nouveau_oclass *oclass, int length, void **pobject)
{
	const struct nvkm_ltc_impl *impl = (void *)oclass;
	struct nvkm_ltc_priv *priv;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0, "PLTCG",
				     "l2c", length, pobject);
	priv = *pobject;
	if (ret)
		return ret;

	priv->base.base.intr = impl->intr;
	priv->base.tags_alloc = nvkm_ltc_tags_alloc;
	priv->base.tags_free = nvkm_ltc_tags_free;
	priv->base.tags_clear = nvkm_ltc_tags_clear;
	return 0;
}
