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
#include "priv.h"

static void
pwr_perfctr_init(struct nvkm_pm *ppm, struct nvkm_perfdom *dom,
		 struct nvkm_perfctr *ctr)
{
	u32 mask = 0x00000000;
	u32 ctrl = 0x00000001;
	int i;

	for (i = 0; i < ARRAY_SIZE(ctr->signal) && ctr->signal[i]; i++)
		mask |= 1 << (ctr->signal[i] - dom->signal);

	nv_wr32(ppm, 0x10a504 + (ctr->slot * 0x10), mask);
	nv_wr32(ppm, 0x10a50c + (ctr->slot * 0x10), ctrl);
	nv_wr32(ppm, 0x10a50c + (ppm->last * 0x10), 0x00000003);
}

static void
pwr_perfctr_read(struct nvkm_pm *ppm, struct nvkm_perfdom *dom,
		 struct nvkm_perfctr *ctr)
{
	ctr->ctr = ppm->pwr[ctr->slot];
	ctr->clk = ppm->pwr[ppm->last];
}

static void
pwr_perfctr_next(struct nvkm_pm *ppm, struct nvkm_perfdom *dom)
{
	int i;

	for (i = 0; i <= ppm->last; i++) {
		ppm->pwr[i] = nv_rd32(ppm, 0x10a508 + (i * 0x10));
		nv_wr32(ppm, 0x10a508 + (i * 0x10), 0x80000000);
	}
}

static const struct nvkm_funcdom
pwr_perfctr_func = {
	.init = pwr_perfctr_init,
	.read = pwr_perfctr_read,
	.next = pwr_perfctr_next,
};

const struct nvkm_specdom
gt215_pm_pwr[] = {
	{ 0x20, (const struct nvkm_specsig[]) {
			{ 0x00, "pwr_gr_idle" },
			{ 0x04, "pwr_bsp_idle" },
			{ 0x05, "pwr_vp_idle" },
			{ 0x06, "pwr_ppp_idle" },
			{ 0x13, "pwr_ce0_idle" },
			{}
		}, &pwr_perfctr_func },
	{}
};

const struct nvkm_specdom
gf100_pm_pwr[] = {
	{ 0x20, (const struct nvkm_specsig[]) {
			{ 0x00, "pwr_gr_idle" },
			{ 0x04, "pwr_bsp_idle" },
			{ 0x05, "pwr_vp_idle" },
			{ 0x06, "pwr_ppp_idle" },
			{ 0x13, "pwr_ce0_idle" },
			{ 0x14, "pwr_ce1_idle" },
			{}
		}, &pwr_perfctr_func },
	{}
};

const struct nvkm_specdom
gk104_pm_pwr[] = {
	{ 0x20, (const struct nvkm_specsig[]) {
			{ 0x00, "pwr_gr_idle" },
			{ 0x04, "pwr_bsp_idle" },
			{ 0x05, "pwr_vp_idle" },
			{ 0x06, "pwr_ppp_idle" },
			{ 0x13, "pwr_ce0_idle" },
			{ 0x14, "pwr_ce1_idle" },
			{ 0x15, "pwr_ce2_idle" },
			{}
		}, &pwr_perfctr_func },
	{}
};
