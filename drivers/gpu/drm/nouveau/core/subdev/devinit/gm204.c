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
#include <subdev/bios/bit.h>
#include <subdev/bios/pmu.h>

#include "nv50.h"

static void
pmu_code(struct nv50_devinit_priv *priv, u32 pmu, u32 img, u32 len, bool sec)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	int i;

	nv_wr32(priv, 0x10a180, 0x01000000 | (sec ? 0x10000000 : 0) | pmu);
	for (i = 0; i < len; i += 4) {
		if ((i & 0xff) == 0)
			nv_wr32(priv, 0x10a188, (pmu + i) >> 8);
		nv_wr32(priv, 0x10a184, nv_ro32(bios, img + i));
	}

	while (i & 0xff) {
		nv_wr32(priv, 0x10a184, 0x00000000);
		i += 4;
	}
}

static void
pmu_data(struct nv50_devinit_priv *priv, u32 pmu, u32 img, u32 len)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	int i;

	nv_wr32(priv, 0x10a1c0, 0x01000000 | pmu);
	for (i = 0; i < len; i += 4)
		nv_wr32(priv, 0x10a1c4, nv_ro32(bios, img + i));
}

static u32
pmu_args(struct nv50_devinit_priv *priv, u32 argp, u32 argi)
{
	nv_wr32(priv, 0x10a1c0, argp);
	nv_wr32(priv, 0x10a1c0, nv_rd32(priv, 0x10a1c4) + argi);
	return nv_rd32(priv, 0x10a1c4);
}

static void
pmu_exec(struct nv50_devinit_priv *priv, u32 init_addr)
{
	nv_wr32(priv, 0x10a104, init_addr);
	nv_wr32(priv, 0x10a10c, 0x00000000);
	nv_wr32(priv, 0x10a100, 0x00000002);
}

static int
pmu_load(struct nv50_devinit_priv *priv, u8 type, bool post,
	 u32 *init_addr_pmu, u32 *args_addr_pmu)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_pmuR pmu;

	if (!nvbios_pmuRm(bios, type, &pmu)) {
		nv_error(priv, "VBIOS PMU fuc %02x not found\n", type);
		return -EINVAL;
	}

	if (!post)
		return 0;

	pmu_code(priv, pmu.boot_addr_pmu, pmu.boot_addr, pmu.boot_size, false);
	pmu_code(priv, pmu.code_addr_pmu, pmu.code_addr, pmu.code_size, true);
	pmu_data(priv, pmu.data_addr_pmu, pmu.data_addr, pmu.data_size);

	if (init_addr_pmu) {
		*init_addr_pmu = pmu.init_addr_pmu;
		*args_addr_pmu = pmu.args_addr_pmu;
		return 0;
	}

	return pmu_exec(priv, pmu.init_addr_pmu), 0;
}

static int
gm204_devinit_post(struct nouveau_subdev *subdev, bool post)
{
	struct nv50_devinit_priv *priv = (void *)nouveau_devinit(subdev);
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct bit_entry bit_I;
	u32 init, args;
	int ret;

	if (bit_entry(bios, 'I', &bit_I) || bit_I.version != 1 ||
					    bit_I.length < 0x1c) {
		nv_error(priv, "VBIOS PMU init data not found\n");
		return -EINVAL;
	}

	/* reset PMU and load init table parser ucode */
	if (post) {
		nv_mask(priv, 0x000200, 0x00002000, 0x00000000);
		nv_mask(priv, 0x000200, 0x00002000, 0x00002000);
		nv_rd32(priv, 0x000200);
		while (nv_rd32(priv, 0x10a10c) & 0x00000006) {
		}
	}

	ret = pmu_load(priv, 0x04, post, &init, &args);
	if (ret)
		return ret;

	/* upload first chunk of init data */
	if (post) {
		u32 pmu = pmu_args(priv, args + 0x08, 0x08);
		u32 img = nv_ro16(bios, bit_I.offset + 0x14);
		u32 len = nv_ro16(bios, bit_I.offset + 0x16);
		pmu_data(priv, pmu, img, len);
	}

	/* upload second chunk of init data */
	if (post) {
		u32 pmu = pmu_args(priv, args + 0x08, 0x10);
		u32 img = nv_ro16(bios, bit_I.offset + 0x18);
		u32 len = nv_ro16(bios, bit_I.offset + 0x1a);
		pmu_data(priv, pmu, img, len);
	}

	/* execute init tables */
	if (post) {
		nv_wr32(priv, 0x10a040, 0x00005000);
		pmu_exec(priv, init);
		while (!(nv_rd32(priv, 0x10a040) & 0x00002000)) {
		}
	}

	/* load and execute some other ucode image (bios therm?) */
	return pmu_load(priv, 0x01, post, NULL, NULL);
}

struct nouveau_oclass *
gm204_devinit_oclass = &(struct nouveau_devinit_impl) {
	.base.handle = NV_SUBDEV(DEVINIT, 0x07),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_devinit_ctor,
		.dtor = _nouveau_devinit_dtor,
		.init = nv50_devinit_init,
		.fini = _nouveau_devinit_fini,
	},
	.pll_set = nvc0_devinit_pll_set,
	.disable = gm107_devinit_disable,
	.post = gm204_devinit_post,
}.base;
