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

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/disp.h>
#include <subdev/bios/init.h>
#include <subdev/ibus.h>
#include <subdev/vga.h>

#include "nv50.h"

int
nv50_devinit_pll_set(struct nouveau_devinit *devinit, u32 type, u32 freq)
{
	struct nv50_devinit_priv *priv = (void *)devinit;
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_pll info;
	int N1, M1, N2, M2, P;
	int ret;

	ret = nvbios_pll_parse(bios, type, &info);
	if (ret) {
		nv_error(devinit, "failed to retrieve pll data, %d\n", ret);
		return ret;
	}

	ret = nv04_pll_calc(nv_subdev(devinit), &info, freq, &N1, &M1, &N2, &M2, &P);
	if (!ret) {
		nv_error(devinit, "failed pll calculation\n");
		return ret;
	}

	switch (info.type) {
	case PLL_VPLL0:
	case PLL_VPLL1:
		nv_wr32(priv, info.reg + 0, 0x10000611);
		nv_mask(priv, info.reg + 4, 0x00ff00ff, (M1 << 16) | N1);
		nv_mask(priv, info.reg + 8, 0x7fff00ff, (P  << 28) |
							(M2 << 16) | N2);
		break;
	case PLL_MEMORY:
		nv_mask(priv, info.reg + 0, 0x01ff0000, (P << 22) |
						        (info.bias_p << 19) |
							(P << 16));
		nv_wr32(priv, info.reg + 4, (N1 << 8) | M1);
		break;
	default:
		nv_mask(priv, info.reg + 0, 0x00070000, (P << 16));
		nv_wr32(priv, info.reg + 4, (N1 << 8) | M1);
		break;
	}

	return 0;
}

static u64
nv50_devinit_disable(struct nouveau_devinit *devinit)
{
	struct nv50_devinit_priv *priv = (void *)devinit;
	u32 r001540 = nv_rd32(priv, 0x001540);
	u64 disable = 0ULL;

	if (!(r001540 & 0x40000000))
		disable |= (1ULL << NVDEV_ENGINE_MPEG);

	return disable;
}

int
nv50_devinit_init(struct nouveau_object *object)
{
	struct nouveau_bios *bios = nouveau_bios(object);
	struct nouveau_ibus *ibus = nouveau_ibus(object);
	struct nv50_devinit_priv *priv = (void *)object;
	struct nvbios_outp info;
	struct dcb_output outp;
	u8  ver = 0xff, hdr, cnt, len;
	int ret, i = 0;

	if (!priv->base.post) {
		if (!nv_rdvgac(priv, 0, 0x00) &&
		    !nv_rdvgac(priv, 0, 0x1a)) {
			nv_info(priv, "adaptor not initialised\n");
			priv->base.post = true;
		}
	}

	/* some boards appear to require certain priv register timeouts
	 * to be bumped before runing devinit scripts.  not a clue why
	 * the vbios engineers didn't make the scripts just work...
	 */
	if (priv->base.post && ibus)
		nv_ofuncs(ibus)->init(nv_object(ibus));

	ret = nouveau_devinit_init(&priv->base);
	if (ret)
		return ret;

	/* if we ran the init tables, we have to execute the first script
	 * pointer of each dcb entry's display encoder table in order
	 * to properly initialise each encoder.
	 */
	while (priv->base.post && dcb_outp_parse(bios, i, &ver, &hdr, &outp)) {
		if (nvbios_outp_match(bios, outp.hasht, outp.hashm,
				     &ver, &hdr, &cnt, &len, &info)) {
			struct nvbios_init init = {
				.subdev = nv_subdev(priv),
				.bios = bios,
				.offset = info.script[0],
				.outp = &outp,
				.crtc = -1,
				.execute = 1,
			};

			nvbios_exec(&init);
		}
		i++;
	}

	return 0;
}

int
nv50_devinit_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nv50_devinit_priv *priv;
	int ret;

	ret = nouveau_devinit_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass *
nv50_devinit_oclass = &(struct nouveau_devinit_impl) {
	.base.handle = NV_SUBDEV(DEVINIT, 0x50),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_devinit_ctor,
		.dtor = _nouveau_devinit_dtor,
		.init = nv50_devinit_init,
		.fini = _nouveau_devinit_fini,
	},
	.pll_set = nv50_devinit_pll_set,
	.disable = nv50_devinit_disable,
	.post = nvbios_init,
}.base;
