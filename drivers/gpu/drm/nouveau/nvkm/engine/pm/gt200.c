/*
 * Copyright 2015 Nouveau project
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
#include "nv40.h"

const struct nvkm_specsrc
gt200_crop_sources[] = {
	{ 0x407008, (const struct nvkm_specmux[]) {
			{ 0xf, 0, "sel0", true },
			{ 0x1f, 16, "sel1", true },
			{}
		}, "pgraph_rop0_crop_pm_mux" },
	{}
};

const struct nvkm_specsrc
gt200_prop_sources[] = {
	{ 0x408750, (const struct nvkm_specmux[]) {
			{ 0x3f, 0, "sel", true },
			{}
		}, "pgraph_tpc0_prop_pm_mux" },
	{}
};

const struct nvkm_specsrc
gt200_tex_sources[] = {
	{ 0x408508, (const struct nvkm_specmux[]) {
			{ 0xfffff, 0, "unk0" },
			{}
		}, "pgraph_tpc0_tex_unk08" },
	{}
};

static const struct nvkm_specdom
gt200_pm[] = {
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0xf0, (const struct nvkm_specsig[]) {
			{ 0xc9, "pc01_gr_idle" },
			{ 0x84, "pc01_strmout_00" },
			{ 0x85, "pc01_strmout_01" },
			{ 0xde, "pc01_trast_00" },
			{ 0xdf, "pc01_trast_01" },
			{ 0xe0, "pc01_trast_02" },
			{ 0xe1, "pc01_trast_03" },
			{ 0xe4, "pc01_trast_04" },
			{ 0xe5, "pc01_trast_05" },
			{ 0x82, "pc01_vattr_00" },
			{ 0x83, "pc01_vattr_01" },
			{ 0x46, "pc01_vfetch_00", g84_vfetch_sources },
			{ 0x47, "pc01_vfetch_01", g84_vfetch_sources },
			{ 0x48, "pc01_vfetch_02", g84_vfetch_sources },
			{ 0x49, "pc01_vfetch_03", g84_vfetch_sources },
			{ 0x4a, "pc01_vfetch_04", g84_vfetch_sources },
			{ 0x4b, "pc01_vfetch_05", g84_vfetch_sources },
			{ 0x4c, "pc01_vfetch_06", g84_vfetch_sources },
			{ 0x4d, "pc01_vfetch_07", g84_vfetch_sources },
			{ 0x4e, "pc01_vfetch_08", g84_vfetch_sources },
			{ 0x4f, "pc01_vfetch_09", g84_vfetch_sources },
			{ 0x50, "pc01_vfetch_0a", g84_vfetch_sources },
			{ 0x51, "pc01_vfetch_0b", g84_vfetch_sources },
			{ 0x52, "pc01_vfetch_0c", g84_vfetch_sources },
			{ 0x53, "pc01_vfetch_0d", g84_vfetch_sources },
			{ 0x54, "pc01_vfetch_0e", g84_vfetch_sources },
			{ 0x55, "pc01_vfetch_0f", g84_vfetch_sources },
			{ 0x56, "pc01_vfetch_10", g84_vfetch_sources },
			{ 0x57, "pc01_vfetch_11", g84_vfetch_sources },
			{ 0x58, "pc01_vfetch_12", g84_vfetch_sources },
			{ 0x59, "pc01_vfetch_13", g84_vfetch_sources },
			{ 0x5a, "pc01_vfetch_14", g84_vfetch_sources },
			{ 0x5b, "pc01_vfetch_15", g84_vfetch_sources },
			{ 0x5c, "pc01_vfetch_16", g84_vfetch_sources },
			{ 0x5d, "pc01_vfetch_17", g84_vfetch_sources },
			{ 0x5e, "pc01_vfetch_18", g84_vfetch_sources },
			{ 0x5f, "pc01_vfetch_19", g84_vfetch_sources },
			{ 0x07, "pc01_zcull_00", nv50_zcull_sources },
			{ 0x08, "pc01_zcull_01", nv50_zcull_sources },
			{ 0x09, "pc01_zcull_02", nv50_zcull_sources },
			{ 0x0a, "pc01_zcull_03", nv50_zcull_sources },
			{ 0x0b, "pc01_zcull_04", nv50_zcull_sources },
			{ 0x0c, "pc01_zcull_05", nv50_zcull_sources },

			{ 0xb0, "pc01_unk00" },
			{ 0xec, "pc01_trailer" },
			{}
		}, &nv40_perfctr_func },
	{ 0xf0, (const struct nvkm_specsig[]) {
			{ 0x55, "pc02_crop_00", gt200_crop_sources },
			{ 0x56, "pc02_crop_01", gt200_crop_sources },
			{ 0x57, "pc02_crop_02", gt200_crop_sources },
			{ 0x58, "pc02_crop_03", gt200_crop_sources },
			{ 0x00, "pc02_prop_00", gt200_prop_sources },
			{ 0x01, "pc02_prop_01", gt200_prop_sources },
			{ 0x02, "pc02_prop_02", gt200_prop_sources },
			{ 0x03, "pc02_prop_03", gt200_prop_sources },
			{ 0x04, "pc02_prop_04", gt200_prop_sources },
			{ 0x05, "pc02_prop_05", gt200_prop_sources },
			{ 0x06, "pc02_prop_06", gt200_prop_sources },
			{ 0x07, "pc02_prop_07", gt200_prop_sources },
			{ 0x78, "pc02_tex_00", gt200_tex_sources },
			{ 0x79, "pc02_tex_01", gt200_tex_sources },
			{ 0x7a, "pc02_tex_02", gt200_tex_sources },
			{ 0x7b, "pc02_tex_03", gt200_tex_sources },
			{ 0x32, "pc02_tex_04", gt200_tex_sources },
			{ 0x33, "pc02_tex_05", gt200_tex_sources },
			{ 0x34, "pc02_tex_06", gt200_tex_sources },
			{ 0x74, "pc02_zrop_00", nv50_zrop_sources },
			{ 0x75, "pc02_zrop_01", nv50_zrop_sources },
			{ 0x76, "pc02_zrop_02", nv50_zrop_sources },
			{ 0x77, "pc02_zrop_03", nv50_zrop_sources },
			{ 0xec, "pc02_trailer" },
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{}
};

int
gt200_pm_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_pm **ppm)
{
	return nv40_pm_new_(gt200_pm, device, type, inst, ppm);
}
