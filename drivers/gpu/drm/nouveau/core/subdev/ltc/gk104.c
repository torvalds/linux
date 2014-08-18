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

static int
gk104_ltc_init(struct nouveau_object *object)
{
	struct nvkm_ltc_priv *priv = (void *)object;
	int ret;

	ret = nvkm_ltc_init(priv);
	if (ret)
		return ret;

	nv_wr32(priv, 0x17e8d8, priv->ltc_nr);
	nv_wr32(priv, 0x17e000, priv->ltc_nr);
	nv_wr32(priv, 0x17e8d4, priv->tag_base);
	return 0;
}

struct nouveau_oclass *
gk104_ltc_oclass = &(struct nvkm_ltc_impl) {
	.base.handle = NV_SUBDEV(LTC, 0xe4),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = gf100_ltc_ctor,
		.dtor = gf100_ltc_dtor,
		.init = gk104_ltc_init,
		.fini = _nvkm_ltc_fini,
	},
	.intr = gf100_ltc_intr,
	.cbc_clear = gf100_ltc_cbc_clear,
	.cbc_wait = gf100_ltc_cbc_wait,
	.zbc = 16,
	.zbc_clear_color = gf100_ltc_zbc_clear_color,
	.zbc_clear_depth = gf100_ltc_zbc_clear_depth,
}.base;
