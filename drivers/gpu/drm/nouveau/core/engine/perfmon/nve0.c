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

#include "nvc0.h"

/*******************************************************************************
 * Perfmon object classes
 ******************************************************************************/

/*******************************************************************************
 * PPM context
 ******************************************************************************/

/*******************************************************************************
 * PPM engine/subdev functions
 ******************************************************************************/

static const struct nouveau_specdom
nve0_perfmon_hub[] = {
	{ 0x60, (const struct nouveau_specsig[]) {
			{ 0x47, "hub00_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{ 0x40, (const struct nouveau_specsig[]) {
			{ 0x27, "hub01_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{ 0x60, (const struct nouveau_specsig[]) {
			{ 0x47, "hub02_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{ 0x60, (const struct nouveau_specsig[]) {
			{ 0x47, "hub03_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{ 0x40, (const struct nouveau_specsig[]) {
			{ 0x03, "host_mmio_rd" },
			{ 0x27, "hub04_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{ 0x60, (const struct nouveau_specsig[]) {
			{ 0x47, "hub05_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{ 0xc0, (const struct nouveau_specsig[]) {
			{ 0x74, "host_fb_rd3x" },
			{ 0x75, "host_fb_rd3x_2" },
			{ 0xa7, "hub06_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{ 0x60, (const struct nouveau_specsig[]) {
			{ 0x47, "hub07_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{}
};

static const struct nouveau_specdom
nve0_perfmon_gpc[] = {
	{ 0xe0, (const struct nouveau_specsig[]) {
			{ 0xc7, "gpc00_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{}
};

static const struct nouveau_specdom
nve0_perfmon_part[] = {
	{ 0x60, (const struct nouveau_specsig[]) {
			{ 0x47, "part00_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{ 0x60, (const struct nouveau_specsig[]) {
			{ 0x47, "part01_user_0" },
			{}
		}, &nvc0_perfctr_func },
	{}
};

static int
nve0_perfmon_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nvc0_perfmon_priv *priv;
	u32 mask;
	int ret;

	ret = nouveau_perfmon_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	/* PDAEMON */
	ret = nouveau_perfdom_new(&priv->base, "pwr", 0, 0, 0, 0,
				   nve0_perfmon_pwr);
	if (ret)
		return ret;

	/* HUB */
	ret = nouveau_perfdom_new(&priv->base, "hub", 0, 0x1b0000, 0, 0x200,
				   nve0_perfmon_hub);
	if (ret)
		return ret;

	/* GPC */
	mask  = (1 << nv_rd32(priv, 0x022430)) - 1;
	mask &= ~nv_rd32(priv, 0x022504);
	mask &= ~nv_rd32(priv, 0x022584);

	ret = nouveau_perfdom_new(&priv->base, "gpc", mask, 0x180000,
				  0x1000, 0x200, nve0_perfmon_gpc);
	if (ret)
		return ret;

	/* PART */
	mask  = (1 << nv_rd32(priv, 0x022438)) - 1;
	mask &= ~nv_rd32(priv, 0x022548);
	mask &= ~nv_rd32(priv, 0x0225c8);

	ret = nouveau_perfdom_new(&priv->base, "part", mask, 0x1a0000,
				  0x1000, 0x200, nve0_perfmon_part);
	if (ret)
		return ret;

	nv_engine(priv)->cclass = &nouveau_perfmon_cclass;
	nv_engine(priv)->sclass =  nouveau_perfmon_sclass;
	priv->base.last = 7;
	return 0;
}

struct nouveau_oclass
nve0_perfmon_oclass = {
	.handle = NV_ENGINE(PERFMON, 0xe0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nve0_perfmon_ctor,
		.dtor = _nouveau_perfmon_dtor,
		.init = _nouveau_perfmon_init,
		.fini = nvc0_perfmon_fini,
	},
};
