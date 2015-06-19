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
#include "gf100.h"

static const struct nvkm_specdom
gk104_pm_hub[] = {
	{ 0x60, (const struct nvkm_specsig[]) {
			{ 0x47, "hub00_user_0" },
			{}
		}, &gf100_perfctr_func },
	{ 0x40, (const struct nvkm_specsig[]) {
			{ 0x27, "hub01_user_0" },
			{}
		}, &gf100_perfctr_func },
	{ 0x60, (const struct nvkm_specsig[]) {
			{ 0x47, "hub02_user_0" },
			{}
		}, &gf100_perfctr_func },
	{ 0x60, (const struct nvkm_specsig[]) {
			{ 0x47, "hub03_user_0" },
			{}
		}, &gf100_perfctr_func },
	{ 0x40, (const struct nvkm_specsig[]) {
			{ 0x03, "host_mmio_rd" },
			{ 0x27, "hub04_user_0" },
			{}
		}, &gf100_perfctr_func },
	{ 0x60, (const struct nvkm_specsig[]) {
			{ 0x47, "hub05_user_0" },
			{}
		}, &gf100_perfctr_func },
	{ 0xc0, (const struct nvkm_specsig[]) {
			{ 0x74, "host_fb_rd3x" },
			{ 0x75, "host_fb_rd3x_2" },
			{ 0xa7, "hub06_user_0" },
			{}
		}, &gf100_perfctr_func },
	{ 0x60, (const struct nvkm_specsig[]) {
			{ 0x47, "hub07_user_0" },
			{}
		}, &gf100_perfctr_func },
	{}
};

static const struct nvkm_specdom
gk104_pm_gpc[] = {
	{ 0xe0, (const struct nvkm_specsig[]) {
			{ 0xc7, "gpc00_user_0" },
			{}
		}, &gf100_perfctr_func },
	{}
};

static const struct nvkm_specdom
gk104_pm_part[] = {
	{ 0x60, (const struct nvkm_specsig[]) {
			{ 0x47, "part00_user_0" },
			{}
		}, &gf100_perfctr_func },
	{ 0x60, (const struct nvkm_specsig[]) {
			{ 0x47, "part01_user_0" },
			{}
		}, &gf100_perfctr_func },
	{}
};

struct nvkm_oclass *
gk104_pm_oclass = &(struct gf100_pm_oclass) {
	.base.handle = NV_ENGINE(PM, 0xe0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_pm_ctor,
		.dtor = _nvkm_pm_dtor,
		.init = _nvkm_pm_init,
		.fini = gf100_pm_fini,
	},
	.doms_gpc  = gk104_pm_gpc,
	.doms_hub  = gk104_pm_hub,
	.doms_part = gk104_pm_part,
}.base;
