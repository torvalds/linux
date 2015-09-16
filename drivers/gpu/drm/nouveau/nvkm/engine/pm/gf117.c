/*
 * Copyright 2015 Samuel Pitoiset
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
 * Authors: Samuel Pitoiset
 */
#include "gf100.h"

static const struct nvkm_specsrc
gf117_pmfb_sources[] = {
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
	{}
};

static const struct nvkm_specdom
gf117_pm_hub[] = {
	{}
};

static const struct nvkm_specdom
gf117_pm_part[] = {
	{ 0xe0, (const struct nvkm_specsig[]) {
			{ 0x00, "part00_pbfb_00", gf100_pbfb_sources },
			{ 0x01, "part00_pbfb_01", gf100_pbfb_sources },
			{ 0x12, "part00_pmfb_00", gf117_pmfb_sources },
			{ 0x15, "part00_pmfb_01", gf117_pmfb_sources },
			{ 0x16, "part00_pmfb_02", gf117_pmfb_sources },
			{ 0x18, "part00_pmfb_03", gf117_pmfb_sources },
			{ 0x1e, "part00_pmfb_04", gf117_pmfb_sources },
			{ 0x23, "part00_pmfb_05", gf117_pmfb_sources },
			{ 0x24, "part00_pmfb_06", gf117_pmfb_sources },
			{ 0x0c, "part00_pmfb_07", gf117_pmfb_sources },
			{ 0x0d, "part00_pmfb_08", gf117_pmfb_sources },
			{ 0x0e, "part00_pmfb_09", gf117_pmfb_sources },
			{ 0x0f, "part00_pmfb_0a", gf117_pmfb_sources },
			{ 0x10, "part00_pmfb_0b", gf117_pmfb_sources },
			{}
		}, &gf100_perfctr_func },
	{}
};

static const struct gf100_pm_func
gf117_pm = {
	.doms_gpc = gf100_pm_gpc,
	.doms_hub = gf117_pm_hub,
	.doms_part = gf117_pm_part,
};

int
gf117_pm_new(struct nvkm_device *device, int index, struct nvkm_pm **ppm)
{
	return gf100_pm_new_(&gf117_pm, device, index, ppm);
}
