/*
 * Copyright 2013 Red Hat Inc.
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

#include "nv50.h"

int
nva3_devinit_pll_set(struct nouveau_devinit *devinit, u32 type, u32 freq)
{
	struct nv50_devinit_priv *priv = (void *)devinit;
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_pll info;
	int N, fN, M, P;
	int ret;

	ret = nvbios_pll_parse(bios, type, &info);
	if (ret)
		return ret;

	ret = nva3_pll_calc(nv_subdev(devinit), &info, freq, &N, &fN, &M, &P);
	if (ret < 0)
		return ret;

	switch (info.type) {
	case PLL_VPLL0:
	case PLL_VPLL1:
		nv_wr32(priv, info.reg + 0, 0x50000610);
		nv_mask(priv, info.reg + 4, 0x003fffff,
					    (P << 16) | (M << 8) | N);
		nv_wr32(priv, info.reg + 8, fN);
		break;
	default:
		nv_warn(priv, "0x%08x/%dKhz unimplemented\n", type, freq);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static u64
nva3_devinit_disable(struct nouveau_devinit *devinit)
{
	struct nv50_devinit_priv *priv = (void *)devinit;
	u32 r001540 = nv_rd32(priv, 0x001540);
	u32 r00154c = nv_rd32(priv, 0x00154c);
	u64 disable = 0ULL;

	if (!(r001540 & 0x40000000)) {
		disable |= (1ULL << NVDEV_ENGINE_VP);
		disable |= (1ULL << NVDEV_ENGINE_PPP);
	}

	if (!(r00154c & 0x00000004))
		disable |= (1ULL << NVDEV_ENGINE_DISP);
	if (!(r00154c & 0x00000020))
		disable |= (1ULL << NVDEV_ENGINE_BSP);
	if (!(r00154c & 0x00000200))
		disable |= (1ULL << NVDEV_ENGINE_COPY0);

	return disable;
}

struct nouveau_oclass *
nva3_devinit_oclass = &(struct nouveau_devinit_impl) {
	.base.handle = NV_SUBDEV(DEVINIT, 0xa3),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_devinit_ctor,
		.dtor = _nouveau_devinit_dtor,
		.init = nv50_devinit_init,
		.fini = _nouveau_devinit_fini,
	},
	.pll_set = nva3_devinit_pll_set,
	.disable = nva3_devinit_disable,
}.base;
