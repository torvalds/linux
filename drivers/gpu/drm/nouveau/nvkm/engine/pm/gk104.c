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

static const struct nvkm_specsrc
gk104_pmfb_sources[] = {
	{ 0x140028, (const struct nvkm_specmux[]) {
			{ 0x3fff, 0, "unk0" },
			{ 0x7, 16, "unk16" },
			{ 0x3, 24, "unk24" },
			{ 0x2, 28, "unk28" },
			{}
		}, "pmfb0_pm_unk28" },
	{ 0x14125c, (const struct nvkm_specmux[]) {
			{ 0x3fff, 0, "unk0" },
			{}
		}, "pmfb0_subp0_pm_unk25c" },
	{ 0x14165c, (const struct nvkm_specmux[]) {
			{ 0x3fff, 0, "unk0" },
			{}
		}, "pmfb0_subp1_pm_unk25c" },
	{ 0x141a5c, (const struct nvkm_specmux[]) {
			{ 0x3fff, 0, "unk0" },
			{}
		}, "pmfb0_subp2_pm_unk25c" },
	{ 0x141e5c, (const struct nvkm_specmux[]) {
			{ 0x3fff, 0, "unk0" },
			{}
		}, "pmfb0_subp3_pm_unk25c" },
	{}
};

static const struct nvkm_specsrc
gk104_tex_sources[] = {
	{ 0x5042c0, (const struct nvkm_specmux[]) {
			{ 0xf, 0, "sel0", true },
			{ 0x7, 8, "sel1", true },
			{}
		}, "pgraph_gpc0_tpc0_tex_pm_mux_c_d" },
	{ 0x5042c8, (const struct nvkm_specmux[]) {
			{ 0x1f, 0, "sel", true },
			{}
		}, "pgraph_gpc0_tpc0_tex_pm_unkc8" },
	{ 0x5042b8, (const struct nvkm_specmux[]) {
			{ 0xff, 0, "sel", true },
			{}
		}, "pgraph_gpc0_tpc0_tex_pm_unkb8" },
	{}
};

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
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &gf100_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{ 0x00, "gpc02_tex_00", gk104_tex_sources },
			{ 0x01, "gpc02_tex_01", gk104_tex_sources },
			{ 0x02, "gpc02_tex_02", gk104_tex_sources },
			{ 0x03, "gpc02_tex_03", gk104_tex_sources },
			{ 0x04, "gpc02_tex_04", gk104_tex_sources },
			{ 0x05, "gpc02_tex_05", gk104_tex_sources },
			{ 0x06, "gpc02_tex_06", gk104_tex_sources },
			{ 0x07, "gpc02_tex_07", gk104_tex_sources },
			{ 0x08, "gpc02_tex_08", gk104_tex_sources },
			{ 0x0a, "gpc02_tex_0a", gk104_tex_sources },
			{ 0x0b, "gpc02_tex_0b", gk104_tex_sources },
			{ 0x0d, "gpc02_tex_0c", gk104_tex_sources },
			{ 0x0c, "gpc02_tex_0d", gk104_tex_sources },
			{ 0x0e, "gpc02_tex_0e", gk104_tex_sources },
			{ 0x0f, "gpc02_tex_0f", gk104_tex_sources },
			{ 0x10, "gpc02_tex_10", gk104_tex_sources },
			{ 0x11, "gpc02_tex_11", gk104_tex_sources },
			{ 0x12, "gpc02_tex_12", gk104_tex_sources },
			{}
		}, &gf100_perfctr_func },
	{}
};

static const struct nvkm_specdom
gk104_pm_part[] = {
	{ 0x60, (const struct nvkm_specsig[]) {
			{ 0x00, "part00_pbfb_00", gf100_pbfb_sources },
			{ 0x01, "part00_pbfb_01", gf100_pbfb_sources },
			{ 0x0c, "part00_pmfb_00", gk104_pmfb_sources },
			{ 0x0d, "part00_pmfb_01", gk104_pmfb_sources },
			{ 0x0e, "part00_pmfb_02", gk104_pmfb_sources },
			{ 0x0f, "part00_pmfb_03", gk104_pmfb_sources },
			{ 0x10, "part00_pmfb_04", gk104_pmfb_sources },
			{ 0x12, "part00_pmfb_05", gk104_pmfb_sources },
			{ 0x15, "part00_pmfb_06", gk104_pmfb_sources },
			{ 0x16, "part00_pmfb_07", gk104_pmfb_sources },
			{ 0x18, "part00_pmfb_08", gk104_pmfb_sources },
			{ 0x21, "part00_pmfb_09", gk104_pmfb_sources },
			{ 0x25, "part00_pmfb_0a", gk104_pmfb_sources },
			{ 0x26, "part00_pmfb_0b", gk104_pmfb_sources },
			{ 0x27, "part00_pmfb_0c", gk104_pmfb_sources },
			{ 0x47, "part00_user_0" },
			{}
		}, &gf100_perfctr_func },
	{ 0x60, (const struct nvkm_specsig[]) {
			{ 0x47, "part01_user_0" },
			{}
		}, &gf100_perfctr_func },
	{}
};

static const struct gf100_pm_func
gk104_pm = {
	.doms_gpc = gk104_pm_gpc,
	.doms_hub = gk104_pm_hub,
	.doms_part = gk104_pm_part,
};

int
gk104_pm_new(struct nvkm_device *device, int index, struct nvkm_pm **ppm)
{
	return gf100_pm_new_(&gk104_pm, device, index, ppm);
}
